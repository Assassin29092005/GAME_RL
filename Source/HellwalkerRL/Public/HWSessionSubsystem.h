// HellwalkerRL — session state that outlives an encounter: what the keepers keep about YOU, and the RL keeper's weights.
//
// RL.md §8: one memory per game session (the RL keeper's recurrent state + the exchanges it perceived), carried from
// keeper to keeper and dropped on quit. A GameInstance subsystem lives exactly as long as the session, so "reset on
// quit" is structural. The weights (RL/Models/hellwalker_rl.hwrl, staged as Content/HellwalkerRL/RL/hellwalker_rl.hwrl)
// are loaded once; if the file is missing, the Hellwalker tier falls back to the script (the optional-asset pattern).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HWTypesUE.h"
#include "HWCore/HWRLObserver.h"
#include "HWCore/HWRLPolicy.h"
#include "HWSessionSubsystem.generated.h"

UCLASS()
class HELLWALKERRL_API UHWSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The RL keeper's weights; null when no model file loaded (then Hellwalker plays the script). */
	const HW::FRLPolicy* GetPolicy() const { return Policy.IsLoaded() ? &Policy : nullptr; }
	/** "loaded <path> (hidden 256)" or why not. */
	const FString& GetPolicyStatus() const { return PolicyStatus; }
	/** What the keepers keep about you this session. */
	HW::FRLSession& GetMemory() { return *Memory; }
	const HW::FRLSession& GetMemory() const { return *Memory; }
	/** Forget everything the keepers learned this session (hw.ResetModel, a new walk). */
	void ResetMemory();
	/** The read head, from the current memory: "if the keeper threw Move now, you would answer OutSym (OutP)". */
	bool PredictAnswer(HW::EMoveId Move, HW::ESym& OutSym, float& OutP) const;

	/** Current tier. Switching tier keeps the memory (it is you being read, whichever keeper reads). */
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

	/** Where the game looks for the weights (overridable with -HWPolicy=<file>). */
	static FString PolicyPath();

private:
	void LoadPolicy();

	HW::FRLPolicy Policy;
	TUniquePtr<HW::FRLSession> Memory;
	FString PolicyStatus;
};
