// Hellwalker — engine-free core.
//
// Everything under HWCore/ is plain C++20 with no Unreal dependency. The same files are compiled
// by the Unreal module (Source/Hellwalker) and by the headless thesis simulator (Sim/), so the
// simulator's verdict is a verdict on the shipped rules, not on a re-implementation of them.
//
// Rules for this folder: no UE headers, no RTTI, no exceptions, no identifiers that collide with
// UE or Windows macros (check, verify, ensure, IN, OUT, IGNORE, DrawText, min, max, small...).

#pragma once

#include <cstdint>

namespace HW
{
	/** Simulation rate. All frame data is in frames at this rate (PLAN §5.2, §6). */
	inline constexpr int32_t FramesPerSecond = 60;

	/** Interleaved symbol alphabet (PLAN §2.2). Player 0-11, boss 12-19. */
	enum class ESym : uint8_t
	{
		// Player
		Neutral = 0, Advance, Retreat, Light, Heavy, Block, Parry, StepF, StepB, StepL, StepR, Switch,
		// Boss
		BFast, BHeavy, BFeint, BKiller, BGuard, BEvade, BApproach, BRetreat,
		Count
	};
	inline constexpr int32_t NumSymbols = static_cast<int32_t>(ESym::Count); // 20
	inline constexpr int32_t NumPlayerSymbols = 12;

	inline constexpr bool IsPlayerSym(ESym S) { return static_cast<int32_t>(S) < NumPlayerSymbols; }
	inline constexpr int32_t SymIndex(ESym S) { return static_cast<int32_t>(S); }
	const char* SymName(ESym S);

	/** Outcome of one swing, raised exactly once per swing (PLAN §5.4). */
	enum class EHitOutcome : uint8_t { None, Hit, Whiff, Blocked, Parried };
	const char* OutcomeName(EHitOutcome O);

	/** Script slot type (PLAN §2.4). */
	enum class ESlotType : uint8_t { Attack, Defend, Reposition };
	const char* SlotTypeName(ESlotType T);

	/** Which fighter. Index into FDuel::Fighters. */
	enum class ESide : uint8_t { Player = 0, Boss = 1 };
	inline constexpr int32_t SideIndex(ESide S) { return static_cast<int32_t>(S); }
	inline constexpr ESide Opponent(ESide S) { return S == ESide::Player ? ESide::Boss : ESide::Player; }

	/** Direction in TARGET space — relative to the line from the mover to its lock-on target (PLAN §2.1). */
	enum class EDir : uint8_t { None, Forward, Back, Left, Right };

	/** Hitbox coverage of an attack, in the attacker's frame at commit. Left/Right are the DEFENDER's left/right. */
	enum class ECoverage : uint8_t
	{
		Straight,    // a line in front of the attacker; lateral steps leave it
		SweepLeft,   // tracks toward the defender's left: catches StepL
		SweepRight,  // tracks toward the defender's right: catches StepR
		Wide,        // both sides (AoE), short
		Grab         // short range, unblockable, unparryable
	};

	/** Broad kind of a move — decides which state machine branch runs it. */
	enum class EMoveKind : uint8_t
	{
		Attack,      // startup / active / recovery with a hitbox
		Parry,       // parry attempt: live window then whiff recovery
		Step,        // ghoststep (player) or evade (boss): displacement with i-frames
		Guard,       // guard stance of fixed length (boss) — player block is a held state, not a move
		Counter,     // boss counter stance: a parry window that auto-punishes
		Locomotion,  // approach / retreat for a fixed number of frames
		Switch       // weapon switch (player)
	};

	/** Every move in the game, player and boss. Indices into the move table (HWMoves.cpp). */
	enum class EMoveId : uint8_t
	{
		None = 0,
		// ---- Player: Twin Blades (weapon 0)
		PLight1, PLight2, PLight3, PHeavy,
		// ---- Player: Glaive (weapon 1) — PLAN C1, genuinely different frame data
		GLight1, GLight2, GHeavy,
		// ---- Player: defence / movement
		PParry, PStepF, PStepB, PStepL, PStepR, PSwitch,
		// ---- Boss attacks
		BFastSlash, BSweepLeft, BSweepLeftLate, BSweepRight, BSweepRightLate,
		BHeavyCleave, BDelayedHeavy, BHeavySweepLeft, BHeavySweepRight,
		BFeintEarly, BFeintMid, BFeintLate,
		BKillerThrust, BGrab,
		// ---- Boss defence / evasion / movement
		BGuard, BCounterStance, BBackstep, BSideStepL, BSideStepR,
		BApproach, BDashIn, BRetreat,
		Count
	};
	inline constexpr int32_t NumMoves = static_cast<int32_t>(EMoveId::Count);

	/** Placeholder combat tuning (PLAN §6). Wrong on purpose; every done-test reads these. */
	struct FCombatTuning
	{
		// Parry
		int32_t ParryWindowFrames = 8;          // press in [Impact-8, Impact) parries
		int32_t ParryWhiffRecovery = 16;        // after the live window, if nothing arrived
		float   ParryRewardShaChi = 15.f;       // always paid to the parried attacker's sha-chi
		int32_t ParryRewardStagger = 20;        // uncancellable
		// Stun
		int32_t Hitstun = 18;                   // player, when hit (§6)
		int32_t BossHitstun = 22;               // boss, when hit: > the player's 20-frame light-chain gap, so
		                                        // a clean light string combos instead of trading (B0 finding)
		int32_t ComboHitCap = 3;                // hits in one unbroken stun before stun decays…
		int32_t ComboDecayStun = 6;             // …to this: nobody can be juggled forever (B0 finding)
		int32_t ComboResetFrames = 20;          // the combo count only resets after this long free of stun
		int32_t Blockstun = 11;
		int32_t GuardBreakStun = 50;            // player guard broken
		int32_t BossExposedStun = 90;           // boss sha-chi broken
		float   ExposedDamageMult = 1.5f;
		// Ghoststep (player) — frames 3-18 of 24 are invulnerable, then 10 recovery
		int32_t StepFrames = 24;
		int32_t StepIFrameStart = 3;
		int32_t StepIFrameEnd = 18;             // inclusive
		int32_t StepRecovery = 10;
		float   StepShaChiCost = 12.f;
		float   StepDistance = 260.f;           // cm
		// Sha-chi
		float   PlayerShaChiMax = 100.f;
		float   BossShaChiMax = 80.f;           // boss guard pool
		float   ShaChiRegenPerSec = 8.f;
		int32_t ShaChiRegenDelay = 90;          // 1.5 s
		// Blocking
		float   BlockChip = 0.2f;               // fraction of damage taken through guard
		int32_t GuardRaiseFrames = 2;
		// Health (identical across control / adaptive — PLAN B4)
		float   PlayerHealthMax = 360.f;
		float   BossHealthMax = 1100.f;
		// Locomotion (cm / s)
		float   PlayerWalkSpeed = 420.f;
		float   BossWalkSpeed = 330.f;
		float   BossDashSpeed = 900.f;
	};

	/** The one tuning instance. Mutable so the simulator / UE config can override it at start-up. */
	FCombatTuning& Tuning();
}
