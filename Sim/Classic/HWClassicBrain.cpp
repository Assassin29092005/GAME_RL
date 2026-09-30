// HellwalkerRL — CLASSIC (reference) brain, tools only. Implementation (verbatim from the reference project).

#include "Classic/HWClassicBrain.h"

#include "Classic/HWPayoffTable.h"

#include <cmath>
#include <cstdio>

namespace HW
{
	namespace
	{
		// ------------------------------------------------------------------------------------------
		// The payoff matrix — where a correct prediction becomes a real counter (PLAN §2.4).
		// AUTHORING RULE: defensive actions cap at +1; only attacking actions reach +2 / +3.
		// Payoff maximisation is therefore biased toward attacking in the DATA, not in code.
		// Columns: Neutral Advance Retreat Light Heavy Block Parry StepF StepB StepL StepR Switch
		// ------------------------------------------------------------------------------------------
		struct FPayoffRow { EMoveId Move; float V[NumPlayerSymbols]; };

		constexpr FPayoffRow PayoffRows[] = {
			//                         Neu  Adv  Ret  Lgt  Hvy  Blk  Pry  StF  StB  StL  StR  Swi
			{ EMoveId::BFastSlash,     { 2,   2,   0,   0,  -1,   0,  -3,  -1,  -1,  -2,  -2,   2 } },
			{ EMoveId::BSweepLeft,     { 2,   2,   0,   0,  -1,   0,  -3,  -1,  -1,   3,  -2,   2 } },
			{ EMoveId::BSweepLeftLate, { 2,   2,   0,  -1,  -1,   0,  -2,  -1,  -1,   3,  -2,   2 } },
			{ EMoveId::BSweepRight,    { 2,   2,   0,   0,  -1,   0,  -3,  -1,  -1,  -2,   3,   2 } },
			{ EMoveId::BSweepRightLate,{ 2,   2,   0,  -1,  -1,   0,  -2,  -1,  -1,  -2,   3,   2 } },
			{ EMoveId::BHeavyCleave,   { 2,   2,  -1,   2,   0,   2,  -3,  -2,  -2,  -2,  -2,   2 } },
			{ EMoveId::BDelayedHeavy,  { 2,   2,  -1,   1,   0,   2,   1,  -1,   0,   0,   0,   2 } },
			{ EMoveId::BHeavySweepLeft,{ 2,   2,  -1,   2,   0,   2,  -3,  -2,  -2,   3,  -2,   2 } },
			{ EMoveId::BHeavySweepRight,{2,   2,  -1,   2,   0,   2,  -3,  -2,  -2,  -2,   3,   2 } },
			{ EMoveId::BFeintEarly,    { 1,   1,  -1,  -2,  -1,   1,   2,  -1,   0,   0,   0,   1 } },
			{ EMoveId::BFeintMid,      { 1,   1,  -1,  -2,  -1,   1,   2,  -1,   0,   0,   0,   1 } },
			{ EMoveId::BFeintLate,     { 1,   1,  -1,  -2,  -1,   1,   2,  -1,   0,   0,   0,   1 } },
			{ EMoveId::BKillerThrust,  { 2,   2,   1,   1,   0,   3,   3,  -2,  -1,  -3,  -3,   2 } },
			{ EMoveId::BGrab,          { 3,   2,  -2,  -2,  -1,   3,   3,  -2,  -2,  -2,  -2,   2 } },
			// Defensive — capped at +1
			{ EMoveId::BGuard,         {-1,   0,  -1,   1,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 } },
			{ EMoveId::BCounterStance, {-1,   0,  -1,   1,   1,  -1,  -2,  -1,  -1,  -1,  -1,  -1 } },
			{ EMoveId::BBackstep,      {-1,  -1,  -1,   1,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 } },
			{ EMoveId::BSideStepL,     {-1,  -1,  -1,   1,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 } },
			{ EMoveId::BSideStepR,     {-1,  -1,  -1,   1,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 } },
			// Movement
			{ EMoveId::BApproach,      { 0,   0,   1,  -1,  -1,   0,   0,   0,   0,   0,   0,   0 } },
			{ EMoveId::BDashIn,        { 0,   0,   1,  -1,  -1,   0,   0,   0,   0,   0,   0,   0 } },
			{ EMoveId::BRetreat,       { 0,  -1,  -1,   1,   1,   0,   0,   0,   0,   0,   0,   0 } },
		};

		const FPayoffRow* FindRow(EMoveId M)
		{
			for (const FPayoffRow& R : PayoffRows)
			{
				if (R.Move == M) { return &R; }
			}
			return nullptr;
		}

		constexpr EMoveId AbortOnWhiff[] = { EMoveId::BGuard, EMoveId::BCounterStance, EMoveId::BBackstep, EMoveId::BSideStepL, EMoveId::BSideStepR };
		constexpr EMoveId AbortOnParried[] = { EMoveId::BGuard, EMoveId::BCounterStance, EMoveId::BBackstep, EMoveId::BFeintEarly, EMoveId::BFeintMid, EMoveId::BFeintLate };

