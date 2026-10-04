// HellwalkerRL — engine-free core. Headless arena, simulated players, encounter runner.

#include "HWCore/HWSim.h"

#include <cmath>

namespace HW
{
	namespace
	{
		FVec2 Sub(const FVec2& A, const FVec2& B) { return FVec2{ A.X - B.X, A.Y - B.Y }; }
		FVec2 AddScaled(const FVec2& A, const FVec2& B, float S) { return FVec2{ A.X + B.X * S, A.Y + B.Y * S }; }
		float Dot(const FVec2& A, const FVec2& B) { return A.X * B.X + A.Y * B.Y; }
		float Len(const FVec2& A) { return std::sqrt(A.X * A.X + A.Y * A.Y); }
		FVec2 Norm(const FVec2& A)
		{
			const float L = Len(A);
			return L > 1.0e-4f ? FVec2{ A.X / L, A.Y / L } : FVec2{ 1.f, 0.f };
		}
		// Left of a facing, counter-clockwise (x right, y up).
		FVec2 LeftOf(const FVec2& F) { return FVec2{ -F.Y, F.X }; }
		FVec2 Neg(const FVec2& A) { return FVec2{ -A.X, -A.Y }; }

		FVec2 DirVector(EDir Dir, const FVec2& Facing)
		{
			switch (Dir)
			{
			case EDir::Forward: return Facing;
			case EDir::Back:    return Neg(Facing);
			case EDir::Left:    return LeftOf(Facing);
			case EDir::Right:   return Neg(LeftOf(Facing));
			default:            return FVec2{};
			}
		}
	}

	// ----------------------------------------------------------------------------------------------
	// Arena
	// ----------------------------------------------------------------------------------------------

	void FSimArena::Reset(float StartDistance)
	{
		Pos[0] = FVec2{ 0.f, 0.f };
		Pos[1] = FVec2{ StartDistance, 0.f };
		StepDir[0] = StepDir[1] = FVec2{};
		PreMove[0] = PreMove[1] = EMoveId::None;
		PreT[0] = PreT[1] = 0;
		PreState[0] = PreState[1] = EFighterState::Idle;
		PreGuard[0] = PreGuard[1] = false;
	}

	float FSimArena::Distance() const { return Len(Sub(Pos[1], Pos[0])); }

	FVec2 FSimArena::FacingOf(ESide Side) const
	{
		const int32_t I = SideIndex(Side);
		return Norm(Sub(Pos[1 - I], Pos[I]));
	}

	void FSimArena::LatchCommits(FDuel& Duel)
	{
		for (int32_t I = 0; I < 2; ++I)
		{
			FFighter& F = Duel.Fighters[I];
			if (F.State != EFighterState::Acting || F.T != 0 || F.CommitFrame != Duel.Frame) { continue; }
			const FVec2 Face = FacingOf(static_cast<ESide>(I));
			F.CommitFacingX = Face.X;
			F.CommitFacingY = Face.Y;
			StepDir[I] = DirVector(F.CurrentMove().Dir, Face);
		}
	}

	void FSimArena::SnapshotPreStep(const FDuel& Duel)
	{
		for (int32_t I = 0; I < 2; ++I)
		{
			const FFighter& F = Duel.Fighters[I];
			PreMove[I] = F.State == EFighterState::Acting ? F.Move : EMoveId::None;
			PreT[I] = F.T;
			PreState[I] = F.State;
			PreGuard[I] = F.bGuardHeld;
		}
	}

