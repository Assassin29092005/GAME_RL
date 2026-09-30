#include "HWAnimDriver.h"

namespace
{
	enum class EKey : uint64 { Move = 1, Hit, BlockHit, Stagger, Exposed, Dead };

	uint64 MakeKey(EKey Kind, uint64 A, int32 Frame)
	{
		return (static_cast<uint64>(Kind) << 56) | ((A & 0xFFFF) << 32) | static_cast<uint32>(Frame);
	}

	const FHWClip* FirstValid(const UHWAnimSet& Set, std::initializer_list<EHWAnimRole> Roles)
	{
		for (EHWAnimRole R : Roles)
		{
			const FHWClip& C = Set.Role(R);
			if (C.IsValid()) { return &C; }
		}
		return nullptr;
	}

	float ClipAt(const FHWClip& C, float Seconds)
	{
		return FMath::Min(C.Start + FMath::Max(Seconds, 0.f), C.End);
	}

	float LoopAt(const FHWClip& C, float Seconds)
	{
		return C.Start + FMath::Fmod(FMath::Max(Seconds, 0.f), FMath::Max(C.Duration(), KINDA_SMALL_NUMBER));
	}

	/** The side the blow came from, in the victim's frame. */
	EHWAnimRole HitRoleFrom(const FVector2D& OpponentLocal)
	{
		if (FMath::Abs(OpponentLocal.X) >= FMath::Abs(OpponentLocal.Y))
		{
			return OpponentLocal.X >= 0.0 ? EHWAnimRole::HitF : EHWAnimRole::HitB;
		}
		return OpponentLocal.Y >= 0.0 ? EHWAnimRole::HitR : EHWAnimRole::HitL;
	}
}

void FHWAnimDriver::Reset()
{
	*this = FHWAnimDriver();
}

void FHWAnimDriver::TrackState(const HW::FFighter& F, int32 DuelFrame)
{
	const bool bStun = F.State == HW::EFighterState::Hitstun || F.State == HW::EFighterState::Blockstun
		|| F.State == HW::EFighterState::Stagger || F.State == HW::EFighterState::GuardBroken;
	// A new reaction starts on a state change, or when a stun is refreshed by another hit.
	if (F.State != LastState || (bStun && F.StunLeft > LastStunLeft))
	{
		StateStartFrame = DuelFrame;
	}
	LastState = F.State;
	LastStunLeft = F.StunLeft;
}

bool FHWAnimDriver::ResolveAction(const UHWAnimSet& Set, const FHWAnimInputs& In, FAction& Out)
{
	const HW::FFighter& F = *In.Fighter;
	const float Seconds = (static_cast<float>(In.DuelFrame - StateStartFrame) + In.FrameAlpha) / static_cast<float>(HW::FramesPerSecond);

	switch (F.State)
	{
	case HW::EFighterState::Acting:
	{
		const HW::FMoveData& M = F.CurrentMove();
		if (M.Kind == HW::EMoveKind::Locomotion) { return false; } // the boss walking: locomotion shows it
		const FHWClip* C = &Set.Move(F.Move);
		if (!C->IsValid())
		{
			if (M.Kind == HW::EMoveKind::Guard) { C = FirstValid(Set, { EHWAnimRole::GuardStance, EHWAnimRole::GuardIdle }); }
			else if (M.Kind == HW::EMoveKind::Counter) { C = FirstValid(Set, { EHWAnimRole::CounterStance, EHWAnimRole::GuardStance }); }
			else { C = nullptr; }
			if (C == nullptr) { return false; }
		}
		Out.Key = MakeKey(EKey::Move, static_cast<uint64>(F.Move), F.CommitFrame);
		Out.Clip = C;
		Out.Time = HWWarpMoveTime(M, *C, static_cast<float>(F.T) + In.FrameAlpha);
		Out.BlendFrames = M.IsAttack() ? 3 : 4;
		return true;
	}
	case HW::EFighterState::Hitstun:
	{
		const FHWClip* C = FirstValid(Set, { HitRole, EHWAnimRole::HitF });
		if (C == nullptr) { return false; }
		Out.Key = MakeKey(EKey::Hit, 0, StateStartFrame);
		Out.Clip = C;
		Out.Time = ClipAt(*C, Seconds);
		Out.BlendFrames = 2;
		return true;
	}
	case HW::EFighterState::Blockstun:
	{
		const FHWClip* C = FirstValid(Set, { EHWAnimRole::BlockHit, EHWAnimRole::GuardStance, EHWAnimRole::HitF });
		if (C == nullptr) { return false; }
		Out.Key = MakeKey(EKey::BlockHit, 0, StateStartFrame);
		Out.Clip = C;
		Out.Time = ClipAt(*C, Seconds);
		Out.BlendFrames = 2;
		return true;
	}
	case HW::EFighterState::Stagger:
	{
		const FHWClip* C = FirstValid(Set, { EHWAnimRole::Stagger, EHWAnimRole::HitF });
		if (C == nullptr) { return false; }
		Out.Key = MakeKey(EKey::Stagger, 0, StateStartFrame);
		Out.Clip = C;
		Out.Time = ClipAt(*C, Seconds);
		Out.BlendFrames = 2;
		return true;
	}
	case HW::EFighterState::GuardBroken:
	{
		// Boss exposed: stun start -> loop -> end, the end timed to finish with the stun.
		const FHWClip& S = Set.Role(EHWAnimRole::StunStart);
		const FHWClip& L = Set.Role(EHWAnimRole::StunLoop);
		const FHWClip& E = Set.Role(EHWAnimRole::StunEnd);
		const float Left = static_cast<float>(F.StunLeft) / static_cast<float>(HW::FramesPerSecond);
		if (S.IsValid())
		{
			if (Seconds < S.Duration() || !L.IsValid())
			{
				Out.Key = MakeKey(EKey::Exposed, 0, StateStartFrame);
				Out.Clip = &S;
				Out.Time = ClipAt(S, Seconds);
			}
			else if (E.IsValid() && Left < E.Duration())
			{
				Out.Key = MakeKey(EKey::Exposed, 2, StateStartFrame);
				Out.Clip = &E;
				Out.Time = FMath::Max(E.End - Left, E.Start);
			}
			else
			{
				Out.Key = MakeKey(EKey::Exposed, 1, StateStartFrame);
				Out.Clip = &L;
				Out.Time = LoopAt(L, Seconds - S.Duration());
			}
			Out.BlendFrames = 4;
			return true;
		}
		const FHWClip* C = FirstValid(Set, { EHWAnimRole::GuardBreak, EHWAnimRole::Stagger, EHWAnimRole::HitF });
		if (C == nullptr) { return false; }
		Out.Key = MakeKey(EKey::Exposed, 0, StateStartFrame);
		Out.Clip = C;
		Out.Time = ClipAt(*C, Seconds);
		Out.BlendFrames = 2;
		return true;
	}
	case HW::EFighterState::Dead:
	{
		const FHWClip* C = FirstValid(Set, { EHWAnimRole::Death });
		if (C == nullptr) { return false; }
		Out.Key = MakeKey(EKey::Dead, 0, 0);
		Out.Clip = C;
		Out.Time = ClipAt(*C, DeadSeconds);
		Out.BlendFrames = 4;
		return true;
	}
	default:
		return false;
	}
}

