#include "HWPlayerCharacter.h"

#include "HWCombatComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"

AHWPlayerCharacter::AHWPlayerCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(38.f, 90.f);
	Combat->SetSide(HW::ESide::Player);

	// ---- greybox body: indigo coat, crimson sash, steel twin blade / long glaive
	BodyColor = FLinearColor(0.04f, 0.05f, 0.11f);
	HeadColor = FLinearColor(0.16f, 0.11f, 0.08f);
	AccentColor = FLinearColor(0.55f, 0.03f, 0.03f);
	BladeColor = FLinearColor(0.78f, 0.8f, 0.86f);
	GhostColor = FLinearColor(0.05f, 0.12f, 0.35f);

	Torso = MakePart(TEXT("Torso"), CylinderMesh, VisualRoot, FVector(0.f, 0.f, -8.f), FVector(0.52f, 0.46f, 1.35f));
	Head = MakePart(TEXT("Head"), SphereMesh, VisualRoot, FVector(4.f, 0.f, 72.f), FVector(0.36f, 0.34f, 0.4f));
	Accent = MakePart(TEXT("Sash"), CylinderMesh, VisualRoot, FVector(0.f, 0.f, 18.f), FVector(0.56f, 0.5f, 0.12f));
	ArmPivot->SetRelativeLocation(FVector(12.f, 26.f, 38.f));
	BladeLength = 115.f;
	Blade = MakePart(TEXT("TwinBlade"), CubeMesh, BladeSlide, FVector(BladeLength * 0.5f, 0.f, 0.f), FVector(BladeLength / 100.f, 0.05f, 0.11f));
	BladeAlt = MakePart(TEXT("Glaive"), CubeMesh, BladeSlide, FVector(95.f, 0.f, 0.f), FVector(1.9f, 0.05f, 0.14f));
	BladeAlt->SetVisibility(false);

	// ---- camera: over the shoulder, behind the lock-on line
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->TargetArmLength = 640.f;
	CameraBoom->SocketOffset = FVector(0.f, 110.f, 190.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 12.f;
	CameraBoom->bDoCollisionTest = false;
	BaseSocketOffset = CameraBoom->SocketOffset;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->SetFieldOfView(80.f);

	// ---- movement
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	CMC->MaxWalkSpeed = HW::Tuning().PlayerWalkSpeed;
	CMC->RotationRate = FRotator(0.f, 900.f, 0.f);
	SetLockedOn(true);
}

void AHWPlayerCharacter::SetLockedOn(bool bLocked)
{
	// PLAN A0: on lock, the CHARACTER faces the target (controller yaw drives it); orient-to-movement off.
	bLockedOn = bLocked;
	bUseControllerRotationYaw = bLocked;
	GetCharacterMovement()->bOrientRotationToMovement = !bLocked;
}

void AHWPlayerCharacter::AddCameraKick(float Strength)
{
	Kick = FMath::Max(Kick, Strength);
}

void AHWPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (CameraBoom != nullptr)
	{
		Kick = FMath::Max(0.f, Kick - DeltaSeconds * 4.f);
		const float T = TimeAlive * 60.f;
		CameraBoom->SocketOffset = BaseSocketOffset + FVector(0.f, FMath::Sin(T * 1.7f), FMath::Sin(T * 2.3f)) * Kick * 12.f;
	}
}