		float Lerp(float A, float B, float T) { return A + (B - A) * T; }
		float Clamp01(float X) { return X < 0.f ? 0.f : (X > 1.f ? 1.f : X); }
	}

	void ApplyScriptTuning(int32_t Index, FBrainConfig& Cfg)
	{
		const FBrainConfig Defaults;
		Cfg.FloorMarginSpm = Index == 1 ? 5.0f : Defaults.FloorMarginSpm;
		Cfg.ShadowGain = Index == 1 ? 0.06f : Defaults.ShadowGain;
	}

	FClassicBrain::FClassicBrain()
	{
		const FScriptSlot* Slots = nullptr;
		const int32_t N = DefaultWardenScript(Slots);
		SetScript(Slots, N);
		Reset(0);
	}

	void FClassicBrain::SetScript(const FScriptSlot* Slots, int32_t Count)
	{
		ScriptLen = Count < MaxScript ? Count : MaxScript;
		for (int32_t I = 0; I < ScriptLen; ++I) { Script[I] = Slots[I]; }
		ScriptIndex = 0;
		bSlotOpen = false;
	}

	void FClassicBrain::Reset(int32_t Seed)
	{
		Rng.Initialize(Seed);
		ScriptIndex = 0;
		bSlotOpen = false;
		CurrentSlotIndex = 0;
		LastSwingOutcome = EHitOutcome::None;
		LastSwingMove = EMoveId::None;
		bOwnDefenceSucceeded = false;
		PressureBias = 0.f;
		LastFloorCorrection = 0.f;
		ShadowScriptedSwings = 0;
		ShadowChosenSwings = 0;
		ShadowScriptedFrames = 0;
		ShadowChosenFrames = 0;
		ConsecutiveSubs = 0;
		for (bool& B : RecentSubs) { B = false; }
		RecentHead = 0;
		SwingCount = 0;
		DamageTotal = 0.f;
		SubCount = 0;
		DecisionCount = 0;
		CounterHits = 0;
		PendingCounterMove = EMoveId::None;
		bReadMeterReady = false;
		Last = FBrainDecision{};
	}

	// ----------------------------------------------------------------------------------------------
	// Payoffs
	// ----------------------------------------------------------------------------------------------

	float FClassicBrain::BasePayoff(EMoveId BossMove, ESym PlayerSym)
	{
		const FPayoffRow* R = FindRow(BossMove);
		if (R == nullptr || !IsPlayerSym(PlayerSym)) { return 0.f; }
		return R->V[SymIndex(PlayerSym)];
	}

	bool FClassicBrain::IsWhiffable(ESym S)
	{
		switch (S)
		{
		case ESym::Neutral: case ESym::Retreat: case ESym::Block: case ESym::Parry:
		case ESym::StepB: case ESym::StepL: case ESym::StepR: case ESym::Switch:
			return true;
		default:
			return false;
		}
	}

	int32_t FClassicBrain::LeadFor(ESym PlayerSym) const
	{
		if (Model != nullptr && Model->LeadEvidence(PlayerSym) >= Cfg.MinLeadEvidence)
		{
			return static_cast<int32_t>(std::lround(Model->LeadMean(PlayerSym)));
		}
		return FPayoffTable::TypicalLead(PlayerSym);
	}

	float FClassicBrain::BiteFor(ESym PlayerSym) const
	{
		if (Model != nullptr && Model->BiteEvidence(PlayerSym) >= Cfg.MinLeadEvidence)
		{
			return Model->BiteMean(PlayerSym);
		}
		return 1.f; // until shown otherwise, a player times to what the wind-up shows
	}

