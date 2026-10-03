// HellwalkerRL — tools only. RL-4: the exploiter player's senses and hands, and the driver that plays a trained exploiter
// in the keeper's env (see HWRLPlayer.h).

#include "HWRLPlayer.h"

#include <cmath>
#include <cstring>

namespace HW
{
	namespace RLPlayerImpl
	{
		inline float Clip(float V, float Lo, float Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
		inline float Flag(bool B) { return B ? 1.f : 0.f; }
		inline int32_t Ring(int32_t F, int32_t N) { const int32_t M = F % N; return M < 0 ? M + N : M; }
		inline float Len(float X, float Y) { return std::sqrt(X * X + Y * Y); }
		inline int32_t PhaseIndex(const FMoveData& M, int32_t T)
		{
			if (!M.IsAttack()) { return 3; }
			if (T < M.Startup) { return 0; }
			if (T < M.Startup + M.Active) { return 1; }
			return 2;
		}
		/** Health fraction; immortal pools (1e9) read as a normal pool that refills (as the keeper's observer does). */
		inline float HealthFraction(float Health, float HealthMax, float RealMax)
		{
			if (HealthMax <= 0.f) { return 0.f; }
			if (HealthMax < 1.0e8f || RealMax <= 0.f) { return Clip(Health / HealthMax, 0.f, 1.f); }
			const float Lost = HealthMax - Health;
			return Clip(1.f - std::fmod(Lost < 0.f ? 0.f : Lost, RealMax) / RealMax, 0.f, 1.f);
		}
	}

	namespace RLPlayer
	{
		const char* ActionName(int32_t A)
		{
			static const char* Names[NumActions] = { "Hold", "WalkF", "WalkB", "WalkL", "WalkR", "Guard",
				"Light", "Heavy", "Parry", "StepF", "StepB", "StepL", "StepR", "Switch" };
			return (A >= 0 && A < NumActions) ? Names[A] : "?";
		}

		const char* ObsFeatureName(int32_t I)
		{
			static const char* Fixed[ObsDim] = {};
			static bool bInit = false;
			if (!bInit)
			{
				// Names for the scalar slots; one-hot blocks get their block name (enough for logs).
				for (const char*& N : Fixed) { N = "?"; }
				auto Block = [](int32_t From, int32_t Count, const char* Name) { for (int32_t K = 0; K < Count; ++K) { Fixed[From + K] = Name; } };
				Fixed[ObsHealth] = "self.health"; Fixed[ObsShaChi] = "self.shachi";
				Block(ObsState, 7, "self.state"); Block(ObsMove, 13, "self.move"); Block(ObsPhase, 4, "self.phase");
				Fixed[ObsProgress] = "self.progress"; Fixed[ObsUntilActionable] = "self.until_actionable";
				Fixed[ObsGuardHeld] = "self.guard_held"; Fixed[ObsGuarding] = "self.guarding"; Fixed[ObsParryLive] = "self.parry_live";
				Fixed[ObsInvulnerable] = "self.invulnerable"; Block(ObsWeapon, 2, "self.weapon"); Fixed[ObsChain] = "self.chain";
				Block(ObsLastOutcome, 5, "self.last_outcome"); Fixed[ObsWalkF] = "self.walk_f"; Fixed[ObsWalkL] = "self.walk_l";
				Fixed[ObsKHealth] = "keeper.health"; Fixed[ObsKShaChi] = "keeper.shachi"; Block(ObsKState, 7, "keeper.state");
				Block(ObsKMove, RL::NumBossMoves, "keeper.move"); Block(ObsKPhase, 4, "keeper.phase"); Fixed[ObsKProgress] = "keeper.progress";
				Fixed[ObsKUntilActionable] = "keeper.until_actionable"; Fixed[ObsKArmor] = "keeper.armor";
				Fixed[ObsKInvulnerable] = "keeper.invulnerable"; Block(ObsKLastOutcome, 5, "keeper.last_outcome");
				Fixed[ObsDistance] = "geo.distance"; Fixed[ObsRadialVel] = "geo.radial_vel"; Fixed[ObsLateralVel] = "geo.lateral_vel";
				Fixed[ObsSwingSin] = "geo.swing_sin"; Fixed[ObsSwingCos] = "geo.swing_cos"; Fixed[ObsFrameAdvantage] = "ctx.frame_advantage";
				Fixed[ObsFightTime] = "ctx.fight_time"; Fixed[ObsFightIndex] = "ctx.fight_index"; Fixed[ObsKSkill] = "keeper.skill";
				Block(ObsKIdentity, RL::NumKeepers, "keeper.identity"); Fixed[ObsSinceKCommit] = "keeper.since_commit";
				bInit = true;
			}
			return (I >= 0 && I < ObsDim) ? Fixed[I] : "?";
		}
	}