	void FSimArena::Integrate(const FDuel& Duel, float PlayerFwd, float PlayerLat)
	{
		const FCombatTuning& K = Tuning();
		bool bStepping = false;
		for (int32_t I = 0; I < 2; ++I)
		{
			if (PreMove[I] == EMoveId::None) { continue; }
			const FMoveData& M = Move(PreMove[I]);
			if (PreT[I] >= M.Active || M.Active <= 0) { continue; }
			const float PerFrame = M.Distance / static_cast<float>(M.Active);
			if (M.Kind == EMoveKind::Step)
			{
				Pos[I] = AddScaled(Pos[I], StepDir[I], PerFrame);
				bStepping = true;
			}
			else if (M.Kind == EMoveKind::Locomotion)
			{
				const FVec2 Face = FacingOf(static_cast<ESide>(I)); // locomotion re-aims every frame
				if (M.Dir == EDir::Forward && Distance() <= ApproachStopDistance) { continue; }
				Pos[I] = AddScaled(Pos[I], DirVector(M.Dir, Face), PerFrame);
			}
		}

		// Player walking (locomotion is free when idle; guard slows it).
		if (PreState[0] == EFighterState::Idle && !Duel.Fighters[0].IsDead())
		{
			const float Speed = K.PlayerWalkSpeed / static_cast<float>(FramesPerSecond) * (PreGuard[0] ? 0.45f : 1.f);
			const FVec2 Face = FacingOf(ESide::Player);
			FVec2 V = AddScaled(FVec2{}, Face, PlayerFwd);
			V = AddScaled(V, LeftOf(Face), PlayerLat);
			const float L = Len(V);
			if (L > 1.f) { V.X /= L; V.Y /= L; }
			Pos[0] = AddScaled(Pos[0], V, Speed);
		}

		// Bodies do not overlap unless someone is ghoststepping through.
		if (!bStepping)
		{
			const FVec2 D = Sub(Pos[1], Pos[0]);
			const float L = Len(D);
			if (L < MinSeparation)
			{
				const FVec2 N = Norm(D);
				const float Push = (MinSeparation - L) * 0.5f;
				Pos[0] = AddScaled(Pos[0], N, -Push);
				Pos[1] = AddScaled(Pos[1], N, Push);
			}
		}

		for (FVec2& P : Pos)
		{
			const float R = Len(P);
			if (R > ArenaRadius) { P.X *= ArenaRadius / R; P.Y *= ArenaRadius / R; }
		}
	}

	bool FSimArena::Contacts(const FDuel& Duel, ESide Attacker)
	{
		const int32_t A = SideIndex(Attacker);
		const FFighter& F = Duel.Fighters[A];
		const FMoveData& M = F.CurrentMove();
		const FVec2 Face = Norm(FVec2{ F.CommitFacingX, F.CommitFacingY });
		const FVec2 R = Sub(Pos[1 - A], Pos[A]);
		const float Along = Dot(R, Face);
		const float LatV = Dot(R, LeftOf(Face)); // defender's LEFT is negative here (it faces the attacker)
		if (Along < -BodyRadius || Along > M.Range) { return false; }
		const float Half = M.HalfWidth + BodyRadius;
		switch (M.Coverage)
		{
		case ECoverage::SweepLeft:  return LatV >= -M.SweepReach && LatV <= Half;
		case ECoverage::SweepRight: return LatV <= M.SweepReach && LatV >= -Half;
		case ECoverage::Wide:       return std::fabs(LatV) <= M.SweepReach;
		default:                    return std::fabs(LatV) <= Half;
		}
	}

	// ----------------------------------------------------------------------------------------------
	// Simulated players
	// ----------------------------------------------------------------------------------------------

	const char* BotKindName(EBotKind K)
	{
		switch (K)
		{
		case EBotKind::Masher:        return "Masher";
		case EBotKind::Turtle:        return "Turtle";
		case EBotKind::Habitual:      return "Habitual";
		case EBotKind::Varied:        return "Varied";
		case EBotKind::DodgerLeft:    return "DodgerLeft";
		case EBotKind::RhythmParrier: return "RhythmParrier";
		case EBotKind::Habit:         return "Habit";
		default:                      return "?";
		}
	}

