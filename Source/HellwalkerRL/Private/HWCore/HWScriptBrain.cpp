// HellwalkerRL — engine-free core. Pathbreaker implementation.

#include "HWCore/HWScriptBrain.h"

#include <cstdio>

namespace HW
{
	FScriptBrain::FScriptBrain()
	{
		SetScriptIndex(0);
		BeginEncounter(0);
	}

	void FScriptBrain::SetScript(const FScriptSlot* Slots, int32_t Count)
	{
		ScriptLen = Count < MaxScript ? Count : MaxScript;
		for (int32_t I = 0; I < ScriptLen; ++I) { Script[I] = Slots[I]; }
		ScriptIndex = 0;
		bSlotOpen = false;
	}

	void FScriptBrain::SetScriptIndex(int32_t Index)
	{
		const FScriptSlot* Slots = nullptr;
		const int32_t N = BossScript(Index, Slots);
		SetScript(Slots, N);
	}

	void FScriptBrain::BeginEncounter(int32_t Seed)
	{
		(void)Seed; // the script has no randomness at all
		ScriptIndex = 0;
		bSlotOpen = false;
		CurrentSlotIndex = 0;
		SwingCount = 0;
		ScriptedSwings = 0;
		DecisionCount = 0;
		Last = FBrainDecision{};
	}

	bool FScriptBrain::Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision)
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
		const float Distance = Geo.Distance();

		const bool bLocomoting = B.State == EFighterState::Acting && B.CurrentMove().Kind == EMoveKind::Locomotion;
		// A new string starts from rest (or out of an approach); chains fire at the cancel frame.
		if (!S.bChain && B.State != EFighterState::Idle && !bLocomoting) { return false; }

		// Spacing: a non-chain ATTACK slot waits until the scripted move is in range.
		if (S.Type == ESlotType::Attack && !S.bChain && Distance > Sc.Range * InRangeFactor)
		{
			if (bLocomoting && B.CurrentMove().Dir == EDir::Forward) { return false; } // keep closing
			Duel.Commit(ESide::Boss, Distance > 650.f ? EMoveId::BDashIn : EMoveId::BApproach);
			return false;
		}

		FBrainDecision D;
		D.Frame = Duel.Frame;
		D.ScriptIndex = CurrentSlotIndex;
		D.Slot = S.Type;
		D.bChain = S.bChain;
		D.Scripted = S.Move;
		D.Chosen = S.Move;
		D.Kind = EDecisionKind::Script;
		D.FrameAdvantage = Duel.FrameAdvantage(ESide::Boss);
		std::snprintf(D.Reason, sizeof(D.Reason), "script %s", Sc.Name);

		if (!Duel.Commit(ESide::Boss, S.Move)) { return false; }

		if (Sc.IsAttack()) { ++ScriptedSwings; }
		ScriptIndex = (ScriptIndex + 1) % ScriptLen;
		bSlotOpen = false;
		++DecisionCount;
		Last = D;
		if (OutDecision != nullptr) { *OutDecision = D; }
		return true;
	}

	void FScriptBrain::OnFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement)
	{
		(void)Duel; (void)Geo; (void)PlayerMovement;
		for (const FDuelEvent& E : Events)
		{
			if (E.Type == EDuelEvent::Commit && E.Side == ESide::Boss && Move(E.Move).IsAttack()) { ++SwingCount; }
		}
	}

	float FScriptBrain::ShadowScriptRate(int32_t Frame) const
	{
		// The script is its own shadow: its swings over the elapsed time.
		return Frame > 60 ? 3600.f * static_cast<float>(ScriptedSwings) / static_cast<float>(Frame) : 0.f;
	}
}
