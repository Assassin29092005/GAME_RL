// Hellwalker — engine-free core. Derived payoff matrix (see header).

#include "HWCore/HWPayoffTable.h"

#include "HWCore/HWSim.h"

#include <cmath>

namespace HW
{
	namespace
	{
		bool IsMovementSym(ESym S) { return S == ESym::Neutral || S == ESym::Advance || S == ESym::Retreat; }

		EDir StepDirOf(ESym S)
		{
			switch (S)
			{
			case ESym::StepF: return EDir::Forward;
			case ESym::StepB: return EDir::Back;
			case ESym::StepL: return EDir::Left;
			case ESym::StepR: return EDir::Right;
			default:          return EDir::None;
			}
		}

	}

	int32_t FPayoffTable::TypicalLead(ESym Sym)
	{
		switch (Sym)
		{
		case ESym::Parry:  return 4;   // presses ~4 frames before the impact it sees coming
		case ESym::StepF:
		case ESym::StepB:
		case ESym::StepL:
		case ESym::StepR:  return 8;   // i-frames 3-18 centred on the impact
		case ESym::Block:  return 14;
		case ESym::Light:  return 6;   // tries to beat the swing out
		case ESym::Heavy:  return 12;
		case ESym::Switch: return 10;
		default:           return 0;
		}
	}

	float FPayoffTable::MeasureExchange(EMoveId Boss, ESym Sym, int32_t Lead, float* OutBossDealt, float* OutPlayerDealt)
	{
		const FCombatTuning& K = Tuning();
		const FMoveData& M = Move(Boss);

		FDuel Duel;
		Duel.Reset();
		FSimArena Arena;
		Arena.Reset(170.f); // an engagement at typical attack range

		int32_t Press = M.PerceivedImpact() - Lead;
		if (Press < 0) { Press = 0; }
		const bool bMovement = IsMovementSym(Sym);
		const int32_t Horizon = M.TotalFrames() + 50;

		bool bResponded = bMovement;
		bool bBossPunished = false;
		int32_t GuardUntil = -1;
		int32_t StringLeft = 0;
		float BossDealt = 0.f;
		float PlayerDealt = 0.f;
		std::vector<FDuelEvent> Events;

		for (int32_t F = 0; F < Horizon; ++F)
		{
			if (F == 0)
			{
				const FVec2 Face = Arena.FacingOf(ESide::Boss);
				Duel.Commit(ESide::Boss, Boss, Face.X, Face.Y);
			}

			// ---- the response, pressed at its lead
			float Fwd = 0.f;
			const FVec2 PFace = Arena.FacingOf(ESide::Player);
			if (!bResponded && F >= Press)
			{
				switch (Sym)
				{
				case ESym::Light:  bResponded = Duel.CommitPlayerAttack(false, PFace.X, PFace.Y); break;
				case ESym::Heavy:  bResponded = Duel.CommitPlayerAttack(true, PFace.X, PFace.Y); break;
				case ESym::Parry:  bResponded = Duel.Commit(ESide::Player, EMoveId::PParry); break;
				case ESym::Switch: bResponded = Duel.CommitSwitch(); break;
				case ESym::Block:  GuardUntil = M.Startup + M.Active + 12; bResponded = true; break;
				default:
				{
					const EDir D = StepDirOf(Sym);
					bResponded = D == EDir::None || Duel.Commit(ESide::Player, StepFor(D), PFace.X, PFace.Y);
					break;
				}
				}
			}
			if (Sym == ESym::Advance && F < 30) { Fwd = 1.f; }
			if (Sym == ESym::Retreat && F < 30) { Fwd = -1.f; }
			Duel.SetGuardHeld(ESide::Player, F < GuardUntil);

			// ---- then a competent punisher: any opening the boss leaves, it takes
			const FFighter& P = Duel.Get(ESide::Player);
			const FFighter& B = Duel.Get(ESide::Boss);
			if (bResponded && F > Press && F >= GuardUntil && P.IsActionable() && Arena.Distance() <= 185.f)
			{
				const bool bOpen = B.State == EFighterState::Stagger || B.State == EFighterState::GuardBroken
					|| B.State == EFighterState::Hitstun
					|| (B.State == EFighterState::Acting && B.IsInRecovery() && B.FramesUntilActionable() > 10);
				if (StringLeft == 0 && bOpen) { StringLeft = 3; }
				if (StringLeft > 0 && Duel.CommitPlayerAttack(false, PFace.X, PFace.Y)) { --StringLeft; }
			}

			// ---- and the boss gets its one punish too, so a guard / counter that SETS UP damage is valued
			// (the exchange is two-sided: the next slot's perfect punish is part of what this move buys).
			if (!bBossPunished && F > M.Startup && B.IsActionable() && Arena.Distance() <= Move(EMoveId::BFastSlash).Range * 0.9f)
			{
				const bool bPlayerOpen = P.State == EFighterState::Stagger || P.State == EFighterState::GuardBroken
					|| P.State == EFighterState::Hitstun
					|| (P.State == EFighterState::Acting && P.FramesUntilActionable() > Move(EMoveId::BFastSlash).Startup);
				if (bPlayerOpen)
				{
					const FVec2 BFace = Arena.FacingOf(ESide::Boss);
					bBossPunished = Duel.Commit(ESide::Boss, EMoveId::BFastSlash, BFace.X, BFace.Y);
				}
			}

			Arena.LatchCommits(Duel);
			Arena.SnapshotPreStep(Duel);
			Duel.Step(Arena);
			Duel.TakeEvents(Events);
			for (const FDuelEvent& E : Events)
			{
				if (E.Type != EDuelEvent::Outcome) { continue; }
				if (E.Side == ESide::Boss) { BossDealt += E.Damage; } else { PlayerDealt += E.Damage; }
			}
			Arena.Integrate(Duel, Fwd, 0.f);
			if (Duel.IsOver()) { break; }
		}

		if (OutBossDealt != nullptr) { *OutBossDealt = BossDealt; }
		if (OutPlayerDealt != nullptr) { *OutPlayerDealt = PlayerDealt; }
		return BossDealt / K.PlayerHealthMax - PlayerDealt / K.BossHealthMax;
	}