	float HabitWeights(const FBotProfile& P, const FBotMemory* Mem, int32_t Phase, int32_t C, float OutW[FBotProfile::HabitResponses])
	{
		// A learning player tilts its habit by what each answer has earned against this keeper (a small floor keeps every
		// answer reachable, so a habit can be unlearned into something it never did before).
		const float* Row = P.Habit[Phase > 0 ? 1 : 0][C];
		const bool bLearn = P.LearnRate > 0.f && Mem != nullptr;
		const float Temp = P.LearnTemp > 0.05f ? P.LearnTemp : 0.05f;
		float Sum = 0.f;
		for (int32_t I = 0; I < FBotProfile::HabitResponses; ++I)
		{
			float W = Row[I] > 0.f ? Row[I] : 0.f;
			if (P.LearnRate > 0.f) { W = (W + 0.02f) * (bLearn ? std::exp(Mem->Q[C][I] / Temp) : 1.f); }
			OutW[I] = W;
			Sum += W;
		}
		return Sum;
	}

	float HabitChoiceProbability(const FBotProfile& P, const FBotMemory* Mem, int32_t Phase, int32_t C, int32_t R)
	{
		if (C < 0 || C >= FBotProfile::HabitClasses || R < 0 || R >= FBotProfile::HabitResponses) { return 0.f; }
		float W[FBotProfile::HabitResponses];
		const float Sum = HabitWeights(P, Mem, Phase, C, W);
		return Sum > 0.f ? W[R] / Sum : 0.f;
	}

	FBotProfile MakeBotProfile(EBotKind Kind, float Skill)
	{
		FBotProfile P;
		P.Kind = Kind;
		P.Skill = Skill;
		P.ReactMean = 18.f - 10.f * Skill;
		P.ReactSigma = 3.5f - 1.5f * Skill;
		P.TimingSigma = 4.5f - 3.0f * Skill;
		P.FeintRead = 0.10f + 0.45f * Skill;
		P.KillerRead = 0.40f + 0.55f * Skill;
		P.PunishRate = 0.25f + 0.65f * Skill;
		P.AggroRate = 0.15f + 0.25f * Skill;
		P.ChainLen = Skill > 0.6f ? 3 : 2;
		P.HeavyRate = 0.15f;
		switch (Kind)
		{
		case EBotKind::Masher:
			P.ReactMean = 26.f; P.TimingSigma = 6.f; P.FeintRead = 0.f; P.KillerRead = 0.1f;
			P.PunishRate = 0.9f; P.AggroRate = 0.95f; P.ChainLen = 3; P.HeavyRate = 0.25f;
			break;
		case EBotKind::Habitual:
			P.TimingSigma = 1.2f + 1.3f * (1.f - Skill); // a rhythm: consistent, therefore readable
			P.FeintRead = 0.05f + 0.15f * Skill;
			break;
		case EBotKind::RhythmParrier:
			P.TimingSigma = 1.0f;
			P.FeintRead = 0.05f;
			break;
		case EBotKind::Varied:
			P.SwitchRate = 0.25f;
			break;
		case EBotKind::Turtle:
			P.AggroRate = 0.05f;
			P.PunishRate = 0.35f;
			break;
		default:
			break;
		}
		return P;
	}

	int32_t HabitClassOf(const FMoveData& M)
	{
		if (M.Owner != ESide::Boss || !M.IsAttack()) { return -1; }
		switch (M.Symbol)
		{
		case ESym::BFast:   return 0;
		case ESym::BHeavy:  return 1;
		case ESym::BFeint:  return 2;
		case ESym::BKiller: return 3;
		default:            return -1;
		}
	}

	void FPlayerBot::Reset(const FBotProfile& InProfile, int32_t Seed)
	{
		*this = FPlayerBot{};
		Prof = InProfile;
		Rng.Initialize(Seed);
	}

	ESym FPlayerBot::MovementSym() const
	{
		if (Fwd > 0.3f) { return ESym::Advance; }
		if (Fwd < -0.3f) { return ESym::Retreat; }
		return ESym::Neutral;
	}

