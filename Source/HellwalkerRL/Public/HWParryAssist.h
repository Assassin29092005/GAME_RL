// HellwalkerRL — the parry assist: when a parry pressed NOW would catch the keeper's swing.
//
// Pure helpers (no UObjects, no world), unit-tested (Project.HellwalkerRL.Assist.*). UHWDuelSubsystem evaluates the cue
// after each tick's frames; AHWHUD draws it — a RED ring round the keeper's weapon hand(s), lit exactly while a press made
// now parries, grey while the window is open but the player cannot get a parry out in time, and on Easy a faint ring from
// the commit (no timing) — and the duel slows itself while the ring is lit (the frame cursor and both fighters'
// CustomTimeDilation; never the global time dilation). Presentation only: HWCore and the rules are untouched.
//
// Exactness. A press is stamped with the duel frame it is made on and applied before the next step
// (UHWDuelSubsystem::QueuePlayerInput / ApplyDueInput). A player who cannot act yet keeps the press buffered for
// ParryBufferFrames, so it commits on step n = FramesUntilActionable() when n <= MaxPressDelay. The parry is live on its own
// frames [Startup, Startup + Active) and the keeper's swing first connects on its frame Startup. With D = Impact - Boss.T
// between steps, the press parries iff
//     PParry.Startup <= D - n <= PParry.Startup + PParry.Active - 1
// (HWCoreTests' A3 parry window, shifted by the player's delay). No countdown before the window opens: the ring lights on
// its first frame. Unparryable moves (the killer thrust, the grab), resolved swings and swings that cannot reach show nothing.
//
// The impact the ring times is the one the wind-up SHOWS (FMoveData::PerceivedImpact): for a feint, its fake swing; for the
// delayed heavy, the cleave it imitates — until that moment passes, then the real impact. So the ring is exact for every
// honest swing and a bait is still a bait: a player who parries every ring is a habit the keeper learns and fakes (the
// Adaptive AI arc, README). Timing the real impact instead would hand the player every feint and leave the keeper no answer
// to a parry habit but its grab and killer thrust.

#pragma once

#include "CoreMinimal.h"
#include "HWCore/HWFighter.h"
#include "HWSettings.h"

/** The assist's view of the keeper's swing, between two duel steps. */
struct FHWParryCue
{
	/** A parryable keeper swing that can reach is on its way: committed, unresolved, its impact still ahead. */
	bool bActive = false;
	/** The ring shows: a press now lands in the window (bCanAct), or the window is open but this player cannot make it. */
	bool bInWindow = false;
	/** Active, and the window has not opened yet (Easy's glow, from the commit). */
	bool bIncoming = false;
	/** The player gets a parry out before the impact: actionable now, or within the parry buffer. */
	bool bCanAct = false;
	/** D: duel steps until the swing's first active frame (the real impact). */
	int32 FramesToImpact = 0;
	/** The values of D for which a press now parries (inclusive; already shifted by the player's delay when bCanAct). */
	int32 WindowFirst = 0;
	int32 WindowLast = 0;
	/** 0 as the window opens .. 1 as it closes (continuous between frames). */
	float Progress = 0.f;

	/** Red: a press made now parries. */
	bool IsLit() const { return bInWindow && bCanAct; }
};

namespace HWParryAssist
{
	/** How long the duel keeps a parry press that could not be applied yet (UHWDuelSubsystem's input buffer). */
	inline constexpr int32 ParryBufferFrames = 3;
	/** The latest step a buffered press still commits on: it is tried once more on the frame it expires. */
	inline constexpr int32 MaxPressDelay = ParryBufferFrames + 1;
	/** The player's capsule radius (the contact volume must overlap it) and a small allowance for a step in. */
	inline constexpr float PlayerRadiusCm = 38.f;
	inline constexpr float ReachMarginCm = 25.f;

	/**
	 * Can this swing connect with a player at this offset from the keeper? AlongCm / LateralCm = the player's position
	 * along the keeper's COMMITTED facing and to its right (Unreal's +Y), as FHWContactOracle::BuildVolume shapes the hit:
	 * along 20 .. Range - 30, sideways the half-width, or out to SweepReach on the side a sweep tracks — each grown by the
	 * capsule and the margin. A sweep's reach is sideways only, never straight ahead.
	 */
	HELLWALKERRL_API bool CanReach(const HW::FMoveData& M, float AlongCm, float LateralCm);

	/**
	 * The cue for the state between steps. FrameAlpha = the fraction of the next frame already elapsed (smooths Progress;
	 * ignored in hit-stop, when no frame elapses); AlongCm / LateralCm = the player's offset in the keeper's committed frame
	 * (CanReach); bOtherPressPending = an attack, switch or step already waits in the input buffer and will commit before a
	 * parry pressed now (the duel applies presses in order), so the ring is grey: the parry would not come out.
	 */
	HELLWALKERRL_API FHWParryCue Evaluate(const HW::FFighter& Boss, const HW::FFighter& Player, float FrameAlpha, float AlongCm, float LateralCm,
		bool bHitstop, bool bOtherPressPending = false);

	/** The duel's time scale wanted now: the fight's slow scale while the ring is lit and the player can act, else 1. */
	HELLWALKERRL_API float TargetTimeScale(const FHWParryCue& Cue, const FHWAssistParams& Assist);
}