	// ----------------------------------------------------------------------------------------------
	// FPlayerAgent
	// ----------------------------------------------------------------------------------------------

	void FPlayerAgent::BeginFight(const FDuel& Duel, const FDuelGeometry& Geo, float KeeperSkill, int32_t KeeperIdentity, int32_t FightIndex)
	{
		for (FSnap& S : Snaps) { S = FSnap{}; }
		FirstSnap = Duel.Frame;
		LatestSnap = -1;
		PendingHead = PendingCount = 0;
		PerceivedKOutcome = EHitOutcome::None;
		PerceivedKCommit = -100000;
		OwnOutcome = EHitOutcome::None;
		Skill = KeeperSkill;
		Identity = KeeperIdentity >= 0 && KeeperIdentity < RL::NumKeepers ? KeeperIdentity : 0;
		Fight = FightIndex;
		NextDecision = Duel.Frame;
		bGuard = false;
		Fwd = Lat = 0.f;
		Snapshot(Duel, Geo, Duel.Frame);
	}

	bool FPlayerAgent::IsDecisionPoint(const FDuel& Duel) const
	{
		return !Duel.IsOver() && Duel.Frame >= NextDecision;
	}

	FPlayerAgent::FSnap FPlayerAgent::SnapAt(int32_t Frame) const
	{
		if (LatestSnap < 0) { return FSnap{}; }
		int32_t Lo = LatestSnap - SnapRing + 1;
		if (Lo < FirstSnap) { Lo = FirstSnap; }
		int32_t F = Frame < Lo ? Lo : (Frame > LatestSnap ? LatestSnap : Frame);
		for (; F >= Lo; --F)
		{
			const FSnap& S = Snaps[RLPlayerImpl::Ring(F, SnapRing)];
			if (S.Frame == F) { return S; }
		}
		return Snaps[RLPlayerImpl::Ring(LatestSnap, SnapRing)];
	}

	void FPlayerAgent::Snapshot(const FDuel& Duel, const FDuelGeometry& Geo, int32_t Frame)
	{
		FSnap& S = Snaps[RLPlayerImpl::Ring(Frame, SnapRing)];
		S = FSnap{};
		S.Frame = Frame;
		S.PX = Geo.PlayerX; S.PY = Geo.PlayerY; S.BX = Geo.BossX; S.BY = Geo.BossY;
		const FFighter& K = Duel.Get(ESide::Boss);
		S.State = K.State;
		S.Move = K.State == EFighterState::Acting ? K.Move : EMoveId::None;
		S.T = K.State == EFighterState::Acting ? K.T : 0;
		S.UntilActionable = K.FramesUntilActionable();
		S.bArmor = K.HasHyperArmor();
		S.bInvulnerable = K.IsInvulnerable();
		S.CommitFacingX = K.CommitFacingX;
		S.CommitFacingY = K.CommitFacingY;
		S.Health = K.Health;
		S.HealthMax = K.HealthMax > 0.f ? K.HealthMax : 1.f;
		S.ShaChi = K.ShaChi;
		S.ShaChiMax = K.ShaChiMax > 0.f ? K.ShaChiMax : 1.f;
		if (Frame > LatestSnap) { LatestSnap = Frame; }
	}

