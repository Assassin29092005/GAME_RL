// HellwalkerRL — engine-free core. Pathbreaker: the boss script, played verbatim (PLAN §2.4, B1).
//
// The control arm. The SCRIPT owns whether, when and what the boss does; outcomes, habits and the player change
// nothing. The only rule besides the script is spacing: a string-opening attack waits (approaching) until its move is
// in range. This is exactly the reference brain's Pathbreaker mode, so B0's control numbers carry over unchanged.

#pragma once

#include "HWBrain.h"

namespace HW
{
	class FScriptBrain : public IBossBrain
	{
	public:
		FScriptBrain();

		void SetScript(const FScriptSlot* Slots, int32_t Count);
		/** Convenience: HW::BossScript(Index). */
		void SetScriptIndex(int32_t Index);
		int32_t ScriptLength() const { return ScriptLen; }
		const FScriptSlot& ScriptAt(int32_t I) const { return Script[I % ScriptLen]; }

		/** A non-chain ATTACK slot waits until Distance <= Range * this. */
		float InRangeFactor = 0.9f;

		// IBossBrain
		EBrainMode Mode() const override { return EBrainMode::Pathbreaker; }
		const char* Name() const override { return "Pathbreaker (script)"; }
		void BeginEncounter(int32_t Seed) override;
		bool Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision) override;
		void OnFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement) override;
		bool PopReadMeter(FReadMeterEvent& Out) override { (void)Out; return false; }
		const FBrainDecision& LastDecision() const override { return Last; }
		int32_t Decisions() const override { return DecisionCount; }
		int32_t CountersLanded() const override { return 0; }
		float ShadowScriptRate(int32_t Frame) const override;

		int32_t Swings() const { return SwingCount; }

	private:
		static constexpr int32_t MaxScript = 64;
		FScriptSlot Script[MaxScript];
		int32_t ScriptLen = 0;
		int32_t ScriptIndex = 0;

		bool        bSlotOpen = false;
		FScriptSlot CurrentSlot;
		int32_t     CurrentSlotIndex = 0;

		int32_t SwingCount = 0;
		int32_t ScriptedSwings = 0;
		int32_t DecisionCount = 0;
		FBrainDecision Last;
	};
}