	float FClassicBrain::Payoff(EMoveId BossMove, ESym PlayerSym) const
	{
		if (Cfg.bDerivedPayoffs)
		{
			// Timing the counter, measured: the table resolves this player's press timing against this
			// move's real frame data through FDuel itself.
			const int32_t Lead = LeadFor(PlayerSym);
			const float OnBait = DerivedPayoffs().Get(BossMove, PlayerSym, Lead);
			const FMoveData& Bm = Move(BossMove);
			if (!Bm.IsAttack() || Bm.FakeImpactFrame < 0) { return OnBait; }
			// A bait only pays against the presses that bite. A player who waits for the real strike presses at
			// the same lead before IT — i.e. (Startup - FakeImpact) frames later on the bait's own timeline.
			const float Bite = BiteFor(PlayerSym);
			if (Bite >= 1.f) { return OnBait; }
			const float OnReal = DerivedPayoffs().Get(BossMove, PlayerSym, Lead - (Bm.Startup - Bm.FakeImpactFrame));
			return Bite * OnBait + (1.f - Bite) * OnReal;
		}
		const float Base = BasePayoff(BossMove, PlayerSym);
		const FMoveData& M = Move(BossMove);
		if (!M.IsAttack() || Model == nullptr) { return Base; }
		if (Model->LeadEvidence(PlayerSym) < Cfg.MinLeadEvidence) { return Base; }

		// Timing the counter: the sidecar says how many frames before the PERCEIVED impact this player
		// presses. Place that press on this move's own timeline and resolve it against this move's real
		// frame data — the same windows FDuel enforces. A feint / held wind-up moves the perceived impact
		// away from the real one; an honest move does not, so it cannot "bait" a player who times to it.
		const FCombatTuning& K = Tuning();
		const int32_t D = M.PerceivedImpact() - static_cast<int32_t>(std::lround(Model->LeadMean(PlayerSym)));
		const int32_t A0 = M.Startup;                  // first active frame
		const int32_t A1 = M.Startup + M.Active - 1;   // last active frame

		switch (PlayerSym)
		{
		case ESym::Parry:
		{
			if (M.bUnparryable) { return Base; }
			const int32_t LiveFrom = D + 1;
			const int32_t LiveTo = D + K.ParryWindowFrames;
			if (A0 >= LiveFrom && A0 <= LiveTo) { return -3.f; }                        // parried
			if (A0 > LiveTo && A0 <= LiveTo + K.ParryWhiffRecovery) { return 3.f; }     // baited: lands in whiff recovery
			if (A0 < LiveFrom) { return 2.f; }                                          // lands before the press
			return 0.f;                                                                 // they recover and re-parry
		}
		case ESym::StepL:
		case ESym::StepR:
		case ESym::StepB:
		case ESym::StepF:
		{
			const int32_t I0 = D + K.StepIFrameStart;
			const int32_t I1 = D + K.StepIFrameEnd;
			const bool bEarly = A0 < I0;   // connects before the invulnerability starts
			const bool bLate = A1 > I1;    // still active after it ends
			if (bEarly) { return M.bUnblockable ? 3.f : 2.f; }
			if (PlayerSym == ESym::StepL || PlayerSym == ESym::StepR)
			{
				const bool bTracks = (PlayerSym == ESym::StepL && M.Coverage == ECoverage::SweepLeft)
					|| (PlayerSym == ESym::StepR && M.Coverage == ECoverage::SweepRight);
				return (bTracks && bLate) ? 3.f : -2.f;
			}
			if (PlayerSym == ESym::StepB) { return (bLate && M.Range >= 330.f) ? 1.f : -1.f; }
			return (bLate && M.Coverage == ECoverage::Wide) ? 1.f : -2.f;
		}
		case ESym::Light:
		case ESym::Heavy:
		{
			// The player swings back. Whose hitbox is out first, and is the boss armored when theirs lands?
			const FMoveData& PM = Move(PlayerSym == ESym::Light ? EMoveId::PLight1 : EMoveId::PHeavy);
			const int32_t Arrive = D + PM.Startup;           // player's first active frame, on this move's timeline
			if (Arrive >= A0) { return Base; }               // the boss's swing is out first (or they trade)
			if (M.HasHyperArmorAt(Arrive)) { return Base; }  // armored through it
			return -2.f;                                     // interrupted in startup: the boss eats a string
		}
		default:
			return Base;
		}
	}

	// ----------------------------------------------------------------------------------------------
	// Scoring
	// ----------------------------------------------------------------------------------------------

	bool FClassicBrain::InRange(EMoveId Id, float Distance) const
	{
		const FMoveData& M = Move(Id);
		return !M.IsAttack() || Distance <= M.Range * Cfg.InRangeFactor;
	}

	float FClassicBrain::IncomingThreatPayoff(const FMoveData& M, int32_t IncomingIn)
	{
		// A player swing is visibly on its way and lands IncomingIn frames after this commit. This is
		// animation reading (it is on screen), not input reading: FDuel state as of the previous frame.
		switch (M.Kind)
		{
		case EMoveKind::Attack:
			if (IncomingIn >= M.Startup) { return 0.f; }           // our hitbox is out first: no override
			return M.HasHyperArmorAt(IncomingIn) ? 0.f : -2.f;     // interrupted in startup
		case EMoveKind::Guard:
			return M.Startup <= IncomingIn ? 1.f : -1.f;           // guard up in time
		case EMoveKind::Counter:
			return (IncomingIn >= M.Startup && IncomingIn < M.Startup + M.Active) ? 1.f : -1.f;
		case EMoveKind::Step:
			return M.IsInvulnerableAt(IncomingIn) ? 1.f : -1.f;
		default:
			return -1.f;                                           // walking into it
		}
	}

