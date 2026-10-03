// HellwalkerRL — engine-free core. The RL keeper's senses and hands: FRLSession, FRLObserver, ChainStartupFloor.
// Semantics: RL/DESIGN.md §1 - §5. Layouts: HWRLTypes.h. Shared verbatim by the training environment and the game.

#include "HWCore/HWRLObserver.h"

#include <cmath>
#include <cstring>

namespace HW
{
	// Helpers live in a named namespace (not an anonymous one): Unreal's unity builds paste several HWCore .cpp files
	// into one translation unit, and HWSim.cpp / HWRLPolicy.cpp already have anonymous-namespace helpers of their own.
	namespace RLObsImpl
	{
		/** TokBoss value of a Wait token (1..8 are the boss symbols BFast .. BRetreat). */
		constexpr int8_t WaitTokenBoss = 9;
		/** ChainStartupFloor for a class no script ever chains after. */
		constexpr int32_t NoChainFloor = 9999;

		inline float Clip(float V, float Lo, float Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
		inline float Flag(bool B) { return B ? 1.f : 0.f; }
		inline int32_t RingIndex(int32_t Frame, int32_t Size)
		{
			const int32_t M = Frame % Size;
			return M < 0 ? M + Size : M;
		}
		inline float Length(float X, float Y) { return std::sqrt(X * X + Y * Y); }

		/**
		 * Health as the keeper sees it. Immortal fights (FEncounter's rate-measurement mode) give both fighters 1e9-point
		 * pools; there the fraction is taken of a normal pool (RealMax) that refills at every would-be death, so the
		 * inputs move as they do in the mortal fights it trained on instead of sitting at 1.0 all fight.
		 */
		inline float HealthFraction(float Health, float HealthMax, float RealMax)
		{
			if (HealthMax <= 0.f) { return 0.f; }
			if (HealthMax < 1.0e8f || RealMax <= 0.f) { return Clip(Health / HealthMax, 0.f, 1.f); }
			const float Lost = HealthMax - Health;
			const float Into = std::fmod(Lost < 0.f ? 0.f : Lost, RealMax);
			return Clip(1.f - Into / RealMax, 0.f, 1.f);
		}

		/** TokBoss for a boss move: its symbol, BFast -> 1 .. BRetreat -> 8. */
		inline int8_t BossTokenOf(EMoveId M)
		{
			const int32_t V = SymIndex(Move(M).Symbol) - (SymIndex(ESym::BFast) - 1);
			return static_cast<int8_t>(V >= 1 && V <= 8 ? V : WaitTokenBoss);
		}

		/** Acting in an attack and already past its cancel frame: a chained attack is possible now. */
		inline bool InChainWindow(const FFighter& B)
		{
			return B.State == EFighterState::Acting && B.CurrentMove().IsAttack() && B.IsActionable();
		}

		/** Phase one-hot index: 0 attack startup, 1 attack active, 2 attack recovery, 3 any other move. */
		inline int32_t PhaseIndex(const FMoveData& M, int32_t T)
		{
			if (!M.IsAttack()) { return 3; }
			if (T < M.Startup) { return 0; }
			if (T < M.Startup + M.Active) { return 1; }
			return 2;
		}
	}

	// ----------------------------------------------------------------------------------------------
	// Session
	// ----------------------------------------------------------------------------------------------

	void FRLSession::Reset()
	{
		*this = FRLSession{};
	}

	void FRLSession::PushToken(const int8_t* Token)
	{
		// Rows 0 .. Keep-1 move down one; when the history is full the oldest (last) row falls off.
		const int32_t Keep = NumTokens < RL::HistoryTokens ? NumTokens : RL::HistoryTokens - 1;
		if (Keep > 0)
		{
			std::memmove(&Tokens[1][0], &Tokens[0][0], static_cast<size_t>(Keep) * static_cast<size_t>(RL::TokenFields));
		}
		for (int32_t F = 0; F < RL::TokenFields; ++F) { Tokens[0][F] = Token[F]; }
		if (NumTokens < RL::HistoryTokens) { ++NumTokens; }
	}

	// ----------------------------------------------------------------------------------------------
	// The legibility floor, from the scripts
	// ----------------------------------------------------------------------------------------------

	float FRLObserver::SwingsPerMinute(int32_t Frame) const
	{
		constexpr int32_t Window = 1800; // 30 s
		int32_t Recent = 0;
		for (int32_t I = 0; I < SwingCount; ++I)
		{
			const int32_t F = SwingFrames[(SwingHead + SwingRing - 1 - I) % SwingRing]; // newest first
			if (Frame - F >= Window) { break; }
			++Recent;
		}
		const int32_t Span = Frame < Window ? (Frame > 600 ? Frame : 600) : Window;
		return static_cast<float>(Recent) * 3600.f / static_cast<float>(Span);
	}

