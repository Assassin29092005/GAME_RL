// HellwalkerRL — engine-free core. The RL keeper: observe -> mask -> forward -> greedy action (RL/DESIGN.md §6).

#include "HWCore/HWRLBrain.h"

#include <cmath>
#include <cstdio>

namespace HW
{
	int32_t FRLNotebook::ClassOf(EMoveId M)
	{
		if (M == EMoveId::None) { return -1; }
		const int32_t C = SymIndex(Move(M).Symbol) - SymIndex(ESym::BFast);
		return C >= 0 && C < Classes ? C : -1;
	}

	void FRLInsight::AfterFight(int32_t Predictions, int32_t Correct)
	{
		++Fights;
		if (Predictions <= 0) { return; } // nothing it could have been right or wrong about
		const float N = static_cast<float>(Predictions);
		float Acc = static_cast<float>(Correct) / N;
		Acc = Acc < 0.f ? 0.f : (Acc > 1.f ? 1.f : Acc);
		float Norm = (Acc - AccuracyFloor) / (AccuracyFull - AccuracyFloor);
		Norm = Norm < 0.f ? 0.f : (Norm > 1.f ? 1.f : Norm);
		const float Target = MaxInsight * Norm;
		const float Rate = (Target >= Insight ? RiseRate : FallRate) * N / (N + HalfWeightPredictions);
		Insight += Rate * (Target - Insight);
		Insight = Insight < 0.f ? 0.f : (Insight > MaxInsight ? MaxInsight : Insight);
	}

	void FRLInsight::KeeperConfig(float SkillLo, float SkillHi, float& OutSkill, float& OutTemperature, int32_t& OutSwingGap) const
	{
		const float I = Insight < 0.f ? 0.f : (Insight > 1.f ? 1.f : Insight);
		OutSkill = SkillLo + (SkillHi - SkillLo) * I;
		const float Loose = 1.f - I / TemperatureGoneAt;
		OutTemperature = Loose > 0.f ? MaxTemperature * Loose : 0.f;
		const float Breathe = 1.f - I / SwingGapGoneAt;
		OutSwingGap = Breathe > 0.f ? static_cast<int32_t>(static_cast<float>(MaxSwingGap) * Breathe + 0.5f) : 0;
	}

	void FRLBrain::FlushNotebook()
	{
		if (Notebook == nullptr || PendingClass < 0) { PendingClass = -1; return; }
		// The window of the last decision is complete once the next decision (or the fight's end) comes: ground truth.
		const int32_t Label = Obs.AnswerToLastDecision();
		if (Label >= 0 && Label < NumPlayerSymbols)
		{
			FRLNotebook& N = *Notebook;
			++N.Answers[PendingClass][Label];
			++N.Predicted[PendingClass][SymIndex(PendingPredicted)];
			const bool bRight = SymIndex(PendingPredicted) == Label;
			++N.Predictions;
			N.Correct += bRight ? 1 : 0;
			if (PendingP >= RL::ReadMeterMinP)
			{
				++N.Confident;
				N.ConfidentCorrect += bRight ? 1 : 0;
			}
			if (PendingClass <= SymIndex(ESym::BKiller) - SymIndex(ESym::BFast))
			{
				++N.SwingPredictions;
				N.SwingCorrect += bRight ? 1 : 0;
			}
			const int32_t F = PendingFight < FRLNotebook::MaxFights ? PendingFight : FRLNotebook::MaxFights - 1;
			++N.FightPredictions[F];
			N.FightCorrect[F] += bRight ? 1 : 0;
		}
		PendingClass = -1;
	}

	bool FRLBrain::PopReadMeter(FReadMeterEvent& Out)
	{
		if (!Obs.PopReadMeter(Out)) { return false; }
		if (Notebook != nullptr) { ++Notebook->ReadsLanded; }
		return true;
	}

	void FRLBrain::BeginEncounter(int32_t Seed)
	{
		// Greedy play (RL.md §8) has no randomness; the easiest difficulties sample, seeded by the fight (reproducible).
		Rng.Initialize(static_cast<int32_t>(static_cast<uint32_t>(Seed) * 31u + 7u)); // unsigned: no signed overflow
		FlushNotebook(); // the previous fight's last decision (the observer still holds its window)
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
			if (Notebook != nullptr) { ++Notebook->Fights; }
		}
		// IsReady() requires a playable boss policy (a player-side exploiter, another layout, or a memory bigger than a
		// session holds never plays; the game checks the same before binding this brain and falls back to the script).
		if (!IsReady() || !Obs.IsDecisionPoint(Duel)) { return false; }
		const int32_t H = Policy->HiddenSize();

		Obs.BuildObservation(Duel, ObsBuf, TokBuf);
		Obs.BuildMask(Duel, MaskBuf);
		if (Session->HiddenSize != H)
		{
			for (float& V : Session->Hidden) { V = 0.f; }
			Session->HiddenSize = H;
		}
		FlushNotebook(); // the previous decision's window is complete now
		if (static_cast<int32_t>(Scratch.size()) < Policy->ScratchSize()) { Scratch.assign(static_cast<size_t>(Policy->ScratchSize()), 0.f); }
		Policy->Forward(ObsBuf, TokBuf, MaskBuf, Session->Hidden, Session->Hidden, LastOut, Scratch.data());
		int32_t A = LastOut.Argmax;
		if (Temperature > 0.f)
		{
			// Sample from softmax(logits / T) over the allowed actions (the trained policy is a sampler; greedy is its
			// sharpest form). One draw per decision from the fight's stream.
			float W[FRLPolicyOutput::MaxActions] = {};
			float Sum = 0.f;
			const float Best = LastOut.Argmax >= 0 ? LastOut.Logits[LastOut.Argmax] : 0.f;
			for (int32_t I = 0; I < RL::NumActions; ++I)
			{
				W[I] = MaskBuf[I] != 0 ? std::exp((LastOut.Logits[I] - Best) / Temperature) : 0.f;
				Sum += W[I];
			}
			float R = Rng.FRand() * Sum;
			for (int32_t I = 0; I < RL::NumActions && Sum > 0.f; ++I)
			{
				if (W[I] <= 0.f) { continue; }
				A = I;
				if (R < W[I]) { break; }
				R -= W[I];
			}
		}
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
		std::snprintf(D.Reason, sizeof(D.Reason), "policy %s p=%.2f%s | read %s %.2f | V %+.2f%s%s", RL::ActionName(A),
			LastOut.Probs[A], Alt, SymName(Read.Predicted), Read.P, LastOut.Value, A != LastOut.Argmax ? " | sampled" : "",
			D.bArgmaxHolds ? "" : " | refused -> Wait");

		// The notebook waits for this decision's answer (Waits teach it nothing about your answers to its moves).
		PendingClass = FRLNotebook::ClassOf(Taken);
		PendingPredicted = Read.Predicted;
		PendingP = Read.P;
		PendingFight = Session->FightIndex;

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