	void FClassicBrain::ScoreCandidate(EMoveId Cand, EMoveId ScriptedMove, float Margin, float Distance, int32_t IncomingIn, FCandidateScore& Out) const
	{
		const FMoveData& M = Move(Cand);
		const FPrediction P = Model->PredictAfter(M.Symbol);

		float E = 0.f;
		const float Threat = IncomingIn >= 0 ? IncomingThreatPayoff(M, IncomingIn) : 0.f;
		if (Threat != 0.f)
		{
			E = Threat; // what is already on screen outranks what the habit predicts
		}
		else if (M.IsAttack() && Distance > M.Range)
		{
			E = -1.f; // it would whiff at this spacing whatever the player does
		}
		else
		{
			for (int32_t S = 0; S < NumPlayerSymbols; ++S)
			{
				if (P.P[S] > 0.f) { E += P.P[S] * Payoff(Cand, static_cast<ESym>(S)); }
			}
		}

		// Correction 3: expected payoff per frame of commitment, normalised to the scripted action so
		// Margin stays in payoff units. Gains are divided by relative commitment; losses are scaled UP
		// by it — a long commitment is never a cheaper way to lose.
		const float Ref = static_cast<float>(Move(ScriptedMove).ActionableFrames() > 0 ? Move(ScriptedMove).ActionableFrames() : 1);
		const float Cf = static_cast<float>(M.ActionableFrames() > 0 ? M.ActionableFrames() : 1);
		float Score = E >= 0.f ? E * (Ref / Cf) : E * (Cf / Ref);
		// Floor pressure is a SWING-RATE correction, so it is paid per frame too: under a deficit the
		// shorter attack gains more than the longer one.
		if (M.IsAttack()) { Score += PressureBias * (Ref / Cf); }

		Out.Move = Cand;
		Out.Expected = E;
		Out.Score = Score;
		Out.Decision = Score - (Cand == ScriptedMove ? 0.f : Margin);
	}

	void FClassicBrain::FinishDecision(FBrainDecision& D, const FCandidateScore* Cands, int32_t NumCands) const
	{
		// argmax over Decision; exact ties prefer the scripted action, then a seeded tie-break.
		float Best = -1.0e30f;
		for (int32_t I = 0; I < NumCands; ++I) { if (Cands[I].Decision > Best) { Best = Cands[I].Decision; } }
		int32_t Ties[64];
		int32_t NumTies = 0;
		bool bScriptedTied = false;
		for (int32_t I = 0; I < NumCands && NumTies < 64; ++I)
		{
			if (Cands[I].Decision >= Best - 1.0e-6f)
			{
				Ties[NumTies++] = I;
				if (Cands[I].Move == D.Scripted) { bScriptedTied = true; }
			}
		}
		if (bScriptedTied || NumTies == 0)
		{
			D.Chosen = D.Scripted;
		}
		else
		{
			D.Chosen = Cands[Ties[NumTies > 1 ? Rng.RandHelper(NumTies) : 0]].Move;
		}

		// Top three by Decision, deterministic (stable selection).
		bool Used[64] = {};
		D.NumTop = 0;
		for (int32_t K = 0; K < 3 && K < NumCands; ++K)
		{
			int32_t BestI = -1;
			for (int32_t I = 0; I < NumCands && I < 64; ++I)
			{
				if (!Used[I] && (BestI < 0 || Cands[I].Decision > Cands[BestI].Decision)) { BestI = I; }
			}
			if (BestI < 0) { break; }
			Used[BestI] = true;
			D.Top[D.NumTop++] = Cands[BestI];
		}

		// C3: chosen == argmax on every decision.
		float ChosenDecision = -1.0e30f;
		for (int32_t I = 0; I < NumCands; ++I) { if (Cands[I].Move == D.Chosen) { ChosenDecision = Cands[I].Decision; } }
		D.bArgmaxHolds = ChosenDecision >= Best - 1.0e-6f;
	}

	bool FClassicBrain::CapReached() const
	{
		if (ConsecutiveSubs >= Cfg.MaxConsecutiveSubs) { return true; }
		int32_t N = 0;
		const int32_t W = Cfg.SubWindow < 32 ? Cfg.SubWindow : 32;
		for (int32_t I = 0; I < W; ++I) { if (RecentSubs[I]) { ++N; } }
		return N >= Cfg.MaxSubsInWindow;
	}

	void FClassicBrain::UpdateFloor(int32_t Frame)
	{
		LastFloorCorrection = 0.f;
		if (BrainMode != EBrainMode::Hellwalker) { return; }
		// §1.2 as written: Deficit = RefSwingsPerMin - SwingsPerMin; if (Deficit > 0) PressureBias += Deficit * FloorGain.
		if (Cfg.RefSwingsPerMin > 0.f && Frame >= Cfg.FloorWarmupFrames)
		{
			const float Deficit = Cfg.RefSwingsPerMin - SwingsPerMin(Frame);
			if (Deficit > 0.f) { LastFloorCorrection += Deficit * Cfg.FloorGain; }
		}
		// Matched-player form. Counterfactual: in THIS fight, the script would have thrown ShadowScriptedSwings
		// in (elapsed - the extra frames our substitutions added). Everything else — approaches, stuns, the
		// player's pace — is common to both. Aim FloorMarginSpm above it so noise cannot push us under.
		if (Frame >= Cfg.FloorWarmupFrames / 3 && Frame > 0)
		{
			const float Elapsed = static_cast<float>(Frame);
			const float ShadowTime = Elapsed - static_cast<float>(ShadowChosenFrames - ShadowScriptedFrames);
			if (ShadowTime > 60.f)
			{
				const float ScriptRate = 3600.f * static_cast<float>(ShadowScriptedSwings) / ShadowTime;
				const float ActualRate = 3600.f * static_cast<float>(SwingCount) / Elapsed;
				const float Deficit = ScriptRate + Cfg.FloorMarginSpm - ActualRate;
				if (Deficit > 0.f) { LastFloorCorrection += Deficit * Cfg.ShadowGain; }
			}
		}

		if (LastFloorCorrection > 0.f)
		{
			PressureBias += LastFloorCorrection;
			if (PressureBias > Cfg.PressureBiasMax) { PressureBias = Cfg.PressureBiasMax; }
		}
		else
		{
			PressureBias *= Cfg.PressureDecay;
		}
	}