	int32_t ChainStartupFloor(ESym PreviousAttackSymbol)
	{
		// Computed once (thread-safe static init), from every boss script: a chained attack slot whose previous slot is
		// also an attack says "the script chains this fast after that class". The fastest such chain is the floor —
		// the RL keeper may never chain faster than a scripted keeper could (RL.md §4.5).
		struct FFloors { int32_t V[NumSymbols]; };
		static const FFloors Floors = []()
		{
			FFloors F;
			for (int32_t& X : F.V) { X = RLObsImpl::NoChainFloor; }
			for (int32_t S = 0; S < NumBossScripts; ++S)
			{
				const FScriptSlot* Slots = nullptr;
				const int32_t N = BossScript(S, Slots);
				for (int32_t I = 0; I < N; ++I)
				{
					const FScriptSlot& Slot = Slots[I];
					const FScriptSlot& Prev = Slots[(I + N - 1) % N];
					if (!Slot.bChain || Slot.Move == EMoveId::None || Prev.Move == EMoveId::None) { continue; }
					const FMoveData& M = Move(Slot.Move);
					const FMoveData& PM = Move(Prev.Move);
					if (!M.IsAttack() || !PM.IsAttack()) { continue; }
					int32_t& Floor = F.V[SymIndex(PM.Symbol)];
					if (M.Startup < Floor) { Floor = M.Startup; }
				}
			}
			return F;
		}();
		const int32_t I = SymIndex(PreviousAttackSymbol);
		return (I >= 0 && I < NumSymbols) ? Floors.V[I] : RLObsImpl::NoChainFloor;
	}

	// ----------------------------------------------------------------------------------------------
	// Fights
	// ----------------------------------------------------------------------------------------------

	void FRLObserver::BeginEncounter(FRLSession* InSession, const FDuel& Duel, const FDuelGeometry& Geo)
	{
		// Tokens the previous fight left open belong to the session that fight was played on. Finalise them into it only
		// if this is still that session: a session that was Reset (FightsBegun == 0) or swapped for another is a new
		// player, and must not inherit the last player's exchanges.
		if (NumOpen > 0 && InSession != nullptr && InSession == Session && InSession->FightsBegun > 0)
		{
			FinaliseTokens(Duel.Frame, true);
		}
		NumOpen = 0;

		Session = InSession;
		if (Session != nullptr) { Session->FightIndex = Session->FightsBegun++; }
		bMirrorY = Geo.bMirrorY;
		SkillP = RL::SkillParams(Config.Skill);
		Target = RL::TargetSwingsPerMin(Config.TargetSwingsPerMin, SkillP.Skill);
		Identity = Config.Identity >= 0 && Config.Identity < RL::NumKeepers ? Config.Identity : 0;
		MinGap = Config.MinSwingGap > 0 ? Config.MinSwingGap : 0;
		if (MinGap > 0)
		{
			// The breather caps the swing rate at 3600 / MinGap a minute. The swing-deficit input measures against 90% of
			// that, so Easy's keeper is not told it is behind for the whole fight (an input training never showed it).
			const float Allowed = 0.9f * 3600.f / static_cast<float>(MinGap);
			Target = Target < Allowed ? Target : Allowed;
		}

		for (FSnap& S : Snaps) { S = FSnap{}; S.Frame = -1; }
		FirstSnapFrame = Duel.Frame;
		LatestSnapFrame = -1;
		PendingHead = PendingCount = 0;
		PerceivedPlayerOutcome = EHitOutcome::None;
		PerceivedPlayerCommitFrame = -100000;
		SwingHead = SwingCount = 0;

		NextDecisionFrame = Duel.Frame;
		LastDecision = -1;
		LastActionIndex = -1;
		StringAttacks = 0;
		LastGrabFrame = -100000;
		LastKillerFrame = -100000;
		LastOwnOutcomeFrame = -1;
		LastOwnOutcome = EHitOutcome::None;
		bDefendedSinceDecision = false;
		LastOwnSwingFrame = -100000;
		Attacks = 0;
		Decisions = 0;
		Illegal = 0;
		ReadCounters = 0;
		bReadMeterReady = false;
		ReadMeter = FReadMeterEvent{};

		Snapshot(Duel, Geo, Duel.Frame);
	}

	bool FRLObserver::IsDecisionPoint(const FDuel& Duel) const
	{
		return Session != nullptr && !Duel.IsOver() && Duel.Get(ESide::Boss).IsActionable() && Duel.Frame >= NextDecisionFrame;
	}