	void FPlayerAgent::RecordFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo)
	{
		const int32_t Now = Duel.Frame;
		for (const FDuelEvent& E : Events)
		{
			if (E.Type == EDuelEvent::Outcome && E.Side == ESide::Player) { OwnOutcome = E.Outcome; } // its own swing: now
			if (E.Side != ESide::Boss || (E.Type != EDuelEvent::Commit && E.Type != EDuelEvent::Outcome)) { continue; }
			if (PendingCount == EventRing)
			{
				const FDuelEvent& Old = Pending[PendingHead];
				if (Old.Type == EDuelEvent::Commit) { PerceivedKCommit = Old.Frame; } else { PerceivedKOutcome = Old.Outcome; }
				PendingHead = (PendingHead + 1) % EventRing;
				--PendingCount;
			}
			Pending[(PendingHead + PendingCount) % EventRing] = E;
			++PendingCount;
		}
		while (PendingCount > 0)
		{
			const FDuelEvent& E = Pending[PendingHead];
			if (E.Frame > Now - RLPlayer::PerceptionFrames - 1) { break; }
			if (E.Type == EDuelEvent::Commit) { PerceivedKCommit = E.Frame; } else { PerceivedKOutcome = E.Outcome; }
			PendingHead = (PendingHead + 1) % EventRing;
			--PendingCount;
		}
		// Geo is the geometry at the start of the frame just stepped: it belongs to that frame's snapshot.
		const int32_t Stepped = Now - 1;
		if (Stepped >= FirstSnap && Stepped <= LatestSnap)
		{
			FSnap& S = Snaps[RLPlayerImpl::Ring(Stepped, SnapRing)];
			if (S.Frame == Stepped) { S.PX = Geo.PlayerX; S.PY = Geo.PlayerY; S.BX = Geo.BossX; S.BY = Geo.BossY; }
		}
		Snapshot(Duel, Geo, Now);
	}

