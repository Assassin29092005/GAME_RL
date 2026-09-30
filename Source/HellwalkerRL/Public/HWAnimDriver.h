// Hellwalker — C2: from the core's frame state to an FHWAnimFrame.
//
// One driver per fighter, ticked by the character every render frame. Everything it shows is keyed
// to the duel's integer frame counter: a move's clip time comes from HWWarpMoveTime(move, T), a
// reaction's from the frames since the stun began, locomotion's phase from frames stepped. Only the
// cross-fades (a few frames) and the death fall use wall time.

#pragma once

#include "CoreMinimal.h"
#include "HWAnimInstance.h"
#include "HWCore/HWFighter.h"

struct FHWAnimInputs
{
	const HW::FFighter* Fighter = nullptr;
	int32 DuelFrame = 0;
	float FrameAlpha = 0.f;
	bool bDuelRunning = false;        // frames are being stepped (not waiting / ended)
	FVector2D LocalVelocity = FVector2D::ZeroVector;  // x = forward, y = right (cm/s)
	FVector2D OpponentLocal = FVector2D(1.0, 0.0);    // where the opponent is, x = forward, y = right
	float DeltaSeconds = 0.f;
};

class FHWAnimDriver
{
public:
	void Reset();
	void Tick(const UHWAnimSet& Set, const FHWAnimInputs& In, FHWAnimFrame& Out);

private:
	struct FAction
	{
		uint64 Key = 0;               // what is being shown (move + commit frame, reaction + start frame)
		const FHWClip* Clip = nullptr;
		float Time = 0.f;
		bool bUpper = false;
		int32 BlendFrames = 3;
	};

	bool ResolveAction(const UHWAnimSet& Set, const FHWAnimInputs& In, FAction& Out);
	void TrackState(const HW::FFighter& F, int32 DuelFrame);
	void AddLocomotion(const UHWAnimSet& Set, const FHWAnimInputs& In, float LocoDt, FHWAnimFrame& Out);

	// Current and previous action (the previous one is frozen and faded out).
	FAction Current;
	bool bHasCurrent = false;
	FHWAnimSample Previous;
	float PreviousWeight = 0.f;
	float Fade = 1.f;                 // 0..1 fade-in of Current (or fade-out of Previous when no Current)
	float FadeRate = 20.f;

	// State tracking for reactions.
	HW::EFighterState LastState = HW::EFighterState::Idle;
	int32 LastStunLeft = 0;
	int32 StateStartFrame = 0;
	EHWAnimRole HitRole = EHWAnimRole::HitF;
	float DeadSeconds = 0.f;

	// Locomotion.
	float Phase = 0.f;
	float Guard = 0.f;                // smoothed 0..1 guard-held weight
	float IdleSeconds = 0.f;
	int32 LastDuelFrame = -1;
};
