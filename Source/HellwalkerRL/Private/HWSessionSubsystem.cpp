#include "HWSessionSubsystem.h"

#include "HellwalkerRL.h"
#include "HWSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

void UHWSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Memory = MakeUnique<HW::FRLSession>();
	Memory->Reset();
	SessionId = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	// Blind A/B assignment: derived from the session clock, fixed for the whole session.
	BlindVariantA = (FDateTime::Now().GetSecond() % 2 == 0) ? EHWTier::Pathbreaker : EHWTier::Hellwalker;
	LoadPolicy();
	UE_LOG(LogHellwalkerRL, Log, TEXT("HellwalkerRL session %s started (the keepers know nothing about you yet). RL keeper: %s"),
		*SessionId, *PolicyStatus);
}

void UHWSessionSubsystem::Deinitialize()
{
	// Reset on quit (RL.md §8): the memory dies with the session.
	Memory.Reset();
	Super::Deinitialize();
}

FString UHWSessionSubsystem::PolicyPath()
{
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("-HWPolicy="), Override) && !Override.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(Override);
	}
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("HellwalkerRL/RL/hellwalker_rl.hwrl"));
}

void UHWSessionSubsystem::LoadPolicy()
{
	const FString Path = PolicyPath();
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent))
	{
		PolicyStatus = FString::Printf(TEXT("no model at %s — Hellwalker plays the script"), *Path);
		UE_LOG(LogHellwalkerRL, Warning, TEXT("RL keeper: %s"), *PolicyStatus);
		return;
	}
	if (!Policy.LoadFromMemory(Bytes.GetData(), static_cast<size_t>(Bytes.Num())))
	{
		PolicyStatus = FString::Printf(TEXT("could not load %s (%s) — Hellwalker plays the script"), *Path, UTF8_TO_TCHAR(Policy.GetError().c_str()));
		UE_LOG(LogHellwalkerRL, Error, TEXT("RL keeper: %s"), *PolicyStatus);
		return;
	}
	if (!Policy.IsBossPlayable())
	{
		// Loads, but the keeper brain could never run it (a player-side exploiter, another layout, a memory bigger than a
		// session holds): treat it as absent so the tier falls back to the script instead of a boss that never moves.
		PolicyStatus = FString::Printf(TEXT("%s is not a playable keeper policy (side %d, obs %d, hidden %d) — Hellwalker plays the script"),
			*FPaths::GetCleanFilename(Path), Policy.Side(), Policy.ObsDim(), Policy.HiddenSize());
		UE_LOG(LogHellwalkerRL, Error, TEXT("RL keeper: %s"), *PolicyStatus);
		Policy = HW::FRLPolicy();
		return;
	}
	PolicyStatus = FString::Printf(TEXT("loaded %s (%s, hidden %d)"), *FPaths::GetCleanFilename(Path),
		Policy.IsRecurrent() ? TEXT("recurrent") : TEXT("feed-forward"), Policy.HiddenSize());
}

void UHWSessionSubsystem::ResetMemory()
{
	if (Memory.IsValid()) { Memory->Reset(); }
	Notebook.Reset();
	Insight.Reset();
	LastInsightChange = 0.f;
	LastSkillChange = 0.f;
	InsightPredictionsSeen = 0;
	InsightCorrectSeen = 0;
	UE_LOG(LogHellwalkerRL, Log, TEXT("The keepers' memory of you was reset (memory, notebook, insight)."));
}

bool UHWSessionSubsystem::IsParityRun()
{
	FString Path;
	return FParse::Value(FCommandLine::Get(), TEXT("-HWParity="), Path) && !Path.IsEmpty();
}

void UHWSessionSubsystem::KeeperConfigFor(EHWDifficulty Difficulty, const HW::FRLInsight& InInsight, float& OutSkill, float& OutTemperature, int32& OutSwingGap)
{
	const FHWDifficultyPreset P = UHWSettingsSubsystem::PresetFor(Difficulty);
	int32_t Gap = 0;
	InInsight.KeeperConfig(P.SkillLo, P.SkillHi, OutSkill, OutTemperature, Gap);
	OutSwingGap = static_cast<int32>(Gap);
}