	void FPlayerAgent::BuildObservation(const FDuel& Duel, const FDuelGeometry& Geo, float* O) const
	{
		using RLPlayerImpl::Clip;
		using RLPlayerImpl::Flag;
		using namespace RLPlayer;
		for (int32_t I = 0; I < ObsDim; ++I) { O[I] = 0.f; }
		const int32_t D = Duel.Frame;
		const FFighter& P = Duel.Get(ESide::Player);
		const FSnap K = SnapAt(D - PerceptionFrames);
		const FSnap K2 = SnapAt(D - PerceptionFrames - 4);
		const FCombatTuning& Tu = Tuning();

		// ---- SELF
		O[ObsHealth] = RLPlayerImpl::HealthFraction(P.Health, P.HealthMax, Tu.PlayerHealthMax);
		O[ObsShaChi] = P.ShaChiMax > 0.f ? Clip(P.ShaChi / P.ShaChiMax, 0.f, 1.f) : 0.f;
		O[ObsState + static_cast<int32_t>(P.State)] = 1.f;
		if (P.State == EFighterState::Acting && P.Move != EMoveId::None)
		{
			const int32_t Mi = static_cast<int32_t>(P.Move) - static_cast<int32_t>(EMoveId::PLight1);
			if (Mi >= 0 && Mi < 13) { O[ObsMove + Mi] = 1.f; }
			const FMoveData& M = Move(P.Move);
			O[ObsPhase + RLPlayerImpl::PhaseIndex(M, P.T)] = 1.f;
			const int32_t Total = M.TotalFrames();
			O[ObsProgress] = Total > 0 ? Clip(static_cast<float>(P.T) / static_cast<float>(Total), 0.f, 1.f) : 0.f;
		}
		O[ObsUntilActionable] = Clip(static_cast<float>(P.FramesUntilActionable()) / 60.f, 0.f, 2.f);
		O[ObsGuardHeld] = Flag(P.bGuardHeld);
		O[ObsGuarding] = Flag(P.IsGuarding());
		O[ObsParryLive] = Flag(P.IsParryLive());
		O[ObsInvulnerable] = Flag(P.IsInvulnerable());
		if (P.Weapon == 0 || P.Weapon == 1) { O[ObsWeapon + P.Weapon] = 1.f; }
		O[ObsChain] = Clip(static_cast<float>(P.ChainDepth) / 3.f, 0.f, 1.f);
		O[ObsLastOutcome + static_cast<int32_t>(OwnOutcome)] = 1.f;
		O[ObsWalkF] = Fwd;
		O[ObsWalkL] = Lat;

		// ---- KEEPER, perceived
		O[ObsKHealth] = RLPlayerImpl::HealthFraction(K.Health, K.HealthMax, Tu.BossHealthMax * RL::KeeperHealthScale(Identity));
		O[ObsKShaChi] = Clip(K.ShaChi / K.ShaChiMax, 0.f, 1.f);
		O[ObsKState + static_cast<int32_t>(K.State)] = 1.f;
		const bool bKAttacking = K.State == EFighterState::Acting && K.Move != EMoveId::None && Move(K.Move).IsAttack();
		if (K.State == EFighterState::Acting && K.Move != EMoveId::None)
		{
			const int32_t A = RL::MoveAction(K.Move);
			if (A >= 0) { O[ObsKMove + A] = 1.f; }
			const FMoveData& M = Move(K.Move);
			O[ObsKPhase + RLPlayerImpl::PhaseIndex(M, K.T)] = 1.f;
			const int32_t Total = M.TotalFrames();
			O[ObsKProgress] = Total > 0 ? Clip(static_cast<float>(K.T) / static_cast<float>(Total), 0.f, 1.f) : 0.f;
		}
		const int32_t KUntil = K.UntilActionable - PerceptionFrames > 0 ? K.UntilActionable - PerceptionFrames : 0;
		O[ObsKUntilActionable] = Clip(static_cast<float>(KUntil) / 60.f, 0.f, 2.f);
		O[ObsKArmor] = Flag(K.bArmor);
		O[ObsKInvulnerable] = Flag(K.bInvulnerable);
		O[ObsKLastOutcome + static_cast<int32_t>(PerceivedKOutcome)] = 1.f;

		// ---- GEOMETRY: its own position now, the keeper's as perceived
		const float Dx = K.BX - Geo.PlayerX;
		const float Dy = K.BY - Geo.PlayerY;
		const float Dist = RLPlayerImpl::Len(Dx, Dy);
		O[ObsDistance] = Clip(Dist / 500.f, 0.f, 3.f);
		{
			const float R0 = RLPlayerImpl::Len(K.BX - K.PX, K.BY - K.PY);
			const float R1 = RLPlayerImpl::Len(K2.BX - K2.PX, K2.BY - K2.PY);
			O[ObsRadialVel] = Clip((R1 - R0) / 4.f / 10.f, -2.f, 2.f);
			float Fx = K.PX - K.BX;
			float Fy = K.PY - K.BY;
			const float Fl = RLPlayerImpl::Len(Fx, Fy);
			if (Fl > 1.0e-4f) { Fx /= Fl; Fy /= Fl; } else { Fx = 1.f; Fy = 0.f; }
			const float Vx = (K.BX - K2.BX) / 4.f;
			const float Vy = (K.BY - K2.BY) / 4.f;
			O[ObsLateralVel] = Clip((Vx * -Fy + Vy * Fx) / 10.f, -2.f, 2.f);
		}
		{
			float Sin = 0.f;
			float Cos = 1.f;
			if (bKAttacking && Dist > 1.0e-4f)
			{
				float Fx = K.CommitFacingX;
				float Fy = K.CommitFacingY;
				const float Fl = RLPlayerImpl::Len(Fx, Fy);
				if (Fl > 1.0e-4f) { Fx /= Fl; Fy /= Fl; } else { Fx = 1.f; Fy = 0.f; }
				const float Ux = -Dx / Dist; // from the keeper to the player
				const float Uy = -Dy / Dist;
				Cos = Clip(Fx * Ux + Fy * Uy, -1.f, 1.f);
				Sin = Clip(Fx * Uy - Fy * Ux, -1.f, 1.f);
			}
			O[ObsSwingSin] = Sin;
			O[ObsSwingCos] = Cos;
		}

		// ---- CONTEXT
		O[ObsFrameAdvantage] = Clip(static_cast<float>(KUntil - P.FramesUntilActionable()) / 30.f, -2.f, 2.f);
		O[ObsFightTime] = Clip(static_cast<float>(D) / 60.f / 180.f, 0.f, 1.f);
		O[ObsFightIndex] = Clip(static_cast<float>(Fight) / 3.f, 0.f, 1.f);
		O[ObsKSkill] = Clip(Skill, 0.f, 1.f);
		O[ObsKIdentity + Identity] = 1.f;
		O[ObsSinceKCommit] = Clip(static_cast<float>(D - PerceivedKCommit) / 60.f, 0.f, 2.f);
	}