	// ----------------------------------------------------------------------------------------------
	// Perception
	// ----------------------------------------------------------------------------------------------

	FRLObserver::FSnap FRLObserver::SnapAt(int32_t Frame) const
	{
		if (LatestSnapFrame < 0) { return FSnap{}; }
		int32_t Lo = LatestSnapFrame - SnapRing + 1;
		if (Lo < FirstSnapFrame) { Lo = FirstSnapFrame; }
		int32_t F = Frame < Lo ? Lo : (Frame > LatestSnapFrame ? LatestSnapFrame : Frame);
		// Frames are recorded one by one; should a caller ever skip a RecordFrame, fall back to the newest older record.
		for (; F >= Lo; --F)
		{
			const FSnap& S = Snaps[RLObsImpl::RingIndex(F, SnapRing)];
			if (S.Frame == F) { return S; }
		}
		return Snaps[RLObsImpl::RingIndex(LatestSnapFrame, SnapRing)];
	}

	void FRLObserver::Snapshot(const FDuel& Duel, const FDuelGeometry& Geo, int32_t Frame)
	{
		FSnap& S = Snaps[RLObsImpl::RingIndex(Frame, SnapRing)];
		S = FSnap{};
		S.Frame = Frame;
		// Positions: the newest geometry known. RecordFrame overwrites them with the frame's own geometry one frame later
		// (the geometry handed to RecordFrame after step F is the one at the START of frame F — exactly Snap[F]'s).
		const float Sy = Geo.bMirrorY ? -1.f : 1.f;
		S.PX = Geo.PlayerX;
		S.PY = Geo.PlayerY * Sy;
		S.BX = Geo.BossX;
		S.BY = Geo.BossY * Sy;

		const FFighter& P = Duel.Get(ESide::Player);
		S.State = P.State;
		S.Move = P.State == EFighterState::Acting ? P.Move : EMoveId::None;
		S.T = P.State == EFighterState::Acting ? P.T : 0;
		S.UntilActionable = P.FramesUntilActionable();
		S.bGuardHeld = P.bGuardHeld;
		S.bGuarding = P.IsGuarding();
		S.bParryLive = P.IsParryLive();
		S.bInvulnerable = P.IsInvulnerable();
		S.bArmor = P.HasHyperArmor();
		S.Weapon = P.Weapon;
		S.ChainDepth = P.ChainDepth;
		S.Health = P.Health;
		S.HealthMax = P.HealthMax > 0.f ? P.HealthMax : 1.f;
		S.ShaChi = P.ShaChi;
		S.ShaChiMax = P.ShaChiMax > 0.f ? P.ShaChiMax : 1.f;
		if (Frame > LatestSnapFrame) { LatestSnapFrame = Frame; }
	}

