// Hellwalker — engine-free core.

#include "HWCore/HWEncounter.h"

#include <cstdio>

namespace HW
{
	// ----------------------------------------------------------------------------------------------
	// Symbol observer — PLAN §2.1 cadence
	// ----------------------------------------------------------------------------------------------

	void FSymbolObserver::Reset()
	{
		LastEmitFrame = 0;
		LastBossCommitFrame = -1;
		PendingBossMove = EMoveId::None;
		bBossSwingPending = false;
		bPlayerCommittedSinceBossSwing = false;
		NumEmitted = 0;
	}

	void FSymbolObserver::Emit(ESym S, int32_t Frame, int32_t Lead, FPlaystyleModel* Model, std::vector<FSymbolRecord>* OutLog, int32_t Bit)
	{
		if (Model != nullptr) { Model->Observe(S, Lead, Bit); }
		if (OutLog != nullptr) { OutLog->push_back(FSymbolRecord{ S, Frame, Lead }); }
		LastEmitFrame = Frame;
		++NumEmitted;
	}

	void FSymbolObserver::ProcessFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, ESym PlayerMovement,
		FPlaystyleModel* Model, std::vector<FSymbolRecord>* OutLog)
	{
		const FFighter& P = Duel.Get(ESide::Player);
		for (const FDuelEvent& E : Events)
		{
			if (E.Type == EDuelEvent::Commit)
			{
				if (E.Side == ESide::Player)
				{
					// Timing sample (the "when"): frames BEFORE the impact this press was timed to.
					int32_t Lead = FPlaystyleModel::NoTiming;
					int32_t Bit = FPlaystyleModel::NoBait;
					if (bBossSwingPending && !bPlayerCommittedSinceBossSwing && PendingBossMove != EMoveId::None)
					{
						const FMoveData& BM = Move(PendingBossMove);
						const int32_t Press = E.Frame - LastBossCommitFrame; // on the boss move's own timeline
						int32_t Anchor = BM.Startup;
						if (BM.FakeImpactFrame >= 0)
						{
							// A bait shows one impact and delivers another. A press nearer the bait than the real
							// strike was timed to the bait (it bit); a later one waited for the real strike (it read
							// the bait). Either way the lead is measured to the impact the player was timing — so a
							// player who has learned to wait does not read as one who "presses 10 frames late".
							Bit = Press * 2 < BM.FakeImpactFrame + BM.Startup ? 1 : 0;
							Anchor = Bit == 1 ? BM.FakeImpactFrame : BM.Startup;
						}
						if (Anchor >= TimingHorizonFrames) { Lead = Anchor - Press; }
					}
					Emit(E.Sym, E.Frame, Lead, Model, OutLog, Bit);
					bPlayerCommittedSinceBossSwing = true;
				}
				else
				{
					Emit(E.Sym, E.Frame, FPlaystyleModel::NoTiming, Model, OutLog);
					LastBossCommitFrame = E.Frame;
					if (Move(E.Move).IsAttack())
					{
						bBossSwingPending = true;
						bPlayerCommittedSinceBossSwing = false;
						PendingBossMove = E.Move;
					}
				}
			}
			else if (E.Type == EDuelEvent::Outcome && E.Side == ESide::Boss && bBossSwingPending)
			{
				// A boss damage window resolved. No player commitment during it -> a movement symbol
				// (or Block: holding guard through it IS the commitment).
				bBossSwingPending = false;
				PendingBossMove = EMoveId::None;
				if (!bPlayerCommittedSinceBossSwing)
				{
					Emit(P.IsGuarding() || P.bGuardHeld ? ESym::Block : PlayerMovement, E.Frame, FPlaystyleModel::NoTiming, Model, OutLog);
				}
			}
		}

		// Watchdog — a turtling player still generates data.
		const int32_t Now = Duel.Frame;
		if (Now - LastEmitFrame >= WatchdogFrames)
		{
			Emit(P.bGuardHeld ? ESym::Block : PlayerMovement, Now, FPlaystyleModel::NoTiming, Model, OutLog);
		}
	}

	// ----------------------------------------------------------------------------------------------
	// CSV (A2)
	// ----------------------------------------------------------------------------------------------

	const char* ExchangeCsvHeader()
	{
		return "run_id,mode,frame,script_index,slot,chain,scripted,chosen,kind,prev_outcome,frame_adv,read_bits,margin,"
			"predicted,predicted_p,pressure_bias,floor_correction,outcome,outcome_frame,frame_adv_after,damage,"
			"player_hp,boss_hp,swings_per_min,damage_per_min,mean_frame_ms,reason";
	}

	void FormatExchangeRow(const FExchangeRow& R, const char* RunId, EBrainMode Mode, float MeanFrameMs, char* Out, int32_t OutSize)
	{
		char Reason[sizeof(R.Decision.Reason)];
		int32_t J = 0;
		for (int32_t I = 0; R.Decision.Reason[I] != '\0' && J < static_cast<int32_t>(sizeof(Reason)) - 1; ++I)
		{
			Reason[J++] = R.Decision.Reason[I] == '"' ? '\'' : R.Decision.Reason[I];
		}
		Reason[J] = '\0';

		const FBrainDecision& D = R.Decision;
		std::snprintf(Out, static_cast<size_t>(OutSize),
			"%s,%s,%d,%d,%s,%d,%s,%s,%s,%s,%d,%.3f,%.3f,%s,%.3f,%.3f,%.4f,%s,%d,%d,%.1f,%.1f,%.1f,%.2f,%.2f,%.2f,\"%s\"",
			RunId, BrainModeName(Mode), D.Frame, D.ScriptIndex, SlotTypeName(D.Slot), D.bChain ? 1 : 0,
			Move(D.Scripted).Name, Move(D.Chosen).Name, DecisionKindName(D.Kind), OutcomeName(D.PrevOutcome), D.FrameAdvantage,
			D.ReadBits, D.Margin, SymName(D.Predicted), D.PredictedP, D.PressureBias, D.FloorCorrection,
			OutcomeName(R.Outcome), R.OutcomeFrame, R.FrameAdvantageAfter, R.Damage,
			R.PlayerHealth, R.BossHealth, R.SwingsPerMin, R.DamagePerMin, MeanFrameMs, Reason);
	}

	// ----------------------------------------------------------------------------------------------
	// Encounter
	// ----------------------------------------------------------------------------------------------

	void FEncounter::Begin(FPlaystyleModel* SessionModel, EBrainMode Mode, int32_t Seed, bool bInImmortal)
	{
		Model = SessionModel;
		bImmortal = bInImmortal;
		Duel.Reset();
		if (bImmortal)
		{
			for (FFighter& F : Duel.Fighters) { F.Health = F.HealthMax = 1.0e9f; }
		}
		Observer.Reset();
		Brain.Reset(Seed);
		Brain.BindModel(SessionModel);
		Brain.SetMode(Mode);
		if (Model != nullptr) { Model->Flush(); }
		Stats = FEncounterStats{};
		LastBossMove = EMoveId::None;
		Events.clear();
		Symbols.clear();
		Rows.clear();
		bRowOpen = false;
	}

	bool FEncounter::ThinkBoss(float Distance)
	{
		FBrainDecision D;
		if (!Brain.Think(Duel, Distance, &D)) { return false; }
		++Stats.Decisions;
		++Stats.DecisionKinds[static_cast<int32_t>(D.Kind)];
		if (D.ScriptIndex >= 0 && D.ScriptIndex < FEncounterStats::MaxSlots)
		{
			++Stats.DecisionsBySlot[D.ScriptIndex];
			if (Move(D.Chosen).IsAttack()) { ++Stats.SwingsBySlot[D.ScriptIndex]; }
		}
		if (!D.bArgmaxHolds) { ++Stats.ArgmaxViolations; }
		Stats.PressureBiasSum += D.PressureBias;
		Stats.FloorCorrectionSum += D.FloorCorrection;
		if (bRecordRows)
		{
			CloseRow();
			OpenRow(D);
		}
		return true;
	}

	void FEncounter::OpenRow(const FBrainDecision& D)
	{
		OpenExchange = FExchangeRow{};
		OpenExchange.Decision = D;
		bRowOpen = true;
	}

	void FEncounter::CloseRow()
	{
		if (!bRowOpen) { return; }
		OpenExchange.PlayerHealth = Duel.Get(ESide::Player).Health;
		OpenExchange.BossHealth = Duel.Get(ESide::Boss).Health;
		OpenExchange.SwingsPerMin = Brain.SwingsPerMin(Duel.Frame);
		OpenExchange.DamagePerMin = Brain.DamagePerMin(Duel.Frame);
		Rows.push_back(OpenExchange);
		bRowOpen = false;
	}

	void FEncounter::StepFrame(IContactOracle& Oracle, ESym PlayerMovement)
	{
		Duel.Step(Oracle);
		Duel.TakeEvents(Events);
		Stats.Frames = Duel.Frame;
		if (Duel.Get(ESide::Boss).State == EFighterState::GuardBroken) { ++Stats.BossExposedFrames; }

		for (const FDuelEvent& E : Events)
		{
			Brain.OnEvent(E, Duel);
			switch (E.Type)
			{
			case EDuelEvent::Commit:
				if (E.Side == ESide::Boss)
				{
					LastBossMove = E.Move;
					++Stats.MoveSelect[static_cast<int32_t>(E.Move)];
					if (Move(E.Move).IsAttack()) { ++Stats.BossSwings; }
				}
				else if (E.Move != EMoveId::None && Move(E.Move).IsAttack())
				{
					++Stats.PlayerSwings;
				}
				break;
			case EDuelEvent::Outcome:
				Stats.DamageByMove[static_cast<int32_t>(E.Move)] += E.Damage;
				if (E.Outcome == EHitOutcome::Hit) { ++Stats.HitsByMove[static_cast<int32_t>(E.Move)]; }
				if (E.Side == ESide::Boss)
				{
					++Stats.BossOutcomes[static_cast<int32_t>(E.Outcome)];
					++Stats.BossOutcomeByMove[static_cast<int32_t>(E.Move)][static_cast<int32_t>(E.Outcome)];
					Stats.PlayerDamageTaken += E.Damage;
					if (Brain.Decisions() > 0)
					{
						const FBrainDecision& Ld = Brain.LastDecision();
						if (Ld.ScriptIndex >= 0 && Ld.ScriptIndex < FEncounterStats::MaxSlots) { Stats.PlayerDamageBySlot[Ld.ScriptIndex] += E.Damage; }
						Stats.PlayerDamageByKind[static_cast<int32_t>(Ld.Kind)] += E.Damage;
					}
					if (bRowOpen && OpenExchange.Outcome == EHitOutcome::None && E.Move == OpenExchange.Decision.Chosen)
					{
						OpenExchange.Outcome = E.Outcome;
						OpenExchange.OutcomeFrame = E.Frame;
						OpenExchange.FrameAdvantageAfter = E.FrameAdvantage;
						OpenExchange.Damage = E.Damage;
					}
				}
				else
				{
					++Stats.PlayerOutcomes[static_cast<int32_t>(E.Outcome)];
					Stats.BossDamageTaken += E.Damage;
					Stats.BossDamageByPhase[E.DefenderPhase < static_cast<uint8_t>(EDefenderPhase::Count) ? E.DefenderPhase : 0] += E.Damage;
					Stats.BossDamageAfterMove[static_cast<int32_t>(LastBossMove)] += E.Damage;
					if (Brain.Decisions() > 0)
					{
						const FBrainDecision& Ld = Brain.LastDecision();
						if (Ld.ScriptIndex >= 0 && Ld.ScriptIndex < FEncounterStats::MaxSlots) { Stats.BossDamageAfterSlot[Ld.ScriptIndex] += E.Damage; }
						Stats.BossDamageAfterKind[static_cast<int32_t>(Ld.Kind)] += E.Damage;
					}
				}
				break;
			case EDuelEvent::GuardBreak:
				if (E.Side == ESide::Player) { ++Stats.PlayerGuardBreaks; } else { ++Stats.BossExposed; }
				break;
			case EDuelEvent::Death:
				if (E.Side == ESide::Player) { Stats.bPlayerDied = true; ++Stats.PlayerDeaths; }
				else { Stats.bBossDied = true; }
				if (Stats.DeathFrame < 0) { Stats.DeathFrame = E.Frame; }
				break;
			default:
				break;
			}
		}

		Observer.ProcessFrame(Events, Duel, PlayerMovement, Model, bRecordSymbols ? &Symbols : nullptr);
		Stats.Symbols = Observer.Emitted();
		Stats.CountersLanded = Brain.CountersLanded();
		Stats.ShadowScriptSpm = Brain.ShadowScriptRate(Duel.Frame);
		if (Duel.IsOver()) { CloseRow(); }
	}
}
