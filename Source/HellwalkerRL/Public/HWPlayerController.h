// Hellwalker — player controller. All Enhanced Input is created in C++ at runtime (no .uasset):
// actions, the mapping contexts and their modifiers. Presses become timestamped duel input; the camera
// lock-on runs in PlayerTick (PLAN A0).
//
// Two modes. COMBAT (possessing Soul in a duel): the duel context. EXPLORE (the open world): a small
// context — interact, menu choices, quit — on top of whatever the explorer pawn adds for itself (the Game
// Animation Sample's character adds its own mapping context when possessed). Every possession clears the
// mappings first, so the two sets never fight over a key.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HWCore/HWTypes.h"
#include "HWPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UHWDuelSubsystem;
class AHWPlayerCharacter;
struct FInputActionValue;

UCLASS()
class HELLWALKERRL_API AHWPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AHWPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	bool IsLockedOn() const { return bLockedOn; }

	/**
	 * PLAN A0 done-test: strafe around the boss for Seconds while locked on and track the worst angle
	 * between the CHARACTER's forward vector and the direction to the boss (asserting the camera would pass
	 * while the character faces away — which silently voids the target-space requirement).
	 */
	void StartStrafeTest(float Seconds, float ToleranceDegrees);

	/**
	 * Every live key binding, read back from the mapping contexts themselves: ours (duel, explore) and the
	 * explorer pawn's own (the Game Animation Sample's IMC_Sandbox), with triggers and modifiers. hw.Controls
	 * writes it to Saved/Hellwalker/Controls.txt — the controls map is generated, never hand-copied.
	 */
	FString DescribeControls() const;

private:
	void BuildInput();
	UHWDuelSubsystem* GetDuel() const;
	AHWPlayerCharacter* GetFighter() const;
	/** Map the current movement input onto a TARGET-space ghoststep direction (PLAN §2.1). */
	HW::EDir StepDirectionFromInput() const;

	void OnMove(const FInputActionValue& Value);
	void OnMoveStop(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnLight();
	void OnHeavy();
	void OnParry();
	void OnStep();
	void OnSwitch();
	void OnGuardStart();
	void OnGuardEnd();
	void OnLockToggle();
	void OnRestart();
	void OnTierPathbreaker();
	void OnTierHellwalker();
	void OnDebugToggle();
	void OnHelpToggle();
	void OnQuit();
	void OnInteract();
	void OnChoice(int32 Choice);
	void OnContinue();
	/** COMBAT or EXPLORE mappings for the current pawn. */
	void ApplyInputMode();
	bool IsOpenWorld() const;

	UPROPERTY() TObjectPtr<UInputMappingContext> Context;
	UPROPERTY() TObjectPtr<UInputAction> IA_Move;
	UPROPERTY() TObjectPtr<UInputAction> IA_Look;
	UPROPERTY() TObjectPtr<UInputAction> IA_Light;
	UPROPERTY() TObjectPtr<UInputAction> IA_Heavy;
	UPROPERTY() TObjectPtr<UInputAction> IA_Parry;
	UPROPERTY() TObjectPtr<UInputAction> IA_Step;
	UPROPERTY() TObjectPtr<UInputAction> IA_Switch;
	UPROPERTY() TObjectPtr<UInputAction> IA_Guard;
	UPROPERTY() TObjectPtr<UInputAction> IA_Lock;
	UPROPERTY() TObjectPtr<UInputAction> IA_Restart;
	UPROPERTY() TObjectPtr<UInputAction> IA_Tier1;
	UPROPERTY() TObjectPtr<UInputAction> IA_Tier2;
	UPROPERTY() TObjectPtr<UInputAction> IA_Debug;
	UPROPERTY() TObjectPtr<UInputAction> IA_Help;
	UPROPERTY() TObjectPtr<UInputAction> IA_Quit;
	UPROPERTY() TObjectPtr<UInputMappingContext> ExploreContext;
	UPROPERTY() TObjectPtr<UInputAction> IA_Interact;
	UPROPERTY() TObjectPtr<UInputAction> IA_Choice1;
	UPROPERTY() TObjectPtr<UInputAction> IA_Choice2;
	UPROPERTY() TObjectPtr<UInputAction> IA_Choice3;
	UPROPERTY() TObjectPtr<UInputAction> IA_Continue;
	UPROPERTY() TObjectPtr<UInputAction> IA_ExploreQuit;

	FVector2D MoveInput = FVector2D::ZeroVector;
	bool bLockedOn = true;
	float StrafeTestLeft = 0.f;
	float StrafeTestDuration = 0.f;
	float StrafeTolerance = 10.f;
	float StrafeWorstDegrees = 0.f;
	float StrafeYawTravelled = 0.f;
	float StrafeLastYaw = 0.f;
	bool bGuardDown = false;
};
