// Hellwalker — Unreal's answer to the core's one geometric question: "does this active hitbox touch
// the opponent on this frame?" (HW::IContactOracle).
//
// PLAN §5.4: per-tick sweeps, not overlap events. The attack's volume is built from the move's own
// data in the attacker's frame at commit (range, half-width, sweep side/reach — the same shape the B0
// simulator uses), and swept from where the attacker was last frame to where it is now, against Pawn
// objects only. A connection that starts already overlapping is still a connection.

#pragma once

#include "CoreMinimal.h"
#include "HWCore/HWDuel.h"

class UHWDuelSubsystem;

class FHWContactOracle : public HW::IContactOracle
{
public:
	explicit FHWContactOracle(UHWDuelSubsystem* InOwner) : Owner(InOwner) {}

	virtual bool Contacts(const HW::FDuel& Duel, HW::ESide Attacker) override;

	/** Build the world-space hit volume for a fighter's current attack (also used for debug drawing). */
	static void BuildVolume(const HW::FFighter& F, const FVector& AttackerLocation, FVector& OutCenter, FVector& OutExtent, FQuat& OutRotation);

	void ResetHistory() { PrevLocation[0] = PrevLocation[1] = FVector::ZeroVector; bHasPrev[0] = bHasPrev[1] = false; }

private:
	UHWDuelSubsystem* Owner = nullptr;
	FVector PrevLocation[2] = { FVector::ZeroVector, FVector::ZeroVector };
	bool bHasPrev[2] = { false, false };
};