	// ----------------------------------------------------------------------------------------------
	// Decide — the pure decision function
	// ----------------------------------------------------------------------------------------------

	FBrainDecision FClassicBrain::Decide(const FDuel& Duel, const FScriptSlot& InSlot, int32_t InScriptIndex, EHitOutcome InPrevOutcome,
		bool bInOwnDefence, float Distance) const
	{
		FBrainDecision D;
		D.Frame = Duel.Frame;
		D.ScriptIndex = InScriptIndex;
		D.Slot = InSlot.Type;
		D.bChain = InSlot.bChain;
		D.Scripted = InSlot.Move;
		D.Chosen = InSlot.Move;
		D.PrevOutcome = InPrevOutcome;
		D.FrameAdvantage = Duel.FrameAdvantage(ESide::Boss);
		D.PressureBias = PressureBias;
		D.FloorCorrection = LastFloorCorrection;

		const FMoveData& Sc = Move(InSlot.Move);

		// The prediction is computed in BOTH modes (telemetry, Read Meter, B4 logs) but only USED by Hellwalker.
		FPrediction PS;
		if (Model != nullptr)
		{
			PS = Model->PredictAfter(Sc.Symbol);
			D.ReadBits = PS.ReadBits;
			D.Margin = Lerp(Cfg.MarginHigh, Cfg.MarginLow, Clamp01(PS.ReadBits / Cfg.ReadForMinMargin));
			D.Predicted = PS.Top;
			D.PredictedP = PS.TopP;
			D.Ctx0 = PS.Ctx1 == ESym::Count ? ESym::Count : (Model->LastSymbol());
			D.Ctx1 = Sc.Symbol;
		}

		if (BrainMode == EBrainMode::Pathbreaker || Model == nullptr)
		{
			D.Kind = EDecisionKind::Script;
			std::snprintf(D.Reason, sizeof(D.Reason), "script %s", Sc.Name);
			return D;
		}

		const int32_t FA = D.FrameAdvantage;
		FCandidateScore Cands[NumMoves];

		// Frame advantage is authoritative: a player swing already on its way (and in reach) is on screen.
		int32_t IncomingIn = -1;
		{
			const FFighter& P = Duel.Get(ESide::Player);
			if (P.State == EFighterState::Acting && P.CurrentMove().IsAttack() && !P.bSwingResolved
				&& P.T >= Cfg.ThreatPerceptionFrames
				&& P.T < P.CurrentMove().Startup + P.CurrentMove().Active && Distance <= P.CurrentMove().Range + 40.f)
			{
				const int32_t Left = P.CurrentMove().Startup - P.T;
				IncomingIn = Left > 0 ? Left : 0;
			}
		}
		int32_t N = 0;

		// ---- Lucky Draw: outcome-driven, on-screen facts (hit / whiff / blocked / parried) --------
		if (Cfg.bLuckyDraw)
		{
			// Perfect punish: the boss's own guard / counter just succeeded.
			if (bInOwnDefence)
			{
				EMoveId Best = EMoveId::None;
				float BestDmg = -1.f;
				for (int32_t I = 1; I < NumMoves; ++I)
				{
					const FMoveData& M = Move(static_cast<EMoveId>(I));
					if (M.Owner != ESide::Boss || !M.IsAttack() || !InRange(M.Id, Distance)) { continue; }
					if (M.Startup > FA - 1) { continue; }            // must be guaranteed
					if (Sc.IsAttack() && M.Startup < Sc.Startup) { continue; } // legibility (a)
					if (M.Damage > BestDmg) { BestDmg = M.Damage; Best = M.Id; }
				}
				if (Best != EMoveId::None)
				{
					D.Chosen = Best;
					D.Kind = EDecisionKind::PerfectPunish;
					std::snprintf(D.Reason, sizeof(D.Reason), "own defence landed (+%df) -> %s guaranteed", FA, Move(Best).Name);
					return D;
				}
			}

			if (InSlot.bChain && InPrevOutcome != EHitOutcome::None)
			{
				switch (InPrevOutcome)
				{
				case EHitOutcome::Hit:
				{
					// Hit confirmed: press with the heaviest follow-up that is still guaranteed.
					EMoveId Best = EMoveId::None;
					float BestDmg = -1.f;
					for (int32_t I = 1; I < NumMoves; ++I)
					{
						const FMoveData& M = Move(static_cast<EMoveId>(I));
						if (M.Owner != ESide::Boss || !M.IsAttack() || !InRange(M.Id, Distance)) { continue; }
						if (M.Startup < Sc.Startup || M.Startup > FA - 1) { continue; }
						if (M.Damage > BestDmg) { BestDmg = M.Damage; Best = M.Id; }
					}
					if (Best != EMoveId::None)
					{
						D.Chosen = Best;
						D.Kind = EDecisionKind::LuckyDrawPress;
						std::snprintf(D.Reason, sizeof(D.Reason), "hit confirmed (+%df) -> %s guaranteed", FA, Move(Best).Name);
						return D;
					}
					break; // nothing guaranteed: fall through to model scoring (attacks only)
				}
				case EHitOutcome::Whiff:
				case EHitOutcome::Parried:
				{
					if (PressureBias >= Cfg.AbortSuppressBias) { break; } // the floor is in deficit: keep swinging
					// Rule 1: every abort transitions to a defensive or evasive state — never to neutral.
					const EMoveId* Set = InPrevOutcome == EHitOutcome::Whiff ? AbortOnWhiff : AbortOnParried;
					const int32_t SetN = InPrevOutcome == EHitOutcome::Whiff
						? static_cast<int32_t>(sizeof(AbortOnWhiff) / sizeof(AbortOnWhiff[0]))
						: static_cast<int32_t>(sizeof(AbortOnParried) / sizeof(AbortOnParried[0]));
					for (int32_t I = 0; I < SetN; ++I)
					{
						if (!InRange(Set[I], Distance)) { continue; }
						// Legibility (a) binds aborts too: a feint may not be faster than the swing it replaces.
						if (Move(Set[I]).IsAttack() && Sc.IsAttack() && Move(Set[I]).Startup < Sc.Startup) { continue; }
						ScoreCandidate(Set[I], InSlot.Move, 0.f, Distance, IncomingIn, Cands[N]);
						Cands[N].Decision = Cands[N].Score; // outcome-driven: no margin, the abort itself is decided
						++N;
					}
					if (N > 0)
					{
						FBrainDecision Tmp = D;
						Tmp.Scripted = EMoveId::None; // the scripted continuation is not a candidate after an abort
						FinishDecision(Tmp, Cands, N);
						D.Chosen = Tmp.Chosen;
						D.NumTop = Tmp.NumTop;
						for (int32_t I = 0; I < Tmp.NumTop; ++I) { D.Top[I] = Tmp.Top[I]; }
						D.bArgmaxHolds = Tmp.bArgmaxHolds;
						D.Kind = EDecisionKind::LuckyDrawAbort;
						std::snprintf(D.Reason, sizeof(D.Reason), "%s -> abort %s, chose %s", OutcomeName(InPrevOutcome), Sc.Name, Move(D.Chosen).Name);
						return D;
					}
					break;
				}
				case EHitOutcome::Blocked:
				{
					// They are holding guard — on screen. Keep the pressure, with the best anti-guard follow-up
					// that is still legible: unblockables first, then sha-chi drain.
					EMoveId Best = InSlot.Move;
					float BestKey = Payoff(InSlot.Move, ESym::Block) * 1000.f + Sc.ShaChiDrainOnBlock;
					for (int32_t I = 1; I < NumMoves; ++I)
					{
						const FMoveData& M = Move(static_cast<EMoveId>(I));
						if (M.Owner != ESide::Boss || !M.IsAttack() || !InRange(M.Id, Distance)) { continue; }
						if (M.Startup < Sc.Startup) { continue; } // legibility (a)
						const float Key = Payoff(M.Id, ESym::Block) * 1000.f + M.ShaChiDrainOnBlock;
						if (Key > BestKey) { BestKey = Key; Best = M.Id; }
					}
					if (Best != InSlot.Move)
					{
						D.Chosen = Best;
						D.Kind = EDecisionKind::LuckyDrawPress;
						std::snprintf(D.Reason, sizeof(D.Reason), "blocked -> guard pressure %s instead of %s", Move(Best).Name, Sc.Name);
						return D;
					}
					break;
				}
				default:
					break;
				}
			}
		}

		// ---- Threat abort: a swing is visibly on its way and every attack we could throw would be
		// interrupted by it. Rule 1: the abort goes to a defensive / evasive state, never to neutral.
		if (Cfg.bLuckyDraw && IncomingIn >= 0 && InSlot.Type == ESlotType::Attack)
		{
			bool bAnyAttackSurvives = false;
			for (int32_t I = 1; I < NumMoves && !bAnyAttackSurvives; ++I)
			{
				const FMoveData& M = Move(static_cast<EMoveId>(I));
				if (M.Owner != ESide::Boss || !M.IsAttack() || !InRange(M.Id, Distance)) { continue; }
				if (Sc.IsAttack() && M.Startup < Sc.Startup) { continue; }
				if (IncomingThreatPayoff(M, IncomingIn) >= 0.f) { bAnyAttackSurvives = true; }
			}
			if (!bAnyAttackSurvives)
			{
				EMoveId Best = EMoveId::None;
				float BestV = -1.0e30f;
				for (EMoveId Def : AbortOnWhiff)
				{
					const float V = IncomingThreatPayoff(Move(Def), IncomingIn);
					if (V > BestV) { BestV = V; Best = Def; }
				}
				if (Best != EMoveId::None && BestV > 0.f)
				{
					D.Chosen = Best;
					D.Kind = EDecisionKind::LuckyDrawAbort;
					std::snprintf(D.Reason, sizeof(D.Reason), "incoming %s lands in %df, %s would be interrupted -> %s",
						Move(Duel.Get(ESide::Player).Move).Name, IncomingIn, Sc.Name, Move(Best).Name);
					return D;
				}
			}
		}

		// ---- §2.4 standard substitution --------------------------------------------------------------
		if (CapReached())
		{
			D.Kind = EDecisionKind::CapForcedScript;
			std::snprintf(D.Reason, sizeof(D.Reason), "adaptation cap -> script %s", Sc.Name);
			return D;
		}

		const bool bUpgrade = InSlot.Type == ESlotType::Defend && IsWhiffable(PS.Top);
		for (int32_t I = 1; I < NumMoves; ++I)
		{
			const FMoveData& M = Move(static_cast<EMoveId>(I));
			if (M.Owner != ESide::Boss) { continue; }
			const bool bSameType = M.Slot == InSlot.Type;
			const bool bUpgradeAttack = bUpgrade && M.IsAttack();
			if (!bSameType && !bUpgradeAttack) { continue; }
			// Legibility (a): an adaptive attack never has fewer startup frames than its scripted counterpart.
			if (M.IsAttack() && Sc.IsAttack() && M.Startup < Sc.Startup) { continue; }
			if (M.Id != InSlot.Move && M.IsAttack() && !InRange(M.Id, Distance)) { continue; }
			ScoreCandidate(M.Id, InSlot.Move, D.Margin, Distance, IncomingIn, Cands[N]);
			++N;
		}
		FinishDecision(D, Cands, N);

		if (D.Chosen != D.Scripted)
		{
			D.Kind = EDecisionKind::Substitution;
			float ScScore = 0.f;
			float ChScore = 0.f;
			for (int32_t I = 0; I < N; ++I)
			{
				if (Cands[I].Move == D.Scripted) { ScScore = Cands[I].Score; }
				if (Cands[I].Move == D.Chosen) { ChScore = Cands[I].Score; }
			}
			std::snprintf(D.Reason, sizeof(D.Reason), "predicted %s (p=%.2f, ctx=%s,%s) -> chose %s over %s (+%.2f > margin %.2f)",
				SymName(PS.Top), PS.TopP, D.Ctx0 == ESym::Count ? "-" : SymName(D.Ctx0), SymName(D.Ctx1),
				Move(D.Chosen).Name, Sc.Name, ChScore - ScScore, D.Margin);
		}
		else
		{
			D.Kind = EDecisionKind::Script;
			std::snprintf(D.Reason, sizeof(D.Reason), "read %.2f bits (top %s p=%.2f) < margin %.2f -> script %s",
				PS.ReadBits, SymName(PS.Top), PS.TopP, D.Margin, Sc.Name);
		}
		return D;
	}

