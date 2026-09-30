#include "HWSessionSubsystem.h"

#include "HellwalkerRL.h"
#include "Misc/DateTime.h"

void UHWSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Model = MakeUnique<HW::FPlaystyleModel>();
	SessionId = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	// Blind A/B assignment: derived from the session clock, fixed for the whole session.
	BlindVariantA = (FDateTime::Now().GetSecond() % 2 == 0) ? EHWTier::Pathbreaker : EHWTier::Hellwalker;
	UE_LOG(LogHellwalkerRL, Log, TEXT("Hellwalker session %s started (playstyle model fresh)."), *SessionId);
}

void UHWSessionSubsystem::Deinitialize()
{
	// Reset on quit (PLAN §2.5): the model dies with the session.
	Model.Reset();
	Super::Deinitialize();
}

void UHWSessionSubsystem::ResetModel()
{
	Model->Reset();
	UE_LOG(LogHellwalkerRL, Log, TEXT("Playstyle model reset."));
}

FString UHWSessionSubsystem::TierLabel(EHWTier InTier) const
{
	if (bBlind)
	{
		return InTier == BlindVariantA ? TEXT("VARIANT A") : TEXT("VARIANT B");
	}
	return InTier == EHWTier::Hellwalker ? TEXT("HELLWALKER") : TEXT("PATHBREAKER");
}
