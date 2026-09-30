#include "HWBossBrainComponent.h"

#include "HWDuelSubsystem.h"
#include "HWTypesUE.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UHWBossBrainComponent::UHWBossBrainComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics; // movement input before CMC consumes it
}

UHWDuelSubsystem* UHWBossBrainComponent::GetDuel() const
{
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UHWDuelSubsystem>() : nullptr;
}

const HW::FBossBrain* UHWBossBrainComponent::GetBrain() const
{
	const UHWDuelSubsystem* Duel = GetDuel();
	return (Duel != nullptr && Duel->GetEncounter() != nullptr) ? &Duel->GetEncounter()->Brain : nullptr;
}

bool UHWBossBrainComponent::IsAdaptationEnabled() const
{
	const HW::FBossBrain* B = GetBrain();
	return B != nullptr && B->IsAdaptive();
}

FString UHWBossBrainComponent::GetLastReason() const
{
	const HW::FBossBrain* B = GetBrain();
	return B != nullptr ? ToFString(B->LastDecision().Reason) : FString();
}

float UHWBossBrainComponent::GetReadBits() const
{
	const HW::FBossBrain* B = GetBrain();
	return B != nullptr ? B->LastDecision().ReadBits : 0.f;
}

void UHWBossBrainComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Boss = Cast<ACharacter>(GetOwner());
	UHWDuelSubsystem* Duel = GetDuel();
	if (Boss == nullptr || Duel == nullptr || Duel->GetEncounter() == nullptr) { return; }
	if (Duel->GetState() != EHWEncounterState::Running) { return; }

	const HW::FFighter& F = Duel->GetFighterState(HW::ESide::Boss);
	const FVector ToPlayer = Duel->TargetDirection(HW::ESide::Boss);

	// Locomotion is code-driven from the core's current move (no AIController, no MoveTo — PLAN §5.1).
	if (F.State == HW::EFighterState::Acting && F.CurrentMove().Kind == HW::EMoveKind::Locomotion)
	{
		const HW::FMoveData& M = F.CurrentMove();
		const float Seconds = static_cast<float>(FMath::Max(M.Active, 1)) / static_cast<float>(HW::FramesPerSecond);
		Boss->GetCharacterMovement()->MaxWalkSpeed = M.Distance / Seconds;
		const bool bForward = M.Dir == HW::EDir::Forward;
		if (!bForward || Duel->FighterDistance() > 150.f)
		{
			Boss->AddMovementInput(bForward ? ToPlayer : -ToPlayer, 1.f);
		}
	}

	// Facing: track the player when free; an attack keeps the facing it committed with (it does not
	// track after commit — the dodge must be able to beat it).
	const bool bAttacking = F.State == HW::EFighterState::Acting && F.CurrentMove().IsAttack();
	const bool bStunned = F.State == HW::EFighterState::Hitstun || F.State == HW::EFighterState::Stagger
		|| F.State == HW::EFighterState::GuardBroken || F.State == HW::EFighterState::Dead;
	if (!bAttacking && !bStunned && !ToPlayer.IsNearlyZero())
	{
		const FRotator Want(0.f, ToPlayer.Rotation().Yaw, 0.f);
		Boss->SetActorRotation(FMath::RInterpTo(Boss->GetActorRotation(), Want, DeltaTime, 7.f));
	}
}