	void FRLObserver::RecordFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement)
	{
		const int32_t Now = Duel.Frame; // the NEXT frame: FDuel::Step already advanced it
		const FFighter& Player = Duel.Get(ESide::Player);
		bMirrorY = Geo.bMirrorY;

		// ---- 1. Ground truth into the open tokens (what the tokens will say once they are perceivable) --------------
		auto FindToken = [this](int32_t F) -> FOpenToken*
		{
			for (int32_t I = NumOpen - 1; I >= 0; --I)
			{
				FOpenToken& T = Open[I];
				if (T.OpenFrame <= F && (T.CloseFrame < 0 || F < T.CloseFrame)) { return &T; }
			}
			return nullptr;
		};
		if (NumOpen > 0 && Open[NumOpen - 1].CloseFrame < 0)
		{
			FOpenToken& Cur = Open[NumOpen - 1];
			Cur.bGuardSeen = Cur.bGuardSeen || Player.bGuardHeld;
			Cur.Movement = PlayerMovement;
		}
		// Commitments first, so the READ check below sees everything the player did up to the frame the hit lands.
		for (const FDuelEvent& E : Events)
		{
			if (E.Type != EDuelEvent::Commit || E.Side != ESide::Player) { continue; }
			FOpenToken* T = FindToken(E.Frame);
			if (T != nullptr && T->Answer == ESym::Count && IsPlayerSym(E.Sym))
			{
				T->Answer = E.Sym;
				T->AnswerFrame = E.Frame;
			}
		}
		for (const FDuelEvent& E : Events)
		{
			if (E.Type != EDuelEvent::Outcome) { continue; }
			FOpenToken* T = FindToken(E.Frame);
			if (E.Side == ESide::Boss)
			{
				// Its own swings: known at once (it knows its body).
				LastOwnOutcome = E.Outcome;
				LastOwnOutcomeFrame = E.Frame;
				if (T == nullptr || T->Move != E.Move) { continue; }
				if (T->Outcome == EHitOutcome::None) { T->Outcome = E.Outcome; }
				if (E.Damage > 0.f) { T->bDealt = true; }
				// READ banner: testimony only — a confident, correct read that a hit has just landed on.
				if (E.Outcome == EHitOutcome::Hit && T->Read.P >= Config.ReadMeterMinP && AnswerOf(*T) == T->Read.Predicted)
				{
					++ReadCounters;
					ReadMeter.Frame = E.Frame;
					ReadMeter.Predicted = T->Read.Predicted;
					ReadMeter.Confidence = T->Read.P;
					ReadMeter.Counter = E.Move;
					bReadMeterReady = true;
				}
			}
			else
			{
				if (T != nullptr && E.Damage > 0.f) { T->bTaken = true; }
				// Its own guard / counter stance caught the swing: its own body again, so current.
				if (E.Outcome == EHitOutcome::Blocked || E.Outcome == EHitOutcome::Parried) { bDefendedSinceDecision = true; }
			}
		}

		// ---- 2. Player events wait until they are PerceptionFrames old ---------------------------------------------
		for (const FDuelEvent& E : Events)
		{
			if (E.Side != ESide::Player || (E.Type != EDuelEvent::Commit && E.Type != EDuelEvent::Outcome)) { continue; }
			if (PendingCount == EventRing)
			{
				// Cannot happen at a few events per frame; if it ever did, perceiving the oldest early beats losing it.
				const FDuelEvent& Old = PendingPlayerEvents[PendingHead];
				if (Old.Type == EDuelEvent::Commit) { PerceivedPlayerCommitFrame = Old.Frame; }
				else { PerceivedPlayerOutcome = Old.Outcome; }
				PendingHead = (PendingHead + 1) % EventRing;
				--PendingCount;
			}
			PendingPlayerEvents[(PendingHead + PendingCount) % EventRing] = E;
			++PendingCount;
		}
		while (PendingCount > 0)
		{
			const FDuelEvent& E = PendingPlayerEvents[PendingHead];
			if (E.Frame > Now - SkillP.Perception - 1) { break; }
			if (E.Type == EDuelEvent::Commit) { PerceivedPlayerCommitFrame = E.Frame; }
			else { PerceivedPlayerOutcome = E.Outcome; }
			PendingHead = (PendingHead + 1) % EventRing;
			--PendingCount;
		}

		// ---- 3. Snapshots: Geo belongs to the frame just stepped; the duel state is the next frame's start ----------
		const int32_t Stepped = Now - 1;
		if (Stepped >= FirstSnapFrame && Stepped <= LatestSnapFrame)
		{
			FSnap& S = Snaps[RLObsImpl::RingIndex(Stepped, SnapRing)];
			if (S.Frame == Stepped)
			{
				const float Sy = Geo.bMirrorY ? -1.f : 1.f;
				S.PX = Geo.PlayerX;
				S.PY = Geo.PlayerY * Sy;
				S.BX = Geo.BossX;
				S.BY = Geo.BossY * Sy;
			}
		}
		Snapshot(Duel, Geo, Now);

		// ---- 4. Tokens whose whole window is now perceivable join the session --------------------------------------
		FinaliseTokens(Now, false);
	}

	// ----------------------------------------------------------------------------------------------
	// The observation
	// ----------------------------------------------------------------------------------------------

