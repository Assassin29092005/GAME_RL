// HellwalkerRL — engine-free core. Encounter, telemetry rows.

#include "HWCore/HWEncounter.h"

#include <cstdio>

namespace HW
{
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

	void FEncounter::Begin(IBossBrain* InBrain, int32_t Seed, bool bInImmortal)
	{
		BossBrain = InBrain;
		bImmortal = bInImmortal;
		Duel.Reset();
		if (bImmortal)
		{
			for (FFighter& F : Duel.Fighters) { F.Health = F.HealthMax = 1.0e9f; }
		}
		if (BossBrain != nullptr) { BossBrain->BeginEncounter(Seed); }
		Stats = FEncounterStats{};
		LastBossMove = EMoveId::None;
		Geometry = FDuelGeometry{};
		Events.clear();
		Rows.clear();
		bRowOpen = false;
	}

	const FBrainDecision& FEncounter::LastDecision() const
	{
		static const FBrainDecision None;
		return BossBrain != nullptr ? BossBrain->LastDecision() : None;
	}

	bool FEncounter::ThinkBoss(const FDuelGeometry& Geo)
	{
		Geometry = Geo;
		FBrainDecision D;
		if (BossBrain == nullptr || !BossBrain->Think(Duel, Geo, &D)) { return false; }
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
		const float Minutes = static_cast<float>(Duel.Frame > 0 ? Duel.Frame : 1) / (60.f * FramesPerSecond);
		OpenExchange.SwingsPerMin = static_cast<float>(Stats.BossSwings) / Minutes;
		OpenExchange.DamagePerMin = Stats.PlayerDamageTaken / Minutes;
		Rows.push_back(OpenExchange);
		bRowOpen = false;
	}

	void FEncounter::StepFrame(IContactOracle& Oracle, ESym PlayerMovement)
	{
		Duel.Step(Oracle);
		Duel.TakeEvents(Events);
		Stats.Frames = Duel.Frame;
		if (Duel.Get(ESide::Boss).State == EFighterState::GuardBroken) { ++Stats.BossExposedFrames; }

		const int32_t BrainDecisions = BossBrain != nullptr ? BossBrain->Decisions() : 0;
		const FBrainDecision& Ld = LastDecision();
		for (const FDuelEvent& E : Events)
		{
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
				else if (E.Move == EMoveId::PParry)
				{
					++Stats.PlayerParryPresses;
				}
				else if (E.Move != EMoveId::None && Move(E.Move).Kind == EMoveKind::Step)
				{
					++Stats.PlayerSteps;
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
					if (BrainDecisions > 0)
					{
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
					if (BrainDecisions > 0)
					{
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

		if (BossBrain != nullptr)
		{
			BossBrain->OnFrame(Events, Duel, Geometry, PlayerMovement);
			Stats.Symbols = BossBrain->SymbolsObserved();
			Stats.CountersLanded = BossBrain->CountersLanded();
			Stats.ShadowScriptSpm = BossBrain->ShadowScriptRate(Duel.Frame);
		}
		if (Duel.IsOver()) { CloseRow(); }
	}
}
