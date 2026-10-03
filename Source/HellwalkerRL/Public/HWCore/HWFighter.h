// Hellwalker — engine-free core. One fighter's combat state, advanced one frame at a time.
//
// There are no timers anywhere in the combat layer (PLAN §5.2): FDuel steps both fighters in
// lock-step, one integer frame at a time, and every window check is an integer comparison.

#pragma once

#include "HWMoves.h"

namespace HW
{
	enum class EFighterState : uint8_t
	{
		Idle,        // actionable, may move
		Acting,      // running a move (attack / parry / step / guard / counter / locomotion / switch)
		Hitstun,
		Blockstun,
		Stagger,     // uncancellable — parry reward floor (PLAN A3)
		GuardBroken, // player guard broken / boss exposed
		Dead
	};
	const char* FighterStateName(EFighterState S);

	struct FFighter
	{
		ESide Side = ESide::Player;

		// Resources
		float Health = 0.f;
		float HealthMax = 0.f;
		float ShaChi = 0.f;
		float ShaChiMax = 0.f;
		int32_t FramesSinceShaChiSpent = 0;

		// State machine
		EFighterState State = EFighterState::Idle;
		EMoveId  Move = EMoveId::None;      // current move while Acting
		int32_t  T = 0;                      // frame index within the current move (0 = commit frame)
		int32_t  StunLeft = 0;               // frames of stun remaining
		int32_t  CommitFrame = -1;           // duel frame the current move was committed on
		bool     bSwingConnected = false;    // AlreadyHitThisSwing (PLAN §5.4)
		bool     bSwingResolved = false;     // outcome already raised for this swing
		bool     bParrySucceeded = false;    // this parry / counter caught something
		int32_t  ChainDepth = 0;             // player light chain position
		int32_t  ComboHitsTaken = 0;         // hits taken in the current combo (combo decay)
		int32_t  FramesFree = 0;             // frames since this fighter was last stunned
		int32_t  Weapon = 0;                 // player weapon index (C1)
		int32_t  NoAttackUntil = -1;         // duel frame before which no attack may start (the keeper, after a parry)

		// Blocking is a held state for the player (Enhanced Input), a stance move for the boss.
		bool     bGuardHeld = false;
		int32_t  GuardHeldFrames = 0;

		// Facing captured at commit (attacks don't track after commit unless their coverage says so).
		float    CommitFacingX = 1.f;
		float    CommitFacingY = 0.f;

		// Counters for telemetry
		int32_t  SwingsCommitted = 0;

		void Reset(ESide InSide);

		const FMoveData& CurrentMove() const { return HW::Move(Move); }
		bool IsDead() const { return State == EFighterState::Dead; }

		/** Frames until this fighter can commit a new action (PLAN §6 frame advantage source). */
		int32_t FramesUntilActionable() const;
		bool IsActionable() const { return FramesUntilActionable() == 0 && State != EFighterState::Dead; }
		bool CanMove() const { return State == EFighterState::Idle; }

		// Per-frame queries (valid for the current T)
		bool IsInStartup() const;
		bool IsHitboxActive() const;
		bool IsInRecovery() const;
		bool IsInvulnerable() const;
		bool IsParryLive() const;       // player parry window
		bool IsCounterLive() const;     // boss counter stance window
		bool IsGuarding() const;        // blocking right now (held guard or guard stance)
		bool HasHyperArmor() const;
		bool IsFeintFakeFrame() const;  // the fake "impact" frame of a feint
		/** Committed and still before the move's actionable point — cannot be cancelled freely. */
		bool IsCommitted() const { return State == EFighterState::Acting && FramesUntilActionable() > 0; }

		/** Advance one frame. Returns true if the current move finished this frame. */
		bool Advance(float RegenPerFrame, int32_t RegenDelay);

		/** Begin a move. Caller has validated IsActionable() and resources. */
		void BeginMove(EMoveId Id, int32_t DuelFrame, float FacingX, float FacingY);

		void EnterStun(EFighterState StunState, int32_t Frames);
		void SpendShaChi(float Amount);
		void ApplyDamage(float Amount);
	};
}