	void FRLObserver::BuildObservation(const FDuel& Duel, float* OutObs, int8_t* OutTokens) const
	{
		using RLObsImpl::Clip;
		using RLObsImpl::Flag;
		float* O = OutObs;
		for (int32_t I = 0; I < RL::ObsDim; ++I) { O[I] = 0.f; }

		const int32_t D = Duel.Frame;
		const FFighter& B = Duel.Get(ESide::Boss);
		const FSnap L = SnapAt(LatestSnapFrame);                         // the keeper's latest recorded position
		const FSnap P = SnapAt(D - SkillP.Perception);                   // the player, as seen
		const FSnap P2 = SnapAt(D - SkillP.Perception - 4);              // ... 4 frames earlier (velocities)

		// ---- SELF: current -------------------------------------------------------------------------------------------
		O[RL::ObsSelfHealth] = RLObsImpl::HealthFraction(B.Health, B.HealthMax, Tuning().BossHealthMax * RL::KeeperHealthScale(Identity));
		O[RL::ObsSelfShaChi] = B.ShaChiMax > 0.f ? Clip(B.ShaChi / B.ShaChiMax, 0.f, 1.f) : 0.f;
		O[RL::ObsSelfState + static_cast<int32_t>(B.State)] = 1.f;
		const bool bSelfAttacking = B.State == EFighterState::Acting && B.CurrentMove().IsAttack();
		if (B.State == EFighterState::Acting)
		{
			const FMoveData& M = B.CurrentMove();
			const int32_t A = RL::MoveAction(B.Move);
			if (A >= 0) { O[RL::ObsSelfMove + A] = 1.f; }
			O[RL::ObsSelfPhase + RLObsImpl::PhaseIndex(M, B.T)] = 1.f;
			const int32_t Total = M.TotalFrames();
			O[RL::ObsSelfProgress] = Total > 0 ? Clip(static_cast<float>(B.T) / static_cast<float>(Total), 0.f, 1.f) : 0.f;
		}
		O[RL::ObsSelfUntilActionable] = Clip(static_cast<float>(B.FramesUntilActionable()) / 60.f, 0.f, 2.f);
		O[RL::ObsSelfArmor] = Flag(B.HasHyperArmor());
		O[RL::ObsSelfInvulnerable] = Flag(B.IsInvulnerable());
		O[RL::ObsSelfStun] = Clip(static_cast<float>(B.StunLeft) / 90.f, 0.f, 1.f);
		O[RL::ObsSelfLastOutcome + static_cast<int32_t>(LastOwnOutcome)] = 1.f;
		O[RL::ObsSelfSinceOutcome] = LastOwnOutcomeFrame < 0 ? 1.f : Clip(static_cast<float>(D - LastOwnOutcomeFrame) / 120.f, 0.f, 1.f);
		O[RL::ObsSelfDefended] = Flag(bDefendedSinceDecision);
		O[RL::ObsSelfString] = Clip(static_cast<float>(StringAttacks) / static_cast<float>(RL::MaxStringAttacks), 0.f, 1.f);
		O[RL::ObsSelfGrabCooldown] = Clip(static_cast<float>(SkillP.GrabCooldown - (D - LastGrabFrame)) / static_cast<float>(SkillP.GrabCooldown), 0.f, 1.f);
		O[RL::ObsSelfKillerCooldown] = Clip(static_cast<float>(SkillP.KillerCooldown - (D - LastKillerFrame)) / static_cast<float>(SkillP.KillerCooldown), 0.f, 1.f);
		O[RL::ObsSelfChainWindow] = Flag(RLObsImpl::InChainWindow(B));

		// ---- PLAYER: PerceptionFrames late ---------------------------------------------------------------------------
		const float Dx = P.PX - L.BX;
		const float Dy = P.PY - L.BY;
		const float Dist = RLObsImpl::Length(Dx, Dy);
		O[RL::ObsPlayerDistance] = Clip(Dist / 500.f, 0.f, 3.f);
		{
			// Radial: how much the gap closed between the two perceived snapshots (+ = closing), each gap measured
			// inside its own snapshot so the keeper's own motion is not mistaken for the player's.
			const float R6 = RLObsImpl::Length(P.PX - P.BX, P.PY - P.BY);
			const float R10 = RLObsImpl::Length(P2.PX - P2.BX, P2.PY - P2.BY);
			O[RL::ObsPlayerRadialVel] = Clip((R10 - R6) / 4.f / 10.f, -2.f, 2.f);
			// Lateral: the player's velocity along its own left (it faces the keeper).
			float Fx = P.BX - P.PX;
			float Fy = P.BY - P.PY;
			const float Fl = RLObsImpl::Length(Fx, Fy);
			if (Fl > 1.0e-4f) { Fx /= Fl; Fy /= Fl; } else { Fx = 1.f; Fy = 0.f; }
			const float Vx = (P.PX - P2.PX) / 4.f;
			const float Vy = (P.PY - P2.PY) / 4.f;
			O[RL::ObsPlayerLateralVel] = Clip((Vx * -Fy + Vy * Fx) / 10.f, -2.f, 2.f);
		}
		{
			// Bearing from the committed facing while a swing is out (does it still track the player?); else straight on.
			float Sin = 0.f;
			float Cos = 1.f;
			if (bSelfAttacking)
			{
				float Fx = B.CommitFacingX;
				float Fy = B.CommitFacingY * (bMirrorY ? -1.f : 1.f);
				const float Fl = RLObsImpl::Length(Fx, Fy);
				if (Fl > 1.0e-4f) { Fx /= Fl; Fy /= Fl; } else { Fx = 1.f; Fy = 0.f; }
				if (Dist > 1.0e-4f)
				{
					const float Ux = Dx / Dist;
					const float Uy = Dy / Dist;
					Cos = Clip(Fx * Ux + Fy * Uy, -1.f, 1.f);
					Sin = Clip(Fx * Uy - Fy * Ux, -1.f, 1.f);
				}
			}
			O[RL::ObsPlayerBearingSin] = Sin;
			O[RL::ObsPlayerBearingCos] = Cos;
		}
		O[RL::ObsPlayerHealth] = RLObsImpl::HealthFraction(P.Health, P.HealthMax, Tuning().PlayerHealthMax);
		O[RL::ObsPlayerShaChi] = Clip(P.ShaChi / P.ShaChiMax, 0.f, 1.f);
		O[RL::ObsPlayerState + static_cast<int32_t>(P.State)] = 1.f;
		if (P.State == EFighterState::Acting && P.Move != EMoveId::None)
		{
			const int32_t Mi = static_cast<int32_t>(P.Move) - static_cast<int32_t>(EMoveId::PLight1);
			if (Mi >= 0 && Mi < 13) { O[RL::ObsPlayerMove + Mi] = 1.f; }
			const FMoveData& M = Move(P.Move);
			O[RL::ObsPlayerPhase + RLObsImpl::PhaseIndex(M, P.T)] = 1.f;
			const int32_t Total = M.TotalFrames();
			O[RL::ObsPlayerProgress] = Total > 0 ? Clip(static_cast<float>(P.T) / static_cast<float>(Total), 0.f, 1.f) : 0.f;
		}
		// What it saw is the perception delay old, so the player is that much closer to acting than the snapshot says.
		const int32_t PlayerUntil = P.UntilActionable - SkillP.Perception > 0 ? P.UntilActionable - SkillP.Perception : 0;
		O[RL::ObsPlayerUntilActionable] = Clip(static_cast<float>(PlayerUntil) / 60.f, 0.f, 2.f);
		O[RL::ObsPlayerGuardHeld] = Flag(P.bGuardHeld);
		O[RL::ObsPlayerGuarding] = Flag(P.bGuarding);
		O[RL::ObsPlayerParryLive] = Flag(P.bParryLive);
		O[RL::ObsPlayerInvulnerable] = Flag(P.bInvulnerable);
		O[RL::ObsPlayerArmor] = Flag(P.bArmor);
		if (P.Weapon == 0 || P.Weapon == 1) { O[RL::ObsPlayerWeapon + P.Weapon] = 1.f; }
		O[RL::ObsPlayerChain] = Clip(static_cast<float>(P.ChainDepth) / 3.f, 0.f, 1.f);
		O[RL::ObsPlayerLastOutcome + static_cast<int32_t>(PerceivedPlayerOutcome)] = 1.f;
		O[RL::ObsPlayerSinceCommit] = Clip(static_cast<float>(D - PerceivedPlayerCommitFrame) / 60.f, 0.f, 2.f);

		// ---- CONTEXT -------------------------------------------------------------------------------------------------
		O[RL::ObsFrameAdvantage] = Clip(static_cast<float>(PlayerUntil - B.FramesUntilActionable()) / 30.f, -2.f, 2.f);
		O[RL::ObsFightTime] = Clip(static_cast<float>(D) / 60.f / 180.f, 0.f, 1.f);
		O[RL::ObsFightIndex] = Session != nullptr ? Clip(static_cast<float>(Session->FightIndex) / 3.f, 0.f, 1.f) : 0.f;
		O[RL::ObsSwingDeficit] = Clip((Target - SwingsPerMinute(D)) / 60.f, -1.f, 1.f);
		O[RL::ObsSinceOwnSwing] = Clip(static_cast<float>(D - LastOwnSwingFrame) / 300.f, 0.f, 1.f);

		// ---- KEEPER --------------------------------------------------------------------------------------------------
		O[RL::ObsSkill] = SkillP.Skill;
		O[RL::ObsIdentity + Identity] = 1.f;

		if (OutTokens != nullptr)
		{
			std::memset(OutTokens, 0, static_cast<size_t>(RL::HistoryTokens) * static_cast<size_t>(RL::TokenFields));
			if (Session != nullptr && Session->NumTokens > 0)
			{
				const int32_t N = Session->NumTokens < RL::HistoryTokens ? Session->NumTokens : RL::HistoryTokens;
				std::memcpy(OutTokens, &Session->Tokens[0][0], static_cast<size_t>(N) * static_cast<size_t>(RL::TokenFields));
			}
		}
	}

