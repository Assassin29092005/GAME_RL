// Hellwalker — per-fighter combat component.
//
// The fighter's combat STATE lives in the engine-free core (HW::FFighter, stepped by UHWDuelSubsystem's
// frame cursor). This component is the actor's handle on it: read-only views for presentation and
// Blueprint, the swing-outcome delegate, and the one thing the core cannot do — move the capsule for a
// ghoststep / evade with a root-motion source (PLAN §5.3), removed on every interrupt path.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HWTypesUE.h"
#include "HWCore/HWFighter.h"
#include "HWCombatComponent.generated.h"

class UHWDuelSubsystem;

UCLASS(ClassGroup = (Hellwalker), meta = (BlueprintSpawnableComponent))
class HELLWALKERRL_API UHWCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHWCombatComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetSide(HW::ESide InSide) { Side = InSide; }
	HW::ESide GetSide() const { return Side; }

	/** Core state for this fighter (valid once the duel subsystem exists). */
	const HW::FFighter* GetFighter() const;

	UFUNCTION(BlueprintPure, Category = "Hellwalker|Combat")
	float GetHealthFraction() const;

	UFUNCTION(BlueprintPure, Category = "Hellwalker|Combat")
	float GetShaChiFraction() const;

	UFUNCTION(BlueprintPure, Category = "Hellwalker|Combat")
	int32 GetFramesUntilActionable() const;

	/** Ghoststep / evade: FRootMotionSource_MoveToForce over Duration seconds (PLAN §5.3). */
	void StartDisplacement(const FVector& WorldDisplacement, float DurationSeconds);
	/** Remove the root-motion source — called on every interrupt path. */
	void StopDisplacement();
	bool IsDisplacing() const { return RootMotionId != 0; }

	/** Swing outcomes where this fighter was the attacker (PLAN A1: one delegate). */
	UPROPERTY(BlueprintAssignable, Category = "Hellwalker|Combat")
	FHWSwingOutcomeSignature OnOutcome;

	/** Presentation flash timer (frames) and colour, set by the duel on outcomes. */
	int32 FlashFrames = 0;
	FLinearColor FlashColor = FLinearColor::White;

private:
	UHWDuelSubsystem* GetDuel() const;

	HW::ESide Side = HW::ESide::Player;
	uint16 RootMotionId = 0;
	int32 DisplacementCommitFrame = -1;
};
