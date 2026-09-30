#include "HWCombatComponent.h"

#include "HWDuelSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"

UHWCombatComponent::UHWCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UHWDuelSubsystem* UHWCombatComponent::GetDuel() const
{
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UHWDuelSubsystem>() : nullptr;
}

const HW::FFighter* UHWCombatComponent::GetFighter() const
{
	const UHWDuelSubsystem* Duel = GetDuel();
	return (Duel != nullptr && Duel->GetEncounter() != nullptr) ? &Duel->GetFighterState(Side) : nullptr;
}

float UHWCombatComponent::GetHealthFraction() const
{
	const HW::FFighter* F = GetFighter();
	return (F != nullptr && F->HealthMax > 0.f) ? F->Health / F->HealthMax : 1.f;
}

float UHWCombatComponent::GetShaChiFraction() const
{
	const HW::FFighter* F = GetFighter();
	return (F != nullptr && F->ShaChiMax > 0.f) ? F->ShaChi / F->ShaChiMax : 1.f;
}

int32 UHWCombatComponent::GetFramesUntilActionable() const
{
	const HW::FFighter* F = GetFighter();
	return F != nullptr ? F->FramesUntilActionable() : 0;
}

void UHWCombatComponent::StartDisplacement(const FVector& WorldDisplacement, float DurationSeconds)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character == nullptr || DurationSeconds <= 0.f) { return; }
	StopDisplacement();

	// PLAN §5.3: never AddActorWorldOffset on a CMC character (velocity never updates, foot-sliding,
	// unswept). A MoveToForce root-motion source moves the capsule through CMC's own swept substeps.
	TSharedPtr<FRootMotionSource_MoveToForce> MoveTo = MakeShared<FRootMotionSource_MoveToForce>();
	MoveTo->InstanceName = FName(TEXT("HWGhoststep"));
	MoveTo->AccumulateMode = ERootMotionAccumulateMode::Override;
	MoveTo->Settings.SetFlag(ERootMotionSourceSettingsFlags::UseSensitiveLiftoffCheck);
	MoveTo->Priority = 1000;
	MoveTo->StartLocation = Character->GetActorLocation();
	MoveTo->TargetLocation = MoveTo->StartLocation + FVector(WorldDisplacement.X, WorldDisplacement.Y, 0.f);
	MoveTo->Duration = DurationSeconds;
	MoveTo->bRestrictSpeedToExpected = false;
	MoveTo->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	MoveTo->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	RootMotionId = Character->GetCharacterMovement()->ApplyRootMotionSource(MoveTo);

	const UHWDuelSubsystem* Duel = GetDuel();
	DisplacementCommitFrame = Duel != nullptr ? Duel->GetDuelFrame() : -1;
}

void UHWCombatComponent::StopDisplacement()
{
	if (RootMotionId == 0) { return; }
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetCharacterMovement()->RemoveRootMotionSourceByID(RootMotionId);
	}
	RootMotionId = 0;
	DisplacementCommitFrame = -1;
}

void UHWCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (RootMotionId == 0) { return; }
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character == nullptr || !Character->GetCharacterMovement()->GetRootMotionSourceByID(RootMotionId).IsValid())
	{
		RootMotionId = 0; // finished on its own
		return;
	}
	// Every interrupt path removes the source: if the fighter is no longer stepping, stop moving it.
	const HW::FFighter* F = GetFighter();
	const bool bStillStepping = F != nullptr && F->State == HW::EFighterState::Acting
		&& F->CurrentMove().Kind == HW::EMoveKind::Step && F->CommitFrame == DisplacementCommitFrame;
	if (!bStillStepping)
	{
		StopDisplacement();
	}
}
