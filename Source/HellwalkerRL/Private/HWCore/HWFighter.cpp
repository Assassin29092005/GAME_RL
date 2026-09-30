// Hellwalker — engine-free core.

#include "HWCore/HWFighter.h"

namespace HW
{
	const char* FighterStateName(EFighterState S)
	{
		switch (S)
		{
		case EFighterState::Idle:        return "Idle";
		case EFighterState::Acting:      return "Acting";
		case EFighterState::Hitstun:     return "Hitstun";
		case EFighterState::Blockstun:   return "Blockstun";
		case EFighterState::Stagger:     return "Stagger";
		case EFighterState::GuardBroken: return "GuardBroken";
		case EFighterState::Dead:        return "Dead";
		default:                         return "?";
		}
	}

	void FFighter::Reset(ESide InSide)
	{
		const FCombatTuning& K = Tuning();
		*this = FFighter{};
		Side = InSide;
		HealthMax = (InSide == ESide::Player) ? K.PlayerHealthMax : K.BossHealthMax;
		ShaChiMax = (InSide == ESide::Player) ? K.PlayerShaChiMax : K.BossShaChiMax;
		Health = HealthMax;
		ShaChi = ShaChiMax;
		FramesSinceShaChiSpent = K.ShaChiRegenDelay;
	}

	int32_t FFighter::FramesUntilActionable() const
	{
		switch (State)
		{
		case EFighterState::Idle:
			return 0;
		case EFighterState::Acting:
		{
			const FMoveData& M = CurrentMove();
			if (bParrySucceeded) { return 0; }
			const int32_t Left = M.ActionableFrames() - T;
			return Left > 0 ? Left : 0;
		}
		case EFighterState::Dead:
			return 1 << 20;
		default:
			return StunLeft > 0 ? StunLeft : 0;
		}
	}

	bool FFighter::IsInStartup() const
	{
		if (State != EFighterState::Acting) { return false; }
		const FMoveData& M = CurrentMove();
		return M.IsAttack() && T < M.Startup;
	}

	bool FFighter::IsHitboxActive() const
	{
		if (State != EFighterState::Acting) { return false; }
		const FMoveData& M = CurrentMove();
		return M.IsAttack() && !bSwingConnected && T >= M.Startup && T < M.Startup + M.Active;
	}

	bool FFighter::IsInRecovery() const
	{
		if (State != EFighterState::Acting) { return false; }
		const FMoveData& M = CurrentMove();
		return T >= M.Startup + M.Active;
	}

	bool FFighter::IsInvulnerable() const
	{
		return State == EFighterState::Acting && CurrentMove().IsInvulnerableAt(T);
	}

	bool FFighter::IsParryLive() const
	{
		if (State != EFighterState::Acting || bParrySucceeded) { return false; }
		const FMoveData& M = CurrentMove();
		return M.Kind == EMoveKind::Parry && T >= M.Startup && T < M.Startup + M.Active;
	}

	bool FFighter::IsCounterLive() const
	{
		if (State != EFighterState::Acting || bParrySucceeded) { return false; }
		const FMoveData& M = CurrentMove();
		return M.Kind == EMoveKind::Counter && T >= M.Startup && T < M.Startup + M.Active;
	}

	bool FFighter::IsGuarding() const
	{
		if (Side == ESide::Player)
		{
			const bool bRaised = bGuardHeld && GuardHeldFrames >= Tuning().GuardRaiseFrames;
			return bRaised && (State == EFighterState::Idle || State == EFighterState::Blockstun);
		}
		if (State == EFighterState::Blockstun) { return true; } // boss only ever enters blockstun from its guard
		if (State != EFighterState::Acting) { return false; }
		const FMoveData& M = CurrentMove();
		return M.Kind == EMoveKind::Guard && T >= M.Startup && T < M.Startup + M.Active;
	}

	bool FFighter::HasHyperArmor() const
	{
		return State == EFighterState::Acting && CurrentMove().HasHyperArmorAt(T);
	}

	bool FFighter::IsFeintFakeFrame() const
	{
		return State == EFighterState::Acting && CurrentMove().FakeImpactFrame == T;
	}

	bool FFighter::Advance(float RegenPerFrame, int32_t RegenDelay)
	{
		GuardHeldFrames = bGuardHeld ? GuardHeldFrames + 1 : 0;
		const bool bStunned = State == EFighterState::Hitstun || State == EFighterState::Blockstun
			|| State == EFighterState::Stagger || State == EFighterState::GuardBroken;
		FramesFree = bStunned ? 0 : FramesFree + 1;
		if (FramesFree >= Tuning().ComboResetFrames) { ComboHitsTaken = 0; }

		if (State != EFighterState::Dead)
		{
			++FramesSinceShaChiSpent;
			if (FramesSinceShaChiSpent >= RegenDelay && ShaChi < ShaChiMax)
			{
				ShaChi += RegenPerFrame;
				if (ShaChi > ShaChiMax) { ShaChi = ShaChiMax; }
			}
		}

		switch (State)
		{
		case EFighterState::Acting:
		{
			const FMoveData& M = CurrentMove();
			++T;
			const bool bParryDone = (M.Kind == EMoveKind::Parry || M.Kind == EMoveKind::Counter) && bParrySucceeded;
			if (T >= M.TotalFrames() || bParryDone)
			{
				State = EFighterState::Idle;
				Move = EMoveId::None;
				T = 0;
				return true;
			}
			return false;
		}
		case EFighterState::Hitstun:
		case EFighterState::Blockstun:
		case EFighterState::Stagger:
		case EFighterState::GuardBroken:
			if (--StunLeft <= 0)
			{
				StunLeft = 0;
				State = EFighterState::Idle;
				if (Side == ESide::Boss && ShaChi <= 0.f)
				{
					ShaChi = ShaChiMax * 0.5f; // boss recovers half its posture after being exposed
				}
				if (Side == ESide::Player && ShaChi <= 0.f)
				{
					ShaChi = ShaChiMax * 0.35f;
				}
			}
			return false;
		default:
			return false;
		}
	}

	void FFighter::BeginMove(EMoveId Id, int32_t DuelFrame, float FacingX, float FacingY)
	{
		const FMoveData& M = HW::Move(Id);
		State = EFighterState::Acting;
		Move = Id;
		T = 0;
		StunLeft = 0;
		CommitFrame = DuelFrame;
		bSwingConnected = false;
		bSwingResolved = false;
		bParrySucceeded = false;
		CommitFacingX = FacingX;
		CommitFacingY = FacingY;
		if (M.ShaChiCost > 0.f) { SpendShaChi(M.ShaChiCost); }
		if (M.IsAttack()) { ++SwingsCommitted; }
	}

	void FFighter::EnterStun(EFighterState StunState, int32_t Frames)
	{
		State = StunState;
		StunLeft = Frames;
		Move = EMoveId::None;
		T = 0;
		ChainDepth = 0;
	}

	void FFighter::SpendShaChi(float Amount)
	{
		ShaChi -= Amount;
		if (ShaChi < 0.f) { ShaChi = 0.f; }
		FramesSinceShaChiSpent = 0;
	}

	void FFighter::ApplyDamage(float Amount)
	{
		Health -= Amount;
		if (Health <= 0.f)
		{
			Health = 0.f;
			State = EFighterState::Dead;
			Move = EMoveId::None;
			T = 0;
			StunLeft = 0;
		}
	}
}