	// ----------------------------------------------------------------------------------------------
	// Think — once per frame
	// ----------------------------------------------------------------------------------------------

	bool FClassicBrain::ThinkAt(FDuel& Duel, float Distance, FBrainDecision* OutDecision)
	{
		FFighter& B = Duel.Get(ESide::Boss);
		if (Duel.IsOver() || !B.IsActionable() || ScriptLen <= 0) { return false; }

		if (!bSlotOpen)
		{
			CurrentSlot = Script[ScriptIndex];
			CurrentSlotIndex = ScriptIndex;
			bSlotOpen = true;
		}
		const FScriptSlot& S = CurrentSlot;
		const FMoveData& Sc = Move(S.Move);

		const bool bLocomoting = B.State == EFighterState::Acting && B.CurrentMove().Kind == EMoveKind::Locomotion;
		// A new string starts from rest (or out of an approach); chains fire at the cancel frame.
		if (!S.bChain && B.State != EFighterState::Idle && !bLocomoting) { return false; }

		// Spacing — identical in both modes: a non-chain ATTACK slot waits until the scripted move is in range.
		if (S.Type == ESlotType::Attack && !S.bChain && Distance > Sc.Range * Cfg.InRangeFactor)
		{
			if (bLocomoting && B.CurrentMove().Dir == EDir::Forward) { return false; } // keep closing
			Duel.Commit(ESide::Boss, Distance > 650.f ? EMoveId::BDashIn : EMoveId::BApproach);
			return false;
		}

		// Rule (c): habits from the last exchange become usable at the start of the next string.
		if (!S.bChain && Model != nullptr) { Model->Flush(); }
		UpdateFloor(Duel.Frame);

		const EHitOutcome Prev = S.bChain ? LastSwingOutcome : EHitOutcome::None;
		FBrainDecision D = Decide(Duel, S, CurrentSlotIndex, Prev, bOwnDefenceSucceeded, Distance);
		D.FloorCorrection = LastFloorCorrection;
		D.PressureBias = PressureBias;

		// Rule (b): sampled once, at this defined point, and honoured — committed on this same frame.
		if (!Duel.Commit(ESide::Boss, D.Chosen))
		{
			if (!Duel.Commit(ESide::Boss, S.Move)) { return false; }
			D.Chosen = S.Move;
			D.Kind = EDecisionKind::Script;
		}

		if (Move(D.Scripted).IsAttack()) { ++ShadowScriptedSwings; }
		if (Move(D.Chosen).IsAttack()) { ++ShadowChosenSwings; }
		ShadowScriptedFrames += Move(D.Scripted).ActionableFrames();
		ShadowChosenFrames += Move(D.Chosen).ActionableFrames();

		const bool bModelSub = D.Kind == EDecisionKind::Substitution;
		ConsecutiveSubs = bModelSub ? ConsecutiveSubs + 1 : 0;
		RecentSubs[RecentHead] = bModelSub;
		RecentHead = (RecentHead + 1) % (Cfg.SubWindow < 32 ? (Cfg.SubWindow > 0 ? Cfg.SubWindow : 1) : 32);
		if (bModelSub)
		{
			++SubCount;
			PendingCounterMove = D.Chosen;
			PendingCounterPred = D.Predicted;
			PendingCounterConf = D.PredictedP;
		}
		else
		{
			PendingCounterMove = EMoveId::None;
		}

		LastSwingOutcome = EHitOutcome::None;
		bOwnDefenceSucceeded = false;
		ScriptIndex = (ScriptIndex + 1) % ScriptLen;
		bSlotOpen = false;
		++DecisionCount;
		Last = D;
		if (OutDecision != nullptr) { *OutDecision = D; }
		return true;
	}