	void FPlayerAgent::BuildMask(const FDuel& Duel, uint8_t* Out) const
	{
		using namespace RLPlayer;
		const FFighter& P = Duel.Get(ESide::Player);
		for (int32_t A = 0; A <= ActGuard; ++A) { Out[A] = 1; }
		int32_t Depth = 0;
		if (P.State == EFighterState::Acting && P.CurrentMove().IsAttack() && P.CurrentMove().Symbol == ESym::Light) { Depth = P.ChainDepth + 1; }
		Out[ActLight] = Duel.CanCommit(ESide::Player, NextLight(P.Weapon, Depth)) ? 1 : 0;
		Out[ActHeavy] = Duel.CanCommit(ESide::Player, HeavyFor(P.Weapon)) ? 1 : 0;
		Out[ActParry] = Duel.CanCommit(ESide::Player, EMoveId::PParry) ? 1 : 0;
		Out[ActStepF] = Duel.CanCommit(ESide::Player, StepFor(EDir::Forward)) ? 1 : 0;
		Out[ActStepB] = Duel.CanCommit(ESide::Player, StepFor(EDir::Back)) ? 1 : 0;
		Out[ActStepL] = Duel.CanCommit(ESide::Player, StepFor(EDir::Left)) ? 1 : 0;
		Out[ActStepR] = Duel.CanCommit(ESide::Player, StepFor(EDir::Right)) ? 1 : 0;
		Out[ActSwitch] = Duel.CanCommit(ESide::Player, EMoveId::PSwitch) ? 1 : 0;
	}

	bool FPlayerAgent::ApplyAction(FDuel& Duel, const FSimArena& Arena, int32_t A)
	{
		using namespace RLPlayer;
		uint8_t M[NumActions];
		BuildMask(Duel, M);
		bool bOk = true;
		if (A < 0 || A >= NumActions || M[A] == 0) { ++Illegal; A = ActHold; bOk = false; }
		NextDecision = Duel.Frame + DecisionGapFrames;
		const FVec2 Face = Arena.FacingOf(ESide::Player);
		// Walks and guard persist until the next decision; a commitment releases the guard and stops walking.
		bGuard = A == ActGuard;
		Fwd = A == ActWalkF ? 1.f : (A == ActWalkB ? -1.f : 0.f);
		Lat = A == ActWalkL ? 1.f : (A == ActWalkR ? -1.f : 0.f);
		bool bCommitted = true;
		switch (A)
		{
		case ActLight:  bCommitted = Duel.CommitPlayerAttack(false, Face.X, Face.Y); break;
		case ActHeavy:  bCommitted = Duel.CommitPlayerAttack(true, Face.X, Face.Y); break;
		case ActParry:  bCommitted = Duel.Commit(ESide::Player, EMoveId::PParry, Face.X, Face.Y); break;
		case ActStepF:  bCommitted = Duel.Commit(ESide::Player, StepFor(EDir::Forward), Face.X, Face.Y); break;
		case ActStepB:  bCommitted = Duel.Commit(ESide::Player, StepFor(EDir::Back), Face.X, Face.Y); break;
		case ActStepL:  bCommitted = Duel.Commit(ESide::Player, StepFor(EDir::Left), Face.X, Face.Y); break;
		case ActStepR:  bCommitted = Duel.Commit(ESide::Player, StepFor(EDir::Right), Face.X, Face.Y); break;
		case ActSwitch: bCommitted = Duel.CommitSwitch(); break;
		default: break;
		}
		if (!bCommitted) { ++Illegal; bOk = false; }
		HoldState(Duel);
		return bOk;
	}

