// Hellwalker — session state that outlives an encounter: the playstyle model.
//
// PLAN §2.3 / §2.5: persist within a session (encounter 3 knows what 1-2 learned — this is what makes
// §1.1's naming test possible); reset on quit. A GameInstance subsystem lives exactly as long as the
// session, so "reset on quit" is structural.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HWTypesUE.h"
#include "HWCore/HWPlaystyleModel.h"
#include "HWSessionSubsystem.generated.h"

UCLASS()
class HELLWALKERRL_API UHWSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	HW::FPlaystyleModel& GetModel() { return *Model; }
	const HW::FPlaystyleModel& GetModel() const { return *Model; }
	void ResetModel();

	/** Current tier. Switching tier keeps the model (the model observes in both tiers; only Hellwalker uses it). */
	UPROPERTY(BlueprintReadOnly, Category = "Hellwalker")
	EHWTier Tier = EHWTier::Hellwalker;

	/** Blind mode for B4: the HUD shows "Variant A / B" instead of tier names. */
	UPROPERTY(BlueprintReadOnly, Category = "Hellwalker")
	bool bBlind = false;

	/** Blind mapping: which tier is "A". Randomised once per session so testers cannot learn it. */
	UPROPERTY(BlueprintReadOnly, Category = "Hellwalker")
	EHWTier BlindVariantA = EHWTier::Pathbreaker;

	UPROPERTY(BlueprintReadOnly, Category = "Hellwalker")
	int32 EncountersStarted = 0;

	/** Session id used to name telemetry files (A2). */
	UPROPERTY(BlueprintReadOnly, Category = "Hellwalker")
	FString SessionId;

	FString TierLabel(EHWTier InTier) const;

private:
	TUniquePtr<HW::FPlaystyleModel> Model;
};