	void FClassicBrain::OnEvent(const FDuelEvent& E, const FDuel& Duel)
	{
		(void)Duel;
		switch (E.Type)
		{
		case EDuelEvent::Commit:
			if (E.Side == ESide::Boss && Move(E.Move).IsAttack()) { ++SwingCount; }
			break;
		case EDuelEvent::Outcome:
			if (E.Side == ESide::Boss)
			{
				LastSwingOutcome = E.Outcome;
				LastSwingMove = E.Move;
				DamageTotal += E.Damage;
				if (E.Move == PendingCounterMove)
				{
					if (E.Outcome == EHitOutcome::Hit)
					{
						++CounterHits;
						ReadMeter.Frame = E.Frame;
						ReadMeter.Predicted = PendingCounterPred;
						ReadMeter.Confidence = PendingCounterConf;
						ReadMeter.Counter = E.Move;
						bReadMeterReady = true;
					}
					PendingCounterMove = EMoveId::None;
				}
			}
			else if (E.Outcome == EHitOutcome::Blocked || E.Outcome == EHitOutcome::Parried)
			{
				bOwnDefenceSucceeded = true; // the player's swing met the boss's guard / counter
			}
			break;
		default:
			break;
		}
	}

	bool FClassicBrain::PopReadMeter(FReadMeterEvent& Out)
	{
		if (!bReadMeterReady) { return false; }
		Out = ReadMeter;
		bReadMeterReady = false;
		return true;
	}

