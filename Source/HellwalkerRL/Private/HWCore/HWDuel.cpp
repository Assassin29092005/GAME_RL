// Hellwalker — engine-free core.

#include "HWCore/HWDuel.h"

namespace HW
{
	const char* DefenderPhaseName(EDefenderPhase P)
	{
		switch (P)
		{
		case EDefenderPhase::Idle:        return "idle";
		case EDefenderPhase::Startup:     return "startup";
		case EDefenderPhase::Active:      return "active";
		case EDefenderPhase::Recovery:    return "recovery";
		case EDefenderPhase::Stunned:     return "stunned";
		case EDefenderPhase::GuardBroken: return "guard-broken";
		case EDefenderPhase::Stepping:    return "stepping";
		case EDefenderPhase::Guarding:    return "guarding";
		case EDefenderPhase::Moving:      return "moving";
		default:                          return "other";
		}
	}

	EDefenderPhase DefenderPhaseOf(const FFighter& F)
	{
		switch (F.State)
		{
		case EFighterState::Idle:        return EDefenderPhase::Idle;
		case EFighterState::Hitstun:
		case EFighterState::Blockstun:
		case EFighterState::Stagger:     return EDefenderPhase::Stunned;
		case EFighterState::GuardBroken: return EDefenderPhase::GuardBroken;
		case EFighterState::Acting:
		{
			const FMoveData& M = F.CurrentMove();
			switch (M.Kind)
			{
			case EMoveKind::Attack:
				if (F.T < M.Startup) { return EDefenderPhase::Startup; }
				if (F.T < M.Startup + M.Active) { return EDefenderPhase::Active; }
				return EDefenderPhase::Recovery;
			case EMoveKind::Step:       return EDefenderPhase::Stepping;
			case EMoveKind::Guard:
			case EMoveKind::Counter:
			case EMoveKind::Parry:      return F.T < M.Startup + M.Active ? EDefenderPhase::Guarding : EDefenderPhase::Recovery;
			case EMoveKind::Locomotion: return EDefenderPhase::Moving;
			default:                    return EDefenderPhase::Other;
			}
		}
		default:
			return EDefenderPhase::Other;
		}
	}

	void FDuel::Reset()
	{
		Fighters[0].Reset(ESide::Player);
		Fighters[1].Reset(ESide::Boss);
		Frame = 0;
		Events.clear();
		bResolvingThisFrame[0] = bResolvingThisFrame[1] = false;
	}

	bool FDuel::CanCommit(ESide S, EMoveId Id) const
	{
		const FFighter& F = Get(S);
		if (F.IsDead() || Get(Opponent(S)).IsDead()) { return false; }
		if (!F.IsActionable()) { return false; }
		const FMoveData& M = Move(Id);
		if (M.Id == EMoveId::None || M.Owner != S) { return false; }
		if (M.ShaChiCost > 0.f && F.ShaChi < M.ShaChiCost) { return false; }
		if (M.IsAttack() && Frame < F.NoAttackUntil) { return false; } // after being parried: a breath for the player
		return true;
	}

	bool FDuel::Commit(ESide S, EMoveId Id, float FacingX, float FacingY)
	{
		if (!CanCommit(S, Id)) { return false; }
		FFighter& F = Get(S);
		RaiseWhiffIfPending(S);
		F.BeginMove(Id, Frame, FacingX, FacingY);

		FDuelEvent E;
		E.Type = EDuelEvent::Commit;
		E.Frame = Frame;
		E.Side = S;
		E.Move = Id;
		E.Sym = Move(Id).Symbol;
		Push(E);
		return true;
	}

	bool FDuel::CommitPlayerAttack(bool bHeavy, float FacingX, float FacingY)
	{
		FFighter& P = Get(ESide::Player);
		int32_t Depth = 0;
		if (!bHeavy && P.State == EFighterState::Acting)
		{
			const FMoveData& Cur = P.CurrentMove();
			if (Cur.IsAttack() && Cur.Symbol == ESym::Light)
			{
				Depth = P.ChainDepth + 1;
			}
		}
		const EMoveId Id = bHeavy ? HeavyFor(P.Weapon) : NextLight(P.Weapon, Depth);
		if (!Commit(ESide::Player, Id, FacingX, FacingY)) { return false; }
		P.ChainDepth = bHeavy ? 0 : Depth;
		return true;
	}

	bool FDuel::CommitSwitch()
	{
		if (!Commit(ESide::Player, EMoveId::PSwitch)) { return false; }
		FFighter& P = Get(ESide::Player);
		P.Weapon = P.Weapon == 0 ? 1 : 0;
		P.ChainDepth = 0;
		return true;
	}

	void FDuel::SetGuardHeld(ESide S, bool bHeld)
	{
		FFighter& F = Get(S);
		const bool bWasHeld = F.bGuardHeld;
		F.bGuardHeld = bHeld;
		if (bHeld && !bWasHeld && !F.IsDead())
		{
			FDuelEvent E;
			E.Type = EDuelEvent::Commit;
			E.Frame = Frame;
			E.Side = S;
			E.Move = EMoveId::None;
			E.Sym = ESym::Block;
			Push(E);
		}
	}