	// ----------------------------------------------------------------------------------------------
	// Masks and actions
	// ----------------------------------------------------------------------------------------------

	void FRLObserver::BuildMask(const FDuel& Duel, uint8_t* OutMask) const
	{
		const int32_t D = Duel.Frame;
		const FFighter& B = Duel.Get(ESide::Boss);
		const bool bChain = RLObsImpl::InChainWindow(B);
		const int32_t Floor = bChain ? ChainStartupFloor(B.CurrentMove().Symbol) : 0;
		// Reach is judged on what it SEES: its latest position to the perceived player.
		const FSnap L = SnapAt(LatestSnapFrame);
		const FSnap P = SnapAt(D - SkillP.Perception);
		const float Dist = RLObsImpl::Length(P.PX - L.BX, P.PY - L.BY);
		for (int32_t A = 0; A < RL::NumBossMoves; ++A)
		{
			const EMoveId Id = RL::ActionMove(A);
			const FMoveData& M = Move(Id);
			bool bOk = Duel.CanCommit(ESide::Boss, Id);
			if (bOk && M.IsAttack())
			{
				bOk = Dist <= M.Range + RL::ReachSlack;
				if (bOk && bChain) { bOk = StringAttacks < SkillP.MaxString && M.Startup >= Floor; }
				if (bOk && !bChain && MinGap > 0) { bOk = D - LastOwnSwingFrame >= MinGap; } // Easy: a breather between strings
			}
			if (bOk && Id == EMoveId::BGrab) { bOk = D - LastGrabFrame >= SkillP.GrabCooldown; }
			if (bOk && Id == EMoveId::BKillerThrust) { bOk = D - LastKillerFrame >= SkillP.KillerCooldown; }
			OutMask[A] = bOk ? 1 : 0;
		}
		OutMask[RL::ActionWait] = 1;
	}

