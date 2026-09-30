// HellwalkerRL — engine-free core. The RL keeper: the game's adaptive boss (RL.md §8).
//
// A frozen policy (FRLPolicy, trained offline from random initialisation) + the observer shared with the training
// environment (FRLObserver) + the session's memory (FRLSession: the recurrent state and the perceived exchange
// history, carried from keeper to keeper and dropped on quit). It "learns you" inside that memory — there is no
// counting, no payoff table and no script in here.
//
// At each decision point: observe -> mask -> forward -> the greedy (argmax) action -> commit. Deterministic: the same
// weights, session and fight give the same decisions (B1). The read head's prediction for the chosen action fills
// the decision record (Predicted / PredictedP / ReadBits) and, when a hit lands on the answer it predicted with
// confidence, the Read Meter (testimony, not telegraph).

#pragma once

#include "HWRLObserver.h"
#include "HWRLPolicy.h"

namespace HW
{
	class FRLBrain : public IBossBrain
	{
	public:
		/** Policy: shared and read-only (one per game). Session: the player's memory (one per game session). */
		void Bind(const FRLPolicy* InPolicy, FRLSession* InSession) { Policy = InPolicy; Session = InSession; }
		bool IsReady() const { return Policy != nullptr && Policy->IsLoaded() && Session != nullptr; }
		FRLObserver& Observer() { return Obs; }
		const FRLObserver& Observer() const { return Obs; }

		// IBossBrain
		EBrainMode Mode() const override { return EBrainMode::Hellwalker; }
		const char* Name() const override { return "Hellwalker (RL)"; }
		void BeginEncounter(int32_t Seed) override;
		bool Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision) override;
		void OnFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement) override;
		bool PopReadMeter(FReadMeterEvent& Out) override { return Obs.PopReadMeter(Out); }
		const FBrainDecision& LastDecision() const override { return Last; }
		int32_t Decisions() const override { return DecisionCount; }
		int32_t CountersLanded() const override { return Obs.ReadCountersLanded(); }

		/** The last forward pass (F3 overlay: probabilities over all actions, the value). */
		const FRLPolicyOutput& LastOutput() const { return LastOut; }
		/** The read head's distribution for the chosen action at the last decision (F3 overlay). */
		const float* LastAnswerProbs() const { return LastAux; }

	private:
		const FRLPolicy* Policy = nullptr;
		FRLSession* Session = nullptr;
		FRLObserver Obs;
		bool bBegun = false;
		FDuelGeometry BeginGeo;
		int32_t DecisionCount = 0;
		FBrainDecision Last;
		FRLPolicyOutput LastOut;
		float LastAux[FRLPolicyOutput::MaxAux] = {};
		float ObsBuf[RL::ObsDim] = {};
		int8_t TokBuf[RL::HistoryTokens * RL::TokenFields] = {};
		uint8_t MaskBuf[RL::NumActions] = {};
	};
}