	void FPlayerBot::OnEvents(const std::vector<FDuelEvent>& Events, const FDuel& Duel)
	{
		for (const FDuelEvent& E : Events)
		{
			// Learning players: what the response to this swing earned (the swing is the boss's current one).
			if (Prof.LearnRate > 0.f && E.Type == EDuelEvent::Outcome && E.Side == ESide::Boss && RespClass >= 0
				&& Duel.Get(ESide::Boss).CommitFrame == RespSwingFrame)
			{
				float R = 0.f;
				switch (E.Outcome)
				{
				case EHitOutcome::Hit:     R = Move(E.Move).bUnblockable ? -1.5f : -1.f; break;
				case EHitOutcome::Blocked: R = 0.25f; break;
				case EHitOutcome::Whiff:   R = 1.f; break;
				case EHitOutcome::Parried: R = 1.5f; break;
				default: break;
				}
				FBotMemory& Mem = Memory != nullptr ? *Memory : OwnMemory;
				float& Q = Mem.Q[RespClass][RespColumn];
				Q += Prof.LearnRate * (R - Q);
				++Mem.Updates;
				RespClass = -1;
			}
			if (E.Type == EDuelEvent::Commit && E.Side == ESide::Boss && Move(E.Move).IsAttack())
			{
				FeintWariness *= 0.96f; // fades a little with every swing
				SideFlip *= 0.97f;
			}
			if (E.Type != EDuelEvent::Outcome || E.Side != ESide::Boss || E.Outcome != EHitOutcome::Hit || !Prof.bAdapts) { continue; }
			const FMoveData& M = Move(E.Move);
			if (M.FakeImpactFrame >= 0)
			{
				FeintWariness += 0.12f * (0.5f + Prof.Skill);
				if (FeintWariness > 0.6f) { FeintWariness = 0.6f; }
			}
			if (M.Coverage == ECoverage::SweepLeft || M.Coverage == ECoverage::SweepRight)
			{
				SideFlip += 0.15f * (0.5f + Prof.Skill);
				if (SideFlip > 0.5f) { SideFlip = 0.5f; }
			}
		}
	}

	FPlayerBot::EResp FPlayerBot::ChooseResponse(const FMoveData& M, bool& bOutSawFeint)
	{
		const bool bKiller = M.bUnblockable;
		const bool bSeesKiller = bKiller && Rng.Chance(Prof.KillerRead);
		const bool bFeint = M.FakeImpactFrame >= 0;
		bOutSawFeint = bFeint && Rng.Chance(Prof.FeintRead + FeintWariness);
		if (Prof.Kind == EBotKind::Masher) { bOutSawFeint = false; }
		const EResp Picked = ChooseResponseCore(M, bKiller, bSeesKiller);
		if (Prof.Kind != EBotKind::Masher && Prof.bAdapts && SideFlip > 0.f && (Picked == EResp::StepL || Picked == EResp::StepR) && Rng.Chance(SideFlip))
		{
			return Picked == EResp::StepL ? EResp::StepR : EResp::StepL;
		}
		return Picked;
	}