	EMoveId FRLObserver::ApplyAction(FDuel& Duel, int32_t A, const FRLRead* Read)
	{
		const int32_t D = Duel.Frame;
		uint8_t Mask[RL::NumActions];
		BuildMask(Duel, Mask);
		if (A < 0 || A >= RL::NumActions || Mask[A] == 0)
		{
			++Illegal;
			A = RL::ActionWait;
		}

		const FFighter& B = Duel.Get(ESide::Boss);
		const bool bWasChainWindow = RLObsImpl::InChainWindow(B); // before the commit changes the state
		EMoveId Committed = EMoveId::None;
		bool bContinue = false;
		EMoveId M = RL::ActionMove(A);
		if (A != RL::ActionWait)
		{
			if (B.State == EFighterState::Acting && B.Move == M && Move(M).Kind == EMoveKind::Locomotion)
			{
				bContinue = true; // re-choosing the walk it is already doing keeps walking (no re-commit)
			}
			else if (Duel.Commit(ESide::Boss, M))
			{
				Committed = M; // facing is latched by the caller after player input, as for every brain
			}
			else
			{
				++Illegal;
				A = RL::ActionWait;
				M = EMoveId::None;
			}
		}

		if (Committed != EMoveId::None)
		{
			if (Move(Committed).IsAttack())
			{
				++Attacks;
				if (Session != nullptr) { ++Session->BossSwingsSeen; }
				LastOwnSwingFrame = D;
				SwingFrames[SwingHead] = D;
				SwingHead = (SwingHead + 1) % SwingRing;
				if (SwingCount < SwingRing) { ++SwingCount; }
				StringAttacks = bWasChainWindow ? StringAttacks + 1 : 1;
				if (Committed == EMoveId::BGrab) { LastGrabFrame = D; }
				if (Committed == EMoveId::BKillerThrust) { LastKillerFrame = D; }
			}
			else
			{
				StringAttacks = 0;
			}
		}

		// ---- Tokens: one per decision window; an unanswered Wait (or walk) that goes on is one exchange, not many ----
		FOpenToken* Cur = (NumOpen > 0 && Open[NumOpen - 1].CloseFrame < 0) ? &Open[NumOpen - 1] : nullptr;
		bool bExtend = false;
		if (Cur != nullptr && Cur->Answer == ESym::Count)
		{
			if (A == RL::ActionWait)
			{
				bExtend = Cur->Boss == RLObsImpl::WaitTokenBoss;
			}
			else if (bContinue)
			{
				// The walk's own token (Move = M) or a continue token opened for the same walk (Move = None, same class).
				bExtend = Cur->Boss != RLObsImpl::WaitTokenBoss && Cur->Boss == RLObsImpl::BossTokenOf(M)
					&& (Cur->Move == M || Cur->Move == EMoveId::None);
			}
		}
		if (!bExtend)
		{
			if (Cur != nullptr) { Cur->CloseFrame = D; }
			if (NumOpen == MaxOpen)
			{
				// Never at one decision per >= 6 frames (at most two tokens are open); kept bounded regardless.
				if (Session != nullptr)
				{
					int8_t Row[RL::TokenFields];
					EncodeToken(Open[0], Row);
					Session->PushToken(Row);
				}
				for (int32_t I = 1; I < NumOpen; ++I) { Open[I - 1] = Open[I]; }
				--NumOpen;
			}
			FOpenToken& T = Open[NumOpen++];
			T = FOpenToken{};
			T.OpenFrame = D;
			T.Boss = A == RL::ActionWait ? RLObsImpl::WaitTokenBoss : RLObsImpl::BossTokenOf(M);
			T.Move = Committed;
			T.MoveCommitFrame = Committed != EMoveId::None ? D : -1;
			if (Read != nullptr) { T.Read = *Read; }
		}

		bDefendedSinceDecision = false;
		LastDecision = D;
		LastActionIndex = A;
		++Decisions;
		NextDecisionFrame = D + SkillP.DecisionGap;
		return Committed;
	}

