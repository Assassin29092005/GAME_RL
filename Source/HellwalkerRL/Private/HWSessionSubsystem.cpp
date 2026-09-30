#include "HWSessionSubsystem.h"

#include "HellwalkerRL.h"
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
	PolicyStatus = FString::Printf(TEXT("loaded %s (%s, hidden %d)"), *FPaths::GetCleanFilename(Path),
		Policy.IsRecurrent() ? TEXT("recurrent") : TEXT("feed-forward"), Policy.HiddenSize());
}

void UHWSessionSubsystem::ResetMemory()
{
	Memory->Reset();
	UE_LOG(LogHellwalkerRL, Log, TEXT("The keepers' memory of you was reset."));
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
