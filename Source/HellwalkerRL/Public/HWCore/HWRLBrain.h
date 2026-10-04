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
//
// Difficulty and identity (ObsLayoutVersion 3): Configure(Skill, Identity) before a fight. The same network plays every
// keeper at every skill; lower skill sees you later and strings less (RL::SkillParams). Optionally (the easiest
// difficulties) it samples its action at a temperature instead of taking the argmax — seeded by the fight, so still
// reproducible.
//
// The notebook (FRLNotebook): what the keeper would write down about you — what you actually answered to each kind
// of move, and how often its read head called it right. Ground truth gathered for the player to see; never an input.

#pragma once

#include "HWRLObserver.h"
#include "HWRLPolicy.h"
#include "HWRandom.h"

namespace HW
{
	/** What the keepers learned about you this session (the notebook screen). Filled by FRLBrain; never fed back. */
	struct FRLNotebook
	{
		/** Boss move classes = the boss symbols BFast .. BRetreat (the history token's Boss field 1..8). */
		static constexpr int32_t Classes = 8;
		static constexpr int32_t MaxFights = 16;
		/** What the player answered, by the class of the keeper's move (RL::NumAnswerClasses columns). */
		int32_t Answers[Classes][NumPlayerSymbols] = {};
		/** What the read head predicted, same layout (its top answer for the action it took). */
		int32_t Predicted[Classes][NumPlayerSymbols] = {};
		int32_t Predictions = 0;            // decisions (not Waits) whose answer is known
		int32_t Correct = 0;                // ... the read head's top answer was right
		int32_t Confident = 0;              // ... it was at least RL::ReadMeterMinP sure
		int32_t ConfidentCorrect = 0;
		int32_t SwingPredictions = 0;       // ... the call was on one of its ATTACKS (BFast .. BKiller): how you answer a swing
		int32_t SwingCorrect = 0;           //     is the read that decides fights (FRLInsight's input)
		int32_t FightPredictions[MaxFights] = {}; // per fight of the session (index clamped)
		int32_t FightCorrect[MaxFights] = {};
		int32_t ReadsLanded = 0;            // READ counters (a confident, correct read that a hit landed on)
		int32_t Fights = 0;

		void Reset() { *this = FRLNotebook{}; }
		/** Boss move class index (0..7) of a move, -1 for none. */
		static int32_t ClassOf(EMoveId M);
		float Accuracy() const { return Predictions > 0 ? static_cast<float>(Correct) / static_cast<float>(Predictions) : 0.f; }
	};

	/**
	 * Insight: how well the keeper actually predicts YOU, in [0, 1] — the learning arc of the Adaptive AI mode. It starts
	 * at 0 each session and, after every fight, moves (an exponential moving average) toward that fight's read-head top-1
	 * accuracy, normalised between what it scores against a near-random player and against one it has figured out.
	 * Repeat one trick and its calls come true, so Insight climbs over ~4-5 fights; change tricks and its calls go wrong,
	 * so Insight decays (explicit, gradual forgetting). A fight with few calls moves it little.
	 *
	 * KeeperConfig turns Insight into the next fight's keeper: skill from the difficulty's Lo to Hi, sampling temperature
	 * (it plays loosely while it does not know you, greedily once it does) and the breather between attack strings (gone
	 * well before full Insight). All three are fixed for a whole fight. Session state like FRLSession: kept keeper to
	 * keeper, dropped on quit and when the memory is reset. Never an input of the network.
	 */
	struct FRLInsight
	{
		// ---- AfterFight: the moving average (tuned with ThesisSim --arc against the shipped keeper) ----------------------
		/** Read-head top-1 accuracy that means "it does not know you" (normalised target 0) — about what it scores against a
		 *  near-random player — and the accuracy that means "it knows you" (target 1). */
		static constexpr float AccuracyFloor = 0.40f;
		static constexpr float AccuracyFull = 0.70f;
		/** The target never asks for more than this: Insight saturates below 1 (one surprising fight always shows). */
		static constexpr float MaxInsight = 0.95f;
		/** Per-fight step toward the target when it is above Insight (learning) / below it (forgetting). */
		static constexpr float RiseRate = 0.45f;
		static constexpr float FallRate = 0.55f;
		/** A fight with this many calls moves Insight half as far as a long fight: weight = N / (N + this). */
		static constexpr float HalfWeightPredictions = 8.f;

		// ---- KeeperConfig: what Insight buys the keeper -----------------------------------------------------------------
		/** Sampling temperature at Insight 0 (loose, readable play), falling linearly to 0 (greedy) at TemperatureGoneAt. */
		static constexpr float MaxTemperature = 2.5f;
		static constexpr float TemperatureGoneAt = 0.7f;
		/** The breather at Insight 0 (frames from one keeper attack's commit to its next opener: 3.5 s, it watches before it
		 *  presses — 2.5x Easy's), falling
		 *  linearly to none at SwingGapGoneAt. */
		static constexpr int32_t MaxSwingGap = 210;
		static constexpr float SwingGapGoneAt = 0.6f;