	int32_t FDuel::FrameAdvantage(ESide S) const
	{
		const int32_t Adv = Get(Opponent(S)).FramesUntilActionable() - Get(S).FramesUntilActionable();
		return Adv > 999 ? 999 : (Adv < -999 ? -999 : Adv); // a dead fighter is never actionable
	}

	EHitOutcome FDuel::Classify(ESide Attacker) const
	{
		const FFighter& Att = Get(Attacker);
		const FFighter& Def = Get(Opponent(Attacker));
		const FMoveData& M = Att.CurrentMove();
		if (Def.IsDead() || Def.IsInvulnerable())
		{
			return EHitOutcome::None; // i-frames: this frame does not connect; later active frames still may
		}
		if (!M.bUnparryable && (Def.IsParryLive() || Def.IsCounterLive()))
		{
			return EHitOutcome::Parried;
		}
		if (!M.bUnblockable && Def.IsGuarding())
		{
			return EHitOutcome::Blocked;
		}
		return EHitOutcome::Hit;
	}

	void FDuel::RaiseWhiffIfPending(ESide S)
	{
		FFighter& F = Get(S);
		if (F.State != EFighterState::Acting || bResolvingThisFrame[SideIndex(S)]) { return; }
		const FMoveData& M = F.CurrentMove();
		if (!M.IsAttack() || F.bSwingResolved || F.T < M.Startup) { return; }
		F.bSwingResolved = true;

		FDuelEvent E;
		E.Type = EDuelEvent::Outcome;
		E.Frame = Frame;
		E.Side = S;
		E.Move = F.Move;
		E.Sym = M.Symbol;
		E.Outcome = EHitOutcome::Whiff;
		E.FrameAdvantage = FrameAdvantage(S);
		Push(E);
	}

	void FDuel::ApplyOutcome(ESide Attacker, EMoveId MoveId, EHitOutcome Outcome)
	{
		const FCombatTuning& K = Tuning();
		FFighter& Att = Get(Attacker);
		const ESide DefSide = Opponent(Attacker);
		FFighter& Def = Get(DefSide);
		const FMoveData& M = Move(MoveId);

		Att.bSwingConnected = true;
		Att.bSwingResolved = true;

		float Damage = 0.f;
		bool bCounterHit = false;
		bool bDefenderDied = false;
		const EDefenderPhase PhaseBefore = DefenderPhaseOf(Def);
		bool bGuardBroke = false;
		ESide BrokenSide = DefSide;

		switch (Outcome)
		{
		case EHitOutcome::Parried:
		{
			// Parry pays a floor, always: fixed sha-chi damage + a short uncancellable stagger (PLAN A3).
			Def.bParrySucceeded = true;
			Att.SpendShaChi(K.ParryRewardShaChi);
			int32_t StunFrames = K.ParryRewardStagger;
			if (Att.ShaChi <= 0.f)
			{
				StunFrames = Attacker == ESide::Boss ? K.BossExposedStun : K.GuardBreakStun;
				Att.EnterStun(EFighterState::GuardBroken, StunFrames);
				bGuardBroke = true;
				BrokenSide = Attacker;
			}
			else
			{
				Att.EnterStun(EFighterState::Stagger, StunFrames);
			}
			// The keeper's next attack comes after a while, not the moment its stagger ends (the player's own counter-parry
			// by the keeper's stance leaves the player free).
			if (Attacker == ESide::Boss) { Att.NoAttackUntil = Frame + StunFrames + K.ParryAttackLockout; }
			break;
		}
		case EHitOutcome::Blocked:
		{
			Damage = M.Damage * K.BlockChip;
			Def.ApplyDamage(Damage);
			bDefenderDied = Def.IsDead();
			if (!bDefenderDied)
			{
				Def.SpendShaChi(M.ShaChiDrainOnBlock);
				if (Def.ShaChi <= 0.f)
				{
					Def.EnterStun(EFighterState::GuardBroken, DefSide == ESide::Boss ? K.BossExposedStun : K.GuardBreakStun);
					bGuardBroke = true;
				}
				else
				{
					Def.EnterStun(EFighterState::Blockstun, K.Blockstun);
				}
			}
			break;
		}
		case EHitOutcome::Hit:
		{
			Damage = M.Damage;
			if (DefSide == ESide::Boss && Def.State == EFighterState::GuardBroken)
			{
				Damage *= K.ExposedDamageMult;
			}
			bCounterHit = Def.IsInStartup() || Def.IsInRecovery();
			const bool bArmored = Def.HasHyperArmor();
			Def.ApplyDamage(Damage);
			bDefenderDied = Def.IsDead();
			if (!bDefenderDied && !bArmored)
			{
				const bool bAlreadyStunned = Def.State == EFighterState::Hitstun || Def.State == EFighterState::Stagger
					|| Def.State == EFighterState::GuardBroken || Def.State == EFighterState::Blockstun;
				// A fighter that re-commits the frame it recovers and is hit again has not escaped the combo.
				Def.ComboHitsTaken = (bAlreadyStunned || Def.FramesFree < K.ComboResetFrames) ? Def.ComboHitsTaken + 1 : 1;
				const int32_t FullStun = DefSide == ESide::Boss ? K.BossHitstun : K.Hitstun;
				const int32_t Stun = Def.ComboHitsTaken > K.ComboHitCap ? K.ComboDecayStun : FullStun;
				if (bAlreadyStunned)
				{
					// Being hit never shortens a stun the defender is already serving.
					if (Def.StunLeft < Stun) { Def.StunLeft = Stun; }
					if (Def.State == EFighterState::Blockstun) { Def.State = EFighterState::Hitstun; }
				}
				else
				{
					RaiseWhiffIfPending(DefSide);
					Def.EnterStun(EFighterState::Hitstun, Stun);
				}
			}
			break;
		}
		default:
			break;
		}

		FDuelEvent E;
		E.Type = EDuelEvent::Outcome;
		E.Frame = Frame;
		E.Side = Attacker;
		E.Move = MoveId;
		E.Sym = M.Symbol;
		E.Outcome = Outcome;
		E.Damage = Damage;
		E.FrameAdvantage = FrameAdvantage(Attacker);
		E.bCounterHit = bCounterHit;
		E.DefenderPhase = static_cast<uint8_t>(PhaseBefore);
		Push(E);

		if (bGuardBroke)
		{
			FDuelEvent G;
			G.Type = EDuelEvent::GuardBreak;
			G.Frame = Frame;
			G.Side = BrokenSide;
			Push(G);
		}
		if (bDefenderDied)
		{
			FDuelEvent D;
			D.Type = EDuelEvent::Death;
			D.Frame = Frame;
			D.Side = DefSide;
			Push(D);
		}
	}

