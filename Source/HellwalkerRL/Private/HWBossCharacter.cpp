#include "HWBossCharacter.h"

#include "HWBossBrainComponent.h"
#include "HWCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

AHWBossCharacter::AHWBossCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(55.f, 125.f);
	Combat->SetSide(HW::ESide::Boss);
	Brain = CreateDefaultSubobject<UHWBossBrainComponent>(TEXT("Brain"));

	// No controller at all (PLAN §5.1). Set in the constructor so ACharacter::PostInitializeComponents
	// gives the CMC a walking mode — otherwise it stays MOVE_None and root-motion sources are cleared.
	AutoPossessAI = EAutoPossessAI::Disabled;
	bUseControllerRotationYaw = false;
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	CMC->bRunPhysicsWithNoController = true;
	CMC->bOrientRotationToMovement = false;
	CMC->MaxWalkSpeed = HW::Tuning().BossWalkSpeed;
	CMC->RotationRate = FRotator(0.f, 540.f, 0.f);

	// ---- greybox body: dark bronze armour, a pyramid helmet, a long guandao
	BodyColor = FLinearColor(0.16f, 0.09f, 0.04f);
	HeadColor = FLinearColor(0.35f, 0.26f, 0.08f);
	AccentColor = FLinearColor(0.08f, 0.07f, 0.06f);
	BladeColor = FLinearColor(0.45f, 0.45f, 0.5f);
	GhostColor = FLinearColor(0.25f, 0.08f, 0.02f);

	Torso = MakePart(TEXT("Torso"), CubeMesh, VisualRoot, FVector(0.f, 0.f, -10.f), FVector(0.9f, 0.75f, 1.85f));
	Shoulders = MakePart(TEXT("Shoulders"), CubeMesh, VisualRoot, FVector(0.f, 0.f, 70.f), FVector(0.7f, 1.45f, 0.35f));
	Head = MakePart(TEXT("Helmet"), ConeMesh, VisualRoot, FVector(0.f, 0.f, 118.f), FVector(0.75f, 0.75f, 0.7f));
	Accent = MakePart(TEXT("Belt"), CylinderMesh, VisualRoot, FVector(0.f, 0.f, 0.f), FVector(0.95f, 0.85f, 0.18f));
	ArmPivot->SetRelativeLocation(FVector(20.f, 48.f, 65.f));
	BladeLength = 200.f;
	Haft = MakePart(TEXT("Haft"), CylinderMesh, BladeSlide, FVector(45.f, 0.f, 0.f), FVector(0.07f, 0.07f, 1.5f), FRotator(90.f, 0.f, 0.f));
	Blade = MakePart(TEXT("Guandao"), CubeMesh, BladeSlide, FVector(150.f, 0.f, 0.f), FVector(1.1f, 0.06f, 0.26f));
}
