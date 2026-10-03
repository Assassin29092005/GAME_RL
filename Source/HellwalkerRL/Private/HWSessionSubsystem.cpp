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
	Memory->Reset();
	Notebook.Reset();
	AdaptiveSkill = AdaptiveStart;
	LastAdaptiveChange = 0.f;
	UE_LOG(LogHellwalkerRL, Log, TEXT("The keepers' memory of you was reset (memory, notebook, adaptive skill)."));
}

int32 UHWSessionSubsystem::AdaptiveSwingGap(float Skill)
{
	return Skill < 0.15f ? FMath::RoundToInt(static_cast<float>(HW::RL::EasySwingGap) * (0.15f - FMath::Max(Skill, 0.f)) / 0.15f) : 0;
}

void UHWSessionSubsystem::GetKeeperConfig(float& OutSkill, float& OutTemperature, bool& bOutAdaptive, int32& OutSwingGap) const
{
	OutSkill = 1.f;
	OutTemperature = 0.f;
	bOutAdaptive = false;
	OutSwingGap = 0;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UHWSettingsSubsystem* S = GI->GetSubsystem<UHWSettingsSubsystem>())
		{
			S->GetKeeperSkill(OutSkill, OutTemperature, bOutAdaptive);
			OutSwingGap = UHWSettingsSubsystem::KeeperSwingGapFor(S->GetDifficulty());
		}
	}
	if (bOutAdaptive)
	{
		OutSkill = AdaptiveSkill;
		OutTemperature = AdaptiveTemperature(AdaptiveSkill);
		OutSwingGap = AdaptiveSwingGap(AdaptiveSkill);
	}
}

float UHWSessionSubsystem::NextAdaptiveSkill(float Skill, bool bPlayerWon, float PlayerHealthFrac, float BossHealthFrac)
{
	float Delta = 0.f;
	if (bPlayerWon) { Delta = PlayerHealthFrac >= 0.4f ? 0.10f : 0.04f; }   // too easy: it gets sharper
	else { Delta = BossHealthFrac >= 0.4f ? -0.10f : -0.03f; }             // too hard: it eases off
	return FMath::Clamp(Skill + Delta, 0.f, 1.f);
}

float UHWSessionSubsystem::RecordFightForAdaptive(bool bPlayerWon, float PlayerHealthFrac, float BossHealthFrac)
{
	float Skill = 1.f, Temperature = 0.f;
	bool bAdaptive = false;
	int32 Gap = 0;
	GetKeeperConfig(Skill, Temperature, bAdaptive, Gap);
	LastAdaptiveChange = 0.f;
	if (!bAdaptive) { return 0.f; }
	const float Next = NextAdaptiveSkill(AdaptiveSkill, bPlayerWon, PlayerHealthFrac, BossHealthFrac);
	LastAdaptiveChange = Next - AdaptiveSkill;
	UE_LOG(LogHellwalkerRL, Log, TEXT("Adaptive difficulty: %s with %.0f%% / %.0f%% health left -> the keeper's skill %.2f -> %.2f."),
		bPlayerWon ? TEXT("you won") : TEXT("you lost"), PlayerHealthFrac * 100.f, BossHealthFrac * 100.f, AdaptiveSkill, Next);
	AdaptiveSkill = Next;
	return LastAdaptiveChange;
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
	return InTier == EHWTier::Hellwalker ? TEXT("HELLWALKER") : TEXT("PATHBREAKER");
}