	void FDuel::Step(IContactOracle& Oracle)
	{
		const FCombatTuning& K = Tuning();

		// 1. Contacts and classification from the PRE-resolution state of both fighters, so a trade is a trade.
		EHitOutcome Result[2] = { EHitOutcome::None, EHitOutcome::None };
		EMoveId     MoveAtContact[2] = { EMoveId::None, EMoveId::None };
		for (int32_t I = 0; I < 2; ++I)
		{
			const ESide S = static_cast<ESide>(I);
			const FFighter& F = Get(S);
			if (F.IsDead() || Get(Opponent(S)).IsDead() || !F.IsHitboxActive()) { continue; }
			if (!Oracle.Contacts(*this, S)) { continue; }
			Result[I] = Classify(S);
			MoveAtContact[I] = F.Move;
		}

		for (int32_t I = 0; I < 2; ++I)
		{
			const FFighter& F = Get(static_cast<ESide>(I));
			if (F.IsFeintFakeFrame() && F.CurrentMove().Symbol == ESym::BFeint)
			{
				FDuelEvent E;
				E.Type = EDuelEvent::FeintFake;
				E.Frame = Frame;
				E.Side = static_cast<ESide>(I);
				E.Move = F.Move;
				E.Sym = F.CurrentMove().Symbol;
				Push(E);
			}
		}

		// 2. Apply. A swing resolving this frame must not also be reported as a whiff by an interrupt.
		bResolvingThisFrame[0] = Result[0] != EHitOutcome::None;
		bResolvingThisFrame[1] = Result[1] != EHitOutcome::None;
		for (int32_t I = 0; I < 2; ++I)
		{
			if (Result[I] != EHitOutcome::None)
			{
				ApplyOutcome(static_cast<ESide>(I), MoveAtContact[I], Result[I]);
			}
		}
		bResolvingThisFrame[0] = bResolvingThisFrame[1] = false;

		// 3. Swings leaving Active this frame with nothing connected raise exactly one Whiff (PLAN §5.4).
		for (int32_t I = 0; I < 2; ++I)
		{
			const ESide S = static_cast<ESide>(I);
			FFighter& F = Get(S);
			if (F.State != EFighterState::Acting) { continue; }
			const FMoveData& M = F.CurrentMove();
			const int32_t LastActive = M.Startup + M.Active - 1;
			if (M.IsAttack() && !F.bSwingResolved && F.T == LastActive)
			{
				RaiseWhiffIfPending(S);
			}
			else if ((M.Kind == EMoveKind::Parry || M.Kind == EMoveKind::Counter) && !F.bParrySucceeded && F.T == LastActive)
			{
				FDuelEvent E;
				E.Type = EDuelEvent::ParryWhiff;
				E.Frame = Frame;
				E.Side = S;
				E.Move = F.Move;
				E.Sym = M.Symbol;
				Push(E);
			}
		}

		// 4. Advance.
		const float RegenPerFrame = K.ShaChiRegenPerSec / static_cast<float>(FramesPerSecond);
		Fighters[0].Advance(RegenPerFrame, K.ShaChiRegenDelay);
		Fighters[1].Advance(RegenPerFrame, K.ShaChiRegenDelay);
		++Frame;
	}
}