void FHWAnimDriver::AddLocomotion(const UHWAnimSet& Set, const FHWAnimInputs& In, float LocoDt, FHWAnimFrame& Out)
{
	const HW::FFighter& F = *In.Fighter;
	const float Speed = static_cast<float>(In.LocalVelocity.Size());

	const float GuardTarget = (F.bGuardHeld && F.State == HW::EFighterState::Idle && Set.Role(EHWAnimRole::GuardIdle).IsValid()) ? 1.f : 0.f;
	Guard = FMath::FInterpConstantTo(Guard, GuardTarget, In.DeltaSeconds, 12.f);

	float Dir[4] = { 0.f, 0.f, 0.f, 0.f }; // F B L R
	if (Speed > 1.f)
	{
		const float C = static_cast<float>(In.LocalVelocity.X) / Speed;
		const float S = static_cast<float>(In.LocalVelocity.Y) / Speed;
		Dir[0] = FMath::Max(C, 0.f);
		Dir[1] = FMath::Max(-C, 0.f);
		Dir[2] = FMath::Max(-S, 0.f);
		Dir[3] = FMath::Max(S, 0.f);
		const float Sum = Dir[0] + Dir[1] + Dir[2] + Dir[3];
		for (float& D : Dir) { D /= FMath::Max(Sum, KINDA_SMALL_NUMBER); }
	}
	const float MoveW = FMath::Clamp(Speed / 150.f, 0.f, 1.f);

	static const EHWAnimRole Normal[5] = { EHWAnimRole::Idle, EHWAnimRole::MoveF, EHWAnimRole::MoveB, EHWAnimRole::MoveL, EHWAnimRole::MoveR };
	static const EHWAnimRole Guarded[5] = { EHWAnimRole::GuardIdle, EHWAnimRole::GuardMoveF, EHWAnimRole::GuardMoveB, EHWAnimRole::GuardMoveL, EHWAnimRole::GuardMoveR };
	auto Pick = [&Set](const EHWAnimRole* Roles, int32 I) -> const FHWClip*
	{
		const FHWClip& C = Set.Role(Roles[I]);
		if (C.IsValid()) { return &C; }
		const FHWClip& N = Set.Role(Normal[I]);
		return N.IsValid() ? &N : nullptr;
	};

	// One shared phase for every directional loop: feet stay in step across the blend.
	float Stride = 0.f;
	float StrideW = 0.f;
	for (int32 D = 0; D < 4; ++D)
	{
		if (const FHWClip* C = Pick(Normal, D + 1))
		{
			const float Sp = C->RootSpeed > 1.f ? C->RootSpeed : 300.f;
			Stride += Dir[D] * Sp * C->Duration();
			StrideW += Dir[D];
		}
	}
	if (StrideW > KINDA_SMALL_NUMBER && Speed > 1.f)
	{
		Stride /= StrideW;
		Phase = FMath::Fmod(Phase + LocoDt * Speed / FMath::Max(Stride, 1.f), 1.f);
	}
	IdleSeconds += LocoDt;

	const float SetW[2] = { 1.f - Guard, Guard };
	const EHWAnimRole* Sets[2] = { Normal, Guarded };
	for (int32 K = 0; K < 2; ++K)
	{
		if (SetW[K] <= KINDA_SMALL_NUMBER) { continue; }
		if (const FHWClip* Idle = Pick(Sets[K], 0))
		{
			Out.Base.Add(FHWAnimSample::Of(*Idle, LoopAt(*Idle, IdleSeconds), SetW[K] * (1.f - MoveW)));
		}
		for (int32 D = 0; D < 4; ++D)
		{
			const float W = SetW[K] * MoveW * Dir[D];
			if (W <= KINDA_SMALL_NUMBER) { continue; }
			if (const FHWClip* C = Pick(Sets[K], D + 1))
			{
				Out.Base.Add(FHWAnimSample::Of(*C, C->Start + Phase * C->Duration(), W));
			}
		}
	}
}