	FPlayerBot::EResp FPlayerBot::ChooseResponseCore(const FMoveData& M, bool bKiller, bool bSeesKiller)
	{

		switch (Prof.Kind)
		{
		case EBotKind::Masher:
		{
			const float R = Rng.FRand();
			if (bSeesKiller) { return EResp::StepB; }
			return R < 0.85f ? EResp::Attack : (R < 0.95f ? EResp::Block : EResp::StepB);
		}
		case EBotKind::Turtle:
		{
			if (bSeesKiller) { return Rng.Chance(0.6f) ? EResp::StepB : EResp::StepL; }
			const float R = Rng.FRand();
			return R < 0.72f ? EResp::Block : (R < 0.9f ? EResp::Parry : EResp::StepB);
		}
		case EBotKind::DodgerLeft:
			return EResp::StepL;
		case EBotKind::RhythmParrier:
			if (bSeesKiller) { return EResp::StepB; }
			return EResp::Parry;
		case EBotKind::Habitual:
		{
			// Strong, consistent habits: parry the fast ones, dodge LEFT from the heavy ones.
			if (M.Id == EMoveId::BGrab) { return bSeesKiller ? EResp::StepB : EResp::Parry; }
			if (bKiller) { return bSeesKiller ? EResp::StepL : EResp::Block; }
			if (M.Symbol == ESym::BHeavy) { return Rng.Chance(0.85f) ? EResp::StepL : EResp::Block; }
			return Rng.Chance(0.85f) ? EResp::Parry : EResp::StepL; // Fast, Feint
		}
		case EBotKind::Habit:
		{
			static constexpr EResp Resp[FBotProfile::HabitResponses] = {
				EResp::Parry, EResp::Block, EResp::StepL, EResp::StepR, EResp::StepB, EResp::StepF, EResp::Attack, EResp::None };
			const int32_t C = HabitClassOf(M);
			RespClass = -1;
			if (C < 0 || Rng.Chance(Prof.HabitNoise)) { return Resp[Rng.RandHelper(FBotProfile::HabitResponses)]; }
			float Wt[FBotProfile::HabitResponses];
			const bool bLearn = Prof.LearnRate > 0.f;
			const float Sum = HabitWeights(Prof, Memory != nullptr ? Memory : &OwnMemory, HabitPhase, C, Wt);
			if (Sum <= 0.f) { return EResp::None; }
			float R = Rng.FRand() * Sum;
			EResp Picked = EResp::None;
			int32_t Column = FBotProfile::HabitResponses - 1;
			for (int32_t I = 0; I < FBotProfile::HabitResponses; ++I)
			{
				if (R < Wt[I]) { Picked = Resp[I]; Column = I; break; }
				R -= Wt[I];
			}
			if (bLearn)
			{
				RespClass = C;
				RespColumn = Column;
			}
			// Seeing the killer's red mark overrides a block / parry habit: those cannot stop it.
			if (bSeesKiller && (Picked == EResp::Block || Picked == EResp::Parry)) { return EResp::StepB; }
			return Picked;
		}
		case EBotKind::Varied:
		default:
		{
			if (bSeesKiller)
			{
				const int32_t R = Rng.RandRange(0, 2);
				return R == 0 ? EResp::StepL : (R == 1 ? EResp::StepR : EResp::StepB);
			}
			static constexpr EResp Mix[] = { EResp::Parry, EResp::Parry, EResp::StepL, EResp::StepR, EResp::StepB, EResp::Block };
			return Mix[Rng.RandHelper(6)];
		}
		}
	}