	// ----------------------------------------------------------------------------------------------
	// Tokens, labels, READ
	// ----------------------------------------------------------------------------------------------

	ESym FRLObserver::AnswerOf(const FOpenToken& T)
	{
		if (T.Answer != ESym::Count) { return T.Answer; }
		if (T.bGuardSeen) { return ESym::Block; }
		return IsPlayerSym(T.Movement) ? T.Movement : ESym::Neutral;
	}

	void FRLObserver::EncodeToken(const FOpenToken& T, int8_t* Out)
	{
		const bool bAttack = T.Move != EMoveId::None && Move(T.Move).IsAttack();
		const bool bAnswered = T.Answer != ESym::Count;
		Out[RL::TokBoss] = T.Boss;
		Out[RL::TokAnswer] = static_cast<int8_t>(SymIndex(AnswerOf(T)) + 1);
		Out[RL::TokOutcome] = static_cast<int8_t>(bAttack ? 1 + static_cast<int32_t>(T.Outcome) : 1);
		Out[RL::TokExchange] = static_cast<int8_t>(1 + (T.bDealt ? 1 : 0) + (T.bTaken ? 2 : 0));
		int32_t Timing = 1;
		int32_t Bite = 1;
		if (bAttack && bAnswered && T.MoveCommitFrame >= 0)
		{
			// When the player pressed, relative to the impact it could have been timing: a bait's fake impact if it
			// pressed nearer that than the real strike (it bit), else the real one.
			const FMoveData& M = Move(T.Move);
			const int32_t Press = T.AnswerFrame - T.MoveCommitFrame;
			int32_t Anchor = M.Startup;
			if (M.FakeImpactFrame >= 0)
			{
				const bool bBit = Press * 2 < M.FakeImpactFrame + M.Startup;
				Anchor = bBit ? M.FakeImpactFrame : M.Startup;
				Bite = bBit ? 2 : 3;
			}
			Timing = RL::LeadBucket(Anchor - Press);
		}
		Out[RL::TokTiming] = static_cast<int8_t>(Timing);
		Out[RL::TokBite] = static_cast<int8_t>(Bite);
	}

	void FRLObserver::FinaliseTokens(int32_t Now, bool bAll)
	{
		// Tokens close in the order they opened, so the finalisable ones are always a prefix of Open[].
		int32_t N = 0;
		while (N < NumOpen)
		{
			const FOpenToken& T = Open[N];
			const bool bReady = bAll || (T.CloseFrame >= 0 && T.CloseFrame + SkillP.Perception <= Now);
			if (!bReady) { break; }
			if (Session != nullptr)
			{
				int8_t Row[RL::TokenFields];
				EncodeToken(T, Row);
				Session->PushToken(Row);
			}
			++N;
		}
		if (N == 0) { return; }
		for (int32_t I = N; I < NumOpen; ++I) { Open[I - N] = Open[I]; }
		NumOpen -= N;
	}

	int32_t FRLObserver::AnswerToLastDecision() const
	{
		// A decision always leaves its token (opened or extended) as the newest open one until the next decision.
		if (LastDecision < 0 || NumOpen <= 0) { return -1; }
		return SymIndex(AnswerOf(Open[NumOpen - 1]));
	}

	bool FRLObserver::PopReadMeter(FReadMeterEvent& Out)
	{
		if (!bReadMeterReady) { return false; }
		Out = ReadMeter;
		bReadMeterReady = false;
		return true;
	}
}