void FHWAnimDriver::Tick(const UHWAnimSet& Set, const FHWAnimInputs& In, FHWAnimFrame& Out)
{
	Out.Base.Reset();
	Out.Layers.Reset();
	if (In.Fighter == nullptr) { return; }
	const HW::FFighter& F = *In.Fighter;

	// Frames actually stepped since the last render frame: locomotion freezes in hitstop like everything else.
	float LocoDt = In.DeltaSeconds;
	if (In.bDuelRunning && LastDuelFrame >= 0)
	{
		LocoDt = static_cast<float>(FMath::Clamp(In.DuelFrame - LastDuelFrame, 0, 4)) / static_cast<float>(HW::FramesPerSecond);
	}
	LastDuelFrame = In.DuelFrame;

	const HW::EFighterState Before = LastState;
	TrackState(F, In.DuelFrame);
	if (F.State == HW::EFighterState::Hitstun && (Before != HW::EFighterState::Hitstun || StateStartFrame == In.DuelFrame))
	{
		HitRole = HitRoleFrom(In.OpponentLocal);
	}
	DeadSeconds = F.State == HW::EFighterState::Dead ? DeadSeconds + In.DeltaSeconds : 0.f;

	AddLocomotion(Set, In, LocoDt, Out);

	FAction A;
	const bool bHas = ResolveAction(Set, In, A);
	auto Shown = [this]() { return bHasCurrent ? 1.f - (1.f - PreviousWeight) * (1.f - Fade) : PreviousWeight * (1.f - Fade); };
	if (bHas)
	{
		if (!bHasCurrent || A.Key != Current.Key)
		{
			// Freeze whatever is showing now and fade the new action in over it.
			const float Weight = Shown();
			if (bHasCurrent) { Previous = FHWAnimSample::Of(*Current.Clip, Current.Time, 1.f, Current.bUpper); }
			PreviousWeight = Weight;
			Current = A;
			bHasCurrent = true;
			Fade = 0.f;
			FadeRate = static_cast<float>(HW::FramesPerSecond) / static_cast<float>(FMath::Max(A.BlendFrames, 1));
		}
		else
		{
			Current.Time = A.Time;
			Current.Clip = A.Clip;
		}
	}
	else if (bHasCurrent)
	{
		// Back to locomotion: the last action pose fades out.
		PreviousWeight = Shown();
		Previous = FHWAnimSample::Of(*Current.Clip, Current.Time, 1.f, Current.bUpper);
		bHasCurrent = false;
		Fade = 0.f;
		FadeRate = static_cast<float>(HW::FramesPerSecond) / 6.f;
	}
	Fade = FMath::Min(1.f, Fade + In.DeltaSeconds * FadeRate);

	if (bHasCurrent)
	{
		// A full pose covers the previous one once faded in; an additive one (a flinch) does not, so the
		// previous pose has to fade out underneath it instead of popping.
		if (Current.Clip->bAdditive) { PreviousWeight = FMath::Max(0.f, PreviousWeight - In.DeltaSeconds * 10.f); }
		else if (Fade >= 1.f) { PreviousWeight = 0.f; }
		if (PreviousWeight > 0.f && Previous.Anim != nullptr)
		{
			FHWAnimSample P = Previous;
			P.Weight = PreviousWeight;
			Out.Layers.Add(P);
		}
		Out.Layers.Add(FHWAnimSample::Of(*Current.Clip, Current.Time, Fade, Current.bUpper));
	}
	else if (PreviousWeight > 0.f && Previous.Anim != nullptr)
	{
		const float W = PreviousWeight * (1.f - Fade);
		if (W > KINDA_SMALL_NUMBER)
		{
			FHWAnimSample P = Previous;
			P.Weight = W;
			Out.Layers.Add(P);
		}
		else
		{
			PreviousWeight = 0.f;
		}
	}
}