void UHWSessionSubsystem::GetKeeperConfig(float& OutSkill, float& OutTemperature, bool& bOutAdaptive, int32& OutSwingGap) const
{
	if (IsParityRun())
	{
		// What parity has always measured against the simulator: the keeper as trained, greedy, no breather.
		OutSkill = 1.f;
		OutTemperature = 0.f;
		bOutAdaptive = false;
		OutSwingGap = 0;
		return;
	}
	EHWDifficulty Difficulty = FHWSettingsData().Difficulty; // the default, when there are no settings (tests)
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UHWSettingsSubsystem* S = GI->GetSubsystem<UHWSettingsSubsystem>()) { Difficulty = S->GetDifficulty(); }
	}
	KeeperConfigFor(Difficulty, Insight, OutSkill, OutTemperature, OutSwingGap);
	bOutAdaptive = true;
}

float UHWSessionSubsystem::GetAdaptiveSkill() const
{
	float Skill = 1.f;
	float Temperature = 0.f;
	bool bAdaptive = false;
	int32 Gap = 0;
	GetKeeperConfig(Skill, Temperature, bAdaptive, Gap);
	return Skill;
}

float UHWSessionSubsystem::RecordFightForAdaptive(bool bPlayerWon, float PlayerHealthFrac, float BossHealthFrac)
{
	// This fight's read-head calls on the keeper's ATTACKS (how you answer a swing — what FRLInsight was tuned on, ThesisSim
	// --arc) = what the notebook's swing totals gained since the last record (FRLBrain::FlushNotebook has written the fight's
	// last answer by now). The totals rather than per-fight slots: those stop at FRLNotebook::MaxFights, and calls from an
	// abandoned fight (a restart) still say how well it reads you.
	if (Notebook.SwingPredictions < InsightPredictionsSeen || Notebook.SwingCorrect < InsightCorrectSeen)
	{
		InsightPredictionsSeen = 0; // the notebook was reset under us
		InsightCorrectSeen = 0;
	}
	const int32 Predictions = Notebook.SwingPredictions - InsightPredictionsSeen;
	const int32 Correct = FMath::Clamp(Notebook.SwingCorrect - InsightCorrectSeen, 0, Predictions);
	InsightPredictionsSeen = Notebook.SwingPredictions;
	InsightCorrectSeen = Notebook.SwingCorrect;

	const float SkillBefore = GetAdaptiveSkill();
	const float Before = Insight.Insight;
	Insight.AfterFight(Predictions, Correct);
	LastInsightChange = Insight.Insight - Before;
	LastSkillChange = GetAdaptiveSkill() - SkillBefore;
	UE_LOG(LogHellwalkerRL, Log, TEXT("Insight: %s (%.0f%% / %.0f%% health left); it called %d of your %d answers -> insight %.2f -> %.2f, the next fight's skill %.2f."),
		bPlayerWon ? TEXT("you won") : TEXT("you lost"), PlayerHealthFrac * 100.f, BossHealthFrac * 100.f, Correct, Predictions, Before, Insight.Insight,
		SkillBefore + LastSkillChange);
	return LastInsightChange;
}

bool UHWSessionSubsystem::PredictAnswer(HW::EMoveId Move, HW::ESym& OutSym, float& OutP) const
{
	const HW::FRLPolicy* P = GetPolicy();
	const int32 Action = HW::RL::MoveAction(Move);
	if (P == nullptr || !Memory.IsValid() || Memory->HiddenSize != P->HiddenSize() || Action < 0) { return false; }
	float Probs[HW::FRLPolicyOutput::MaxAux] = {};
	P->Aux(Memory->Hidden, Action, Probs);
	int32 Best = 0;
	for (int32 C = 1; C < P->AuxClasses(); ++C) { if (Probs[C] > Probs[Best]) { Best = C; } }
	OutSym = static_cast<HW::ESym>(Best);
	OutP = Probs[Best];
	return true;
}

FString UHWSessionSubsystem::TierLabel(EHWTier InTier) const
{
	if (bBlind)
	{
		return InTier == BlindVariantA ? TEXT("VARIANT A") : TEXT("VARIANT B");
	}
	// The modes' display names (the enum keeps the research names: Pathbreaker = the script, Hellwalker = the RL keeper).
	return InTier == EHWTier::Hellwalker ? TEXT("ADAPTIVE AI") : TEXT("NORMAL");
}