	void FPlayerBot::Act(FDuel& Duel, const FSimArena& Arena, float& OutFwd, float& OutLat)
	{
		FFighter& Me = Duel.Get(ESide::Player);
		const FFighter& Boss = Duel.Get(ESide::Boss);
		const int32_t Now = Duel.Frame;
		const float Dist = Arena.Distance();
		Fwd = 0.f;
		Lat = 0.f;

		if (Me.IsDead() || Boss.IsDead())
		{
			OutFwd = OutLat = 0.f;
			return;
		}
		++DiagC.Frames;
		DiagC.DistanceSum += Dist;

		// ---- Defence: plan a response once per boss swing, after a reaction delay -----------------
		const bool bBossSwinging = Boss.State == EFighterState::Acting && Boss.CurrentMove().IsAttack() && !Boss.bSwingResolved;
		if (bBossSwinging && PlannedCommitFrame != Boss.CommitFrame)
		{
			if (RecogniseAt < 0 || RecogniseAt < Boss.CommitFrame)
			{
				RecogniseAt = Boss.CommitFrame + static_cast<int32_t>(std::lround(Prof.ReactMean + Rng.Gaussian() * Prof.ReactSigma));
				if (RecogniseAt < Boss.CommitFrame + 4) { RecogniseAt = Boss.CommitFrame + 4; }
			}
			if (Now >= RecogniseAt)
			{
				const FMoveData& M = Boss.CurrentMove();
				bool bSawFeint = false;
				Planned = ChooseResponse(M, bSawFeint);
				PlannedCommitFrame = Boss.CommitFrame;
				PlannedPhase = HabitPhase;
				RespSwingFrame = Boss.CommitFrame;
				bExecuted = false;
				const int32_t Impact = (M.FakeImpactFrame >= 0 && !bSawFeint) ? M.FakeImpactFrame : M.Startup;
				const float Aim = Planned == EResp::Parry ? Prof.ParryAim : Prof.StepAim;
				const int32_t Noise = static_cast<int32_t>(std::lround(Rng.Gaussian() * Prof.TimingSigma));
				ExecuteAt = Boss.CommitFrame + Impact - static_cast<int32_t>(Aim) + Noise;
				GiveUpAt = Boss.CommitFrame + M.Startup + M.Active;
				if (Planned == EResp::Block || Planned == EResp::Attack) { ExecuteAt = Now; }
				if (Planned != EResp::Attack) { StringLeft = 0; } // a defender stops its own string
			}
		}

		if (!bExecuted && Now >= ExecuteAt)
		{
			switch (Planned)
			{
			case EResp::Parry:
				if (Duel.Commit(ESide::Player, EMoveId::PParry)) { bExecuted = true; }
				break;
			case EResp::StepL:
			case EResp::StepR:
			case EResp::StepB:
			case EResp::StepF:
			{
				const EDir D = Planned == EResp::StepL ? EDir::Left : Planned == EResp::StepR ? EDir::Right
					: Planned == EResp::StepB ? EDir::Back : EDir::Forward;
				const FVec2 Face = Arena.FacingOf(ESide::Player);
				if (Duel.Commit(ESide::Player, StepFor(D), Face.X, Face.Y)) { bExecuted = true; }
				else if (!Duel.CanCommit(ESide::Player, StepFor(D)) && Me.IsActionable())
				{
					Planned = EResp::Block; // out of sha-chi for a ghoststep: it guards instead (and learns about guarding)
					bExecuted = false;
					if (RespClass >= 0) { RespColumn = 1; }
				}
				break;
			}
			case EResp::Block:
				GuardUntil = GiveUpAt + 10;
				bExecuted = true;
				break;
			case EResp::Attack:
				StringLeft = 1;
				bExecuted = true;
				++DiagC.ResponseAttacks;
				break;
			default:
				bExecuted = true;
				break;
			}
			if (!bExecuted && Now >= GiveUpAt) { bExecuted = true; }
		}

		// ---- Offence ------------------------------------------------------------------------------
		const bool bDefencePending = !bExecuted && Planned != EResp::Attack && Prof.Kind != EBotKind::Masher;
		const bool bInRange = Dist <= 185.f;
		const bool bBossOpen = Boss.State == EFighterState::Stagger || Boss.State == EFighterState::GuardBroken
			|| Boss.State == EFighterState::Hitstun
			|| (Boss.State == EFighterState::Acting && Boss.IsInRecovery() && Boss.FramesUntilActionable() > 12);
		const bool bActionable = Me.IsActionable();
		DiagC.Actionable += bActionable ? 1 : 0;
		DiagC.InRange += bInRange ? 1 : 0;
		DiagC.InRangeActionable += (bInRange && bActionable) ? 1 : 0;
		DiagC.BossOpen += bBossOpen ? 1 : 0;
		DiagC.BossOpenInRange += (bBossOpen && bInRange) ? 1 : 0;
		DiagC.BossSwinging += bBossSwinging ? 1 : 0;
		DiagC.DefencePending += bDefencePending ? 1 : 0;
		if (bActionable && !bDefencePending)
		{
			const int32_t WindowId = Boss.CommitFrame * 4 + static_cast<int32_t>(Boss.State);
			if (StringLeft == 0 && bBossOpen && bInRange && PunishedWindowFrame != WindowId)
			{
				PunishedWindowFrame = WindowId;
				if (Rng.Chance(Prof.PunishRate))
				{
					bStringHeavy = Boss.State == EFighterState::GuardBroken || Rng.Chance(Prof.HeavyRate);
					StringLeft = bStringHeavy ? 1 : Prof.ChainLen;
					++DiagC.PunishStarts;
				}
			}
			if (StringLeft == 0 && bInRange && !bBossSwinging && Now >= NextAggroCheck)
			{
				NextAggroCheck = Now + 20;
				if (Rng.Chance(Prof.AggroRate))
				{
					bStringHeavy = Rng.Chance(Prof.HeavyRate);
					StringLeft = bStringHeavy ? 1 : Prof.ChainLen;
					++DiagC.AggroStarts;
				}
			}
			if (StringLeft > 0)
			{
				if (!bInRange && Prof.Kind != EBotKind::Masher)
				{
					StringLeft = 0;
				}
				else
				{
					const FVec2 Face = Arena.FacingOf(ESide::Player);
					if (Duel.CommitPlayerAttack(bStringHeavy, Face.X, Face.Y))
					{
						++DiagC.AttackCommits;
						--StringLeft;
						if (StringLeft == 0 && Prof.SwitchRate > 0.f && Rng.Chance(Prof.SwitchRate)) { StringLeft = -1; }
					}
				}
			}
			else if (StringLeft < 0)
			{
				if (Duel.CommitSwitch()) { StringLeft = 0; }
			}
		}

		// ---- Guard and footwork -------------------------------------------------------------------
		Duel.SetGuardHeld(ESide::Player, Now < GuardUntil);
		if (Me.CanMove())
		{
			if (Dist > Prof.PreferredRange + 25.f) { Fwd = 1.f; }
			else if (Dist < Prof.PreferredRange - 70.f) { Fwd = -0.6f; }
		}
		DiagC.WalkFwd += Fwd > 0.f ? 1 : 0;
		DiagC.WalkBack += Fwd < 0.f ? 1 : 0;
		DiagC.Guarding += Now < GuardUntil ? 1 : 0;
		OutFwd = Fwd;
		OutLat = Lat;
	}