	void FPlayerAgent::HoldState(FDuel& Duel) const
	{
		Duel.SetGuardHeld(ESide::Player, bGuard && !Duel.Get(ESide::Player).IsDead());
	}

	ESym FPlayerAgent::MovementSym() const
	{
		if (Fwd > 0.3f) { return ESym::Advance; }
		if (Fwd < -0.3f) { return ESym::Retreat; }
		return ESym::Neutral;
	}

	// ----------------------------------------------------------------------------------------------
	// FExploiterDriver
	// ----------------------------------------------------------------------------------------------

	bool FExploiterDriver::Bind(const FRLPolicy* InPolicy)
	{
		Policy = nullptr;
		if (InPolicy == nullptr || !InPolicy->IsLoaded() || InPolicy->Side() != 1 || InPolicy->ObsDim() != RLPlayer::ObsDim
			|| InPolicy->NumActions() != RLPlayer::NumActions || InPolicy->HistoryTokens() != 0)
		{
			return false;
		}
		Policy = InPolicy;
		Hidden.assign(static_cast<size_t>(Policy->HiddenSize()), 0.f);
		Scratch.assign(static_cast<size_t>(Policy->ScratchSize()), 0.f);
		return true;
	}

	void FExploiterDriver::BeginSession(int32_t Seed)
	{
		Rng.Initialize(Seed);
		for (float& H : Hidden) { H = 0.f; }
	}

	void FExploiterDriver::BeginFight(const FDuel& Duel, const FDuelGeometry& Geo, float KeeperSkill, int32_t KeeperIdentity, int32_t FightIndex)
	{
		Agent.BeginFight(Duel, Geo, KeeperSkill, KeeperIdentity, FightIndex);
	}

	void FExploiterDriver::Act(FDuel& Duel, const FSimArena& Arena, float& OutFwd, float& OutLat)
	{
		if (Policy != nullptr && Agent.IsDecisionPoint(Duel))
		{
			Agent.BuildObservation(Duel, Arena.Geometry(), Obs);
			Agent.BuildMask(Duel, Mask);
			FRLPolicyOutput Out;
			Policy->Forward(Obs, nullptr, Mask, Hidden.data(), Hidden.data(), Out, Scratch.data());
			// Sample at temperature 1 (as it was trained), from the env's stream: deterministic for a seed.
			float R = Rng.FRand();
			int32_t A = Out.Argmax;
			for (int32_t I = 0; I < RLPlayer::NumActions; ++I)
			{
				if (Mask[I] == 0 || Out.Probs[I] <= 0.f) { continue; }
				A = I;
				if (R < Out.Probs[I]) { break; }
				R -= Out.Probs[I];
			}
			Agent.ApplyAction(Duel, Arena, A);
		}
		else
		{
			Agent.HoldState(Duel);
		}
		OutFwd = Agent.WalkForward();
		OutLat = Agent.WalkLateral();
	}
}