		float   Insight = 0.f;
		int32_t Fights = 0;     // fights recorded since the last Reset

		void Reset() { *this = FRLInsight{}; }
		/** After a fight: its read-head calls on its ATTACKS whose answer is known, and how many were right — the deltas of
		 *  FRLNotebook::SwingPredictions / SwingCorrect since the last record, taken after FRLBrain::FlushNotebook (as the
		 *  game's UHWSessionSubsystem and ThesisSim --arc do). AccuracyFloor / AccuracyFull were tuned on these, not on
		 *  all calls (FightPredictions), which score higher and would saturate Insight for everyone. */
		void AfterFight(int32_t Predictions, int32_t Correct);
		/** The next fight's keeper for a difficulty's skill range: skill Lo..Hi with Insight, temperature, breather gap. */
		void KeeperConfig(float SkillLo, float SkillHi, float& OutSkill, float& OutTemperature, int32_t& OutSwingGap) const;
	};

	class FRLBrain : public IBossBrain
	{
	public:
		/** Policy: shared and read-only (one per game). Session: the player's memory (one per game session).
		 *  Notebook (optional): what it learned, for the player to read. */
		void Bind(const FRLPolicy* InPolicy, FRLSession* InSession, FRLNotebook* InNotebook = nullptr)
		{
			Policy = InPolicy;
			Session = InSession;
			Notebook = InNotebook;
		}
		bool IsReady() const { return Policy != nullptr && Policy->IsBossPlayable() && Session != nullptr; }
		FRLObserver& Observer() { return Obs; }
		const FRLObserver& Observer() const { return Obs; }
		/** Difficulty in [0, 1] and keeper identity (RL::EKeeper), applied at the next fight's start. MinSwingGap: the
		 *  Easy breather (frames from one attack's commit to the next opener, FRLConfig::MinSwingGap; 0 = off). */
		void Configure(float Skill, int32_t Identity, int32_t MinSwingGap = 0)
		{
			Obs.Config.Skill = Skill;
			Obs.Config.Identity = Identity;
			Obs.Config.MinSwingGap = MinSwingGap;
		}
		/** 0 = greedy (the default, B1 determinism); > 0 = sample at this temperature (seeded by the fight). */
		void SetTemperature(float T) { Temperature = T > 0.f ? T : 0.f; }
		float GetTemperature() const { return Temperature; }

		// IBossBrain
		EBrainMode Mode() const override { return EBrainMode::Hellwalker; }
		const char* Name() const override { return "Hellwalker (RL)"; }
		void BeginEncounter(int32_t Seed) override;
		bool Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision) override;
		void OnFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement) override;
		bool PopReadMeter(FReadMeterEvent& Out) override;
		const FBrainDecision& LastDecision() const override { return Last; }
		int32_t Decisions() const override { return DecisionCount; }
		int32_t CountersLanded() const override { return Obs.ReadCountersLanded(); }

		/** The last forward pass (F3 overlay: probabilities over all actions, the value). */
		const FRLPolicyOutput& LastOutput() const { return LastOut; }
		/** The read head's distribution for the chosen action at the last decision (F3 overlay). */
		const float* LastAnswerProbs() const { return LastAux; }
		/** Record the last decision's answer in the notebook now (call when a fight ends; the next fight does it too). */
		void FlushNotebook();

	private:
		const FRLPolicy* Policy = nullptr;
		FRLSession* Session = nullptr;
		FRLNotebook* Notebook = nullptr;
		FRLObserver Obs;
		FRandom Rng;
		float Temperature = 0.f;
		bool bBegun = false;
		FDuelGeometry BeginGeo;
		int32_t DecisionCount = 0;
		FBrainDecision Last;
		FRLPolicyOutput LastOut;
		float LastAux[FRLPolicyOutput::MaxAux] = {};
		float ObsBuf[RL::ObsDim] = {};
		int8_t TokBuf[RL::HistoryTokens * RL::TokenFields] = {};
		uint8_t MaskBuf[RL::NumActions] = {};
		std::vector<float> Scratch;            // the forward pass's own scratch (sized once): brains on several threads may
		                                       // share one const policy (the evaluation tools)
		// The prediction made at the last decision, waiting for its answer (the notebook).
		int32_t PendingClass = -1;
		ESym    PendingPredicted = ESym::Neutral;
		float   PendingP = 0.f;
		int32_t PendingFight = 0;
	};
}
