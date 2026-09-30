// HellwalkerRL — engine-free core. The RL keeper: observe -> mask -> forward -> greedy action (RL/DESIGN.md §6).

#include "HWCore/HWRLBrain.h"

#include <cmath>
#include <cstdio>

namespace HW
{
	void FRLBrain::BeginEncounter(int32_t Seed)
	{
		(void)Seed; // greedy play (RL.md §8): the keeper has no randomness at all; the session carries its memory
		bBegun = false;
		DecisionCount = 0;
		Last = FBrainDecision{};
		LastOut = FRLPolicyOutput{};
		for (float& P : LastAux) { P = 0.f; }
	}

	bool FRLBrain::Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision)
	{
		if (Session == nullptr) { return false; }
		if (!bBegun)
		{
			// The first frame of the fight: the observer starts recording even without a policy, so a policy bound
			// mid-fight still sees a consistent history.
			Obs.BeginEncounter(Session, Duel, Geo);
			BeginGeo = Geo;
			bBegun = true;
		}
		if (!IsReady() || !Obs.IsDecisionPoint(Duel)) { return false; }
		// A policy this observer cannot feed (a player-side exploiter, or a memory bigger than the session holds) never
		// plays: refusing is safer than reading past a buffer.
		const int32_t H = Policy->HiddenSize();
		if (Policy->Side() != 0 || Policy->ObsDim() != RL::ObsDim || Policy->NumActions() != RL::NumActions
			|| H <= 0 || H > FRLSession::MaxHidden)
		{
			return false;
		}

		Obs.BuildObservation(Duel, ObsBuf, TokBuf);
		Obs.BuildMask(Duel, MaskBuf);
		if (Session->HiddenSize != H)
		{
			for (float& V : Session->Hidden) { V = 0.f; }
			Session->HiddenSize = H;
		}
		Policy->Forward(ObsBuf, TokBuf, MaskBuf, Session->Hidden, Session->Hidden, LastOut);
		int32_t A = LastOut.Argmax;
		if (A < 0 || A >= RL::NumActions) { A = RL::ActionWait; }

		// The read head, for the action about to be taken: what it expects the player to answer.
		const int32_t NAux = Policy->AuxClasses() < FRLPolicyOutput::MaxAux ? Policy->AuxClasses() : FRLPolicyOutput::MaxAux;
		for (float& P : LastAux) { P = 0.f; }
		Policy->Aux(Session->Hidden, A, LastAux);
		int32_t BestAux = 0;
		float Entropy = 0.f;
		for (int32_t C = 0; C < NAux; ++C)
		{
			if (LastAux[C] > LastAux[BestAux]) { BestAux = C; }
			if (LastAux[C] > 0.f) { Entropy -= LastAux[C] * std::log2(LastAux[C]); }
		}
		FRLRead Read;
		Read.Predicted = BestAux < NumPlayerSymbols ? static_cast<ESym>(BestAux) : ESym::Neutral;
		Read.P = LastAux[BestAux];

		FBrainDecision D;
		D.Frame = Duel.Frame;
		D.ScriptIndex = -1;
		D.Kind = EDecisionKind::Policy;
		D.Scripted = EMoveId::None;
		D.bChain = ObsBuf[RL::ObsSelfChainWindow] > 0.5f;
		for (int32_t O = 0; O < 5; ++O)
		{
			if (ObsBuf[RL::ObsSelfLastOutcome + O] > 0.5f) { D.PrevOutcome = static_cast<EHitOutcome>(O); }
		}
		D.FrameAdvantage = Duel.FrameAdvantage(ESide::Boss);
		D.ReadBits = NAux > 1 ? std::log2(static_cast<float>(NAux)) - Entropy : 0.f;
		D.Predicted = Read.Predicted;
		D.PredictedP = Read.P;
		D.Value = LastOut.Value;

		// The three most probable allowed actions (ties: the lower index, as the argmax).
		int32_t Picked[3] = { -1, -1, -1 };
		for (int32_t K = 0; K < 3; ++K)
		{
			int32_t Best = -1;
			for (int32_t I = 0; I < RL::NumActions; ++I)
			{
				if (MaskBuf[I] == 0 || I == Picked[0] || I == Picked[1]) { continue; }
				if (Best < 0 || LastOut.Probs[I] > LastOut.Probs[Best]) { Best = I; }
			}
			if (Best < 0) { break; }
			Picked[K] = Best;
			FCandidateScore& C = D.Top[D.NumTop++];
			C.Move = RL::ActionMove(Best); // None = Wait
			C.Expected = LastOut.Logits[Best];
			C.Score = LastOut.Probs[Best];
			C.Decision = LastOut.Probs[Best];
		}

		const int32_t IllegalBefore = Obs.IllegalActions();
		D.Chosen = Obs.ApplyAction(Duel, A, &Read);
		D.bArgmaxHolds = Obs.IllegalActions() == IllegalBefore;
		const EMoveId Taken = RL::ActionMove(Obs.LastAction()); // None for Wait; the walk itself for a continue
		if (Taken == EMoveId::None) { D.Slot = ESlotType::Reposition; }
		else
		{
			const FMoveData& M = Move(Taken);
			D.Slot = M.IsAttack() ? ESlotType::Attack
				: ((M.Kind == EMoveKind::Guard || M.Kind == EMoveKind::Counter) ? ESlotType::Defend : ESlotType::Reposition);
		}

		const int32_t Second = D.NumTop > 1 ? (Picked[0] == A ? Picked[1] : Picked[0]) : -1;
		char Alt[48] = {};
		if (Second >= 0) { std::snprintf(Alt, sizeof(Alt), " (%s %.2f)", RL::ActionName(Second), LastOut.Probs[Second]); }
		std::snprintf(D.Reason, sizeof(D.Reason), "policy %s p=%.2f%s | read %s %.2f | V %+.2f%s", RL::ActionName(A),
			LastOut.Probs[A], Alt, SymName(Read.Predicted), Read.P, LastOut.Value, D.bArgmaxHolds ? "" : " | refused -> Wait");

		++DecisionCount;
		Last = D;
		if (OutDecision != nullptr) { *OutDecision = D; }
		return true;
	}

	void FRLBrain::OnFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement)
	{
		if (bBegun) { Obs.RecordFrame(Events, Duel, Geo, PlayerMovement); }
	}
}
