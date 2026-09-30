// Hellwalker — the player: "Soul", a greybox wanderer with twin blades and a glaive (C1).

#pragma once

#include "CoreMinimal.h"
#include "HWCharacterBase.h"
#include "HWPlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;

UCLASS()
class HELLWALKERRL_API AHWPlayerCharacter : public AHWCharacterBase
{
	GENERATED_BODY()

public:
	AHWPlayerCharacter();

	virtual void Tick(float DeltaSeconds) override;

	/** Lock-on (PLAN A0): the character faces the boss; ghoststep directions are in target space. */
	void SetLockedOn(bool bLocked);
	bool IsLockedOn() const { return bLockedOn; }

	/** Presentation: a short decaying camera kick when hit / on a parry. */
	void AddCameraKick(float Strength);

	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<UCameraComponent> FollowCamera;

	bool bLockedOn = true;
	float Kick = 0.f;
	FVector BaseSocketOffset = FVector::ZeroVector;
};