	// ----------------------------------------------------------------------------------------------
	// Runner
	// ----------------------------------------------------------------------------------------------

	FEncounterStats RunEncounter(IBossBrain& Brain, const FRunConfig& Cfg, std::vector<FExchangeRow>* OutRows)
	{
		FEncounter Enc;
		Enc.bRecordRows = OutRows != nullptr;
		Enc.Begin(&Brain, Cfg.Seed, Cfg.bImmortal);
		Enc.Duel.KeeperDamageScale = Cfg.KeeperDamageScale;
		if (!Cfg.bImmortal && Cfg.BossHealthScale != 1.f)
		{
			FFighter& B = Enc.Duel.Get(ESide::Boss);
			B.HealthMax *= Cfg.BossHealthScale;
			B.Health = B.HealthMax;
		}

		FSimArena Arena;
		Arena.Reset(Cfg.StartDistance);
		FPlayerBot Bot;
		Bot.Reset(Cfg.Bot, Cfg.Seed * 7919 + 17);

		std::vector<FExchangeRow> Rows;
		for (int32_t F = 0; F < Cfg.MaxFrames; ++F)
		{
			Enc.ThinkBoss(Arena.Geometry());
			float PFwd = 0.f;
			float PLat = 0.f;
			Bot.Act(Enc.Duel, Arena, PFwd, PLat);
			Arena.LatchCommits(Enc.Duel);
			Arena.SnapshotPreStep(Enc.Duel);
			Enc.StepFrame(Arena, Bot.MovementSym());
			Bot.OnEvents(Enc.FrameEvents(), Enc.Duel);
			Arena.Integrate(Enc.Duel, PFwd, PLat);
			if (OutRows != nullptr)
			{
				Enc.TakeRows(Rows);
				OutRows->insert(OutRows->end(), Rows.begin(), Rows.end());
			}
			if (Enc.IsOver()) { break; }
		}
		return Enc.Stats;
	}
}
