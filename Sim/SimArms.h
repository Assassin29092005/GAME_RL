// HellwalkerRL — tools only. The simulator's "arms": one boss brain plus its session memory, run against simulated
// players. Pathbreaker is the game's FScriptBrain; the adaptive arm is the RL keeper (FRLBrain + an exported policy)
// or the classic reference brain (FClassicBrain + a playstyle model), so B0 compares any of them on identical seeds.

#pragma once

#include "HWCore/HWScriptBrain.h"
#include "HWCore/HWSim.h"

#include <memory>
#include <vector>

namespace HW
{
	class FSimArm
	{
	public:
		virtual ~FSimArm() = default;
		virtual const char* Label() const = 0;
		/** Forget the player: a new session (new playstyle model / new recurrent memory). */
		virtual void NewSession() = 0;
		/** One encounter on the current session. */
		virtual FEncounterStats Run(const FRunConfig& Cfg, std::vector<FExchangeRow>* Rows = nullptr) = 0;
		/** The classic brain's floor controller target (the script's measured swing rate). Others ignore it. */
		virtual void SetReferenceSwingsPerMin(float Spm) { (void)Spm; }
	};

	class FScriptArm : public FSimArm
	{
	public:
		explicit FScriptArm(int32_t ScriptIndex) { Brain.SetScriptIndex(ScriptIndex); }
		const char* Label() const override { return "Pathbreaker"; }
		void NewSession() override {}
		FEncounterStats Run(const FRunConfig& Cfg, std::vector<FExchangeRow>* Rows) override { return RunEncounter(Brain, Cfg, Rows); }

	private:
		FScriptBrain Brain;
	};

	/**
	 * "classic": the reference project's tally brain (benchmark). "rl": the RL keeper with the weights in PolicyPath.
	 * Returns nullptr (and prints why) if the arm cannot be built.
	 */
	std::unique_ptr<FSimArm> MakeAdaptiveArm(const char* Name, const char* PolicyPath, int32_t ScriptIndex, bool bDerivedPayoffs,
		float KeeperSkill = 1.f, int32_t KeeperIdentity = -1);

	/** Tests beyond the game core and the classic brain (the RL keeper's), run by ThesisSim --tests. Returns the count. */
	int32_t RunExtraTests(int32_t& InOutFailed);
}