	float FClassicBrain::SwingsPerMin(int32_t Frame) const
	{
		const float Minutes = static_cast<float>(Frame > 0 ? Frame : 1) / (60.f * FramesPerSecond);
		return static_cast<float>(SwingCount) / Minutes;
	}

	float FClassicBrain::ShadowScriptRate(int32_t Frame) const
	{
		const float ShadowTime = static_cast<float>(Frame) - static_cast<float>(ShadowChosenFrames - ShadowScriptedFrames);
		return ShadowTime > 60.f ? 3600.f * static_cast<float>(ShadowScriptedSwings) / ShadowTime : 0.f;
	}

	float FClassicBrain::DamagePerMin(int32_t Frame) const
	{
		const float Minutes = static_cast<float>(Frame > 0 ? Frame : 1) / (60.f * FramesPerSecond);
		return DamageTotal / Minutes;
	}

	// ----------------------------------------------------------------------------------------------
	// IBossBrain
	// ----------------------------------------------------------------------------------------------

	void FClassicBrain::BeginEncounter(int32_t Seed)
	{
		Reset(Seed);
		Observer.Reset();
		Symbols.clear();
		if (Model != nullptr) { Model->Flush(); }
	}

	bool FClassicBrain::Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision)
	{
		return ThinkAt(Duel, Geo.Distance(), OutDecision);
	}

	void FClassicBrain::OnFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement)
	{
		(void)Geo;
		for (const FDuelEvent& E : Events) { OnEvent(E, Duel); }
		Observer.ProcessFrame(Events, Duel, PlayerMovement, Model, bRecordSymbols ? &Symbols : nullptr);
	}

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
							// A bait shows one impact and delivers another: a press nearer the bait was timed to it.
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
}
