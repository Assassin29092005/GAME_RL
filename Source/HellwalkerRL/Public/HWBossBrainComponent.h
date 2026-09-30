// Hellwalker — UBossBrainComponent (PLAN B1/B3).
//
// The decision function is plain C++ (the tier's HW::IBossBrain — the script, or the RL keeper — driven by the
// duel's HW::FEncounter), which is what makes the determinism tests implementable (PLAN §5.1 — no StateTree). This component is the brain's body: it executes the boss's movement from the core state
// (approach / retreat / dash; facing), and exposes the brain's reasoning to Blueprint and the HUD.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HWTypesUE.h"
#include "HWCore/HWBrain.h"
#include "HWBossBrainComponent.generated.h"

class UHWDuelSubsystem;

UCLASS(ClassGroup = (Hellwalker), meta = (BlueprintSpawnableComponent))
class HELLWALKERRL_API UHWBossBrainComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHWBossBrainComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** true = Hellwalker (the RL keeper reads you), false = Pathbreaker (the script, the control arm). */
	UFUNCTION(BlueprintPure, Category = "Hellwalker|Brain")
	bool IsAdaptationEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Hellwalker|Brain")
	FString GetLastReason() const;

	UFUNCTION(BlueprintPure, Category = "Hellwalker|Brain")
	float GetReadBits() const;

	const HW::IBossBrain* GetBrain() const;

private:
	UHWDuelSubsystem* GetDuel() const;
};
