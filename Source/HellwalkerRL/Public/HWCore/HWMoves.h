// Hellwalker — engine-free core. Move table: every action either fighter can commit to.
//
// Integer frame counts at 60 fps (PLAN §6). The Unreal side exposes these through
// UCombatAnimConfig so designers can edit them without a rebuild; the defaults live here so the
// simulator and the game start from the same numbers.

#pragma once

#include "HWTypes.h"

namespace HW
{
	struct FMoveData
	{
		const char* Name = "None";
		EMoveId     Id = EMoveId::None;
		EMoveKind   Kind = EMoveKind::Attack;
		ESym        Symbol = ESym::Neutral;     // symbol emitted to the playstyle model on commit
		ESlotType   Slot = ESlotType::Attack;   // boss only: which script slot type it can fill
		ESide       Owner = ESide::Player;
		int32_t     Weapon = 0;                 // player attacks: 0 Twin Blades, 1 Glaive

		// Timeline (frames). Attack: Startup / Active / Recovery. Step: Active = step frames.
		// Guard / Counter / Locomotion: Active = stance length.
		int32_t Startup = 0;
		int32_t Active = 0;
		int32_t Recovery = 0;
		int32_t CancelFrame = 0;                // 0 = not cancellable before the end

		// Hitbox
		float      Damage = 0.f;
		float      ShaChiDrainOnBlock = 0.f;     // drained from the blocker's sha-chi
		float      Range = 0.f;                  // cm, from attacker's centre along facing
		float      HalfWidth = 0.f;              // cm, lateral half-width of a straight hitbox
		float      SweepReach = 0.f;             // cm, how far a sweep tracks to its side
		ECoverage  Coverage = ECoverage::Straight;
		bool       bUnblockable = false;
		bool       bUnparryable = false;
		int32_t    HyperArmorFrom = -1;          // frame index from which hits do not stun; -1 none
		int32_t    HyperArmorTo = -1;            // inclusive; -1 = until end of active
		int32_t    FakeImpactFrame = -1;         // feints: frame the fake swing "arrives" (no hitbox)

		// Displacement (steps / evades / locomotion)
		EDir    Dir = EDir::None;
		float   Distance = 0.f;                  // cm over Active frames
		int32_t IFrameStart = -1;                // inclusive frame indices
		int32_t IFrameEnd = -1;
		float   ShaChiCost = 0.f;

		/** Frames from commit until the fighter may act again: min(S+A+R, Cancel) (PLAN §6). */
		int32_t ActionableFrames() const
		{
			const int32_t Total = Startup + Active + Recovery;
			return (CancelFrame > 0 && CancelFrame < Total) ? CancelFrame : Total;
		}
		int32_t TotalFrames() const { return Startup + Active + Recovery; }
		bool IsAttack() const { return Kind == EMoveKind::Attack; }
		bool IsDefensive() const { return Kind == EMoveKind::Guard || Kind == EMoveKind::Counter || Kind == EMoveKind::Step || Kind == EMoveKind::Parry; }
		/** First frame a hitbox can connect. */
		int32_t ImpactFrame() const { return Startup; }
		/**
		 * The impact the wind-up SUGGESTS. A feint's fake swing, a delayed heavy's held wind-up (it reads
		 * like the ordinary heavy, then holds). Players time to this; the baits live in the difference.
		 */
		int32_t PerceivedImpact() const { return FakeImpactFrame >= 0 ? FakeImpactFrame : Startup; }
		bool HasHyperArmorAt(int32_t T) const
		{
			if (HyperArmorFrom < 0) { return false; }
			const int32_t To = HyperArmorTo >= 0 ? HyperArmorTo : (Startup + Active - 1);
			return T >= HyperArmorFrom && T <= To;
		}
		bool IsInvulnerableAt(int32_t T) const { return IFrameStart >= 0 && T >= IFrameStart && T <= IFrameEnd; }
	};

	/** Read-only view of the move table (defaults, possibly overridden by config at start-up). */
	const FMoveData& Move(EMoveId Id);

	/** Mutable access for config overrides (UCombatAnimConfig, simulator tuning sweeps). */
	FMoveData& MutableMove(EMoveId Id);

	/** Restore every move to its compiled default. */
	void ResetMoveTable();

	/** Player light-chain successor for a weapon (Light1 -> Light2 -> Light3 -> Light1). */
	EMoveId NextLight(int32_t Weapon, int32_t ChainDepth);
	EMoveId HeavyFor(int32_t Weapon);
	EMoveId StepFor(EDir Dir);

	/** All boss moves that may fill a slot of the given type (PLAN §2.4 step 4, before the DEFEND upgrade). */
	int32_t BossMovesForSlot(ESlotType Slot, EMoveId* OutMoves, int32_t MaxOut);
}