	void FPayoffTable::Build()
	{
		for (int32_t Mi = 0; Mi < NumMoves; ++Mi)
		{
			const FMoveData& M = Move(static_cast<EMoveId>(Mi));
			for (int32_t S = 0; S < NumPlayerSymbols; ++S)
			{
				for (int32_t L = 0; L < NumLeads; ++L) { V[Mi][S][L] = 0.f; }
				if (M.Owner != ESide::Boss || M.Id == EMoveId::None) { continue; }
				const ESym Sym = static_cast<ESym>(S);
				const bool bMovement = IsMovementSym(Sym);
				for (int32_t L = 0; L < NumLeads; ++L)
				{
					if (bMovement && L > 0) { V[Mi][S][L] = V[Mi][S][0]; continue; }
					float P = Scale * MeasureExchange(M.Id, Sym, MinLead + L);
					if (P > 3.f) { P = 3.f; }
					if (P < -3.f) { P = -3.f; }
					// Authoring rule (PLAN §2.4): defensive actions cap at +1; only attacks reach +2 / +3.
					if (!M.IsAttack() && P > 1.f) { P = 1.f; }
					V[Mi][S][L] = P;
				}
			}
		}
		bBuilt = true;
	}

	float FPayoffTable::Get(EMoveId Boss, ESym Sym, int32_t Lead) const
	{
		if (!IsPlayerSym(Sym)) { return 0.f; }
		int32_t L = Lead - MinLead;
		if (L < 0) { L = 0; }
		if (L >= NumLeads) { L = NumLeads - 1; }
		return V[static_cast<int32_t>(Boss)][SymIndex(Sym)][L];
	}

	namespace
	{
		FPayoffTable& TableInstance()
		{
			static FPayoffTable Instance;
			return Instance;
		}
	}

	const FPayoffTable& DerivedPayoffs()
	{
		FPayoffTable& T = TableInstance();
		if (!T.IsBuilt()) { T.Build(); }
		return T;
	}

	void RebuildDerivedPayoffs()
	{
		TableInstance().Build();
	}
}
