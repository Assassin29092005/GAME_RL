// HellwalkerRL — engine-free core. The RL keeper's done-tests (RL/DESIGN.md §1 - §6): the action space, masks and
// fairness rules, perception delay, history tokens, the read head's labels and READ banner, handedness, the .hwrl
// format + forward pass, the brain's determinism and the session memory.
//
// Run by ThesisSim --tests (after the game core's and the classic brain's) and by the Unreal automation wrapper
// ("Project.HellwalkerRL.RL.<Name>"). Every test builds what it needs in memory: no files, no randomness but FRandom.

#include "HWCore/HWCoreTests.h"

#include "HWCore/HWRLBrain.h"
#include "HWCore/HWSim.h"

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace HW
{
	// A named namespace, not an anonymous one: Unreal's unity builds paste HWCoreTests.cpp (which has its own
	// anonymous-namespace helpers) into the same translation unit.
	namespace RLTestImpl
	{
		void Logf(std::string& Log, const char* Fmt, ...)
		{
			char Buf[512];
			va_list Args;
			va_start(Args, Fmt);
			std::vsnprintf(Buf, sizeof(Buf), Fmt, Args);
			va_end(Args);
			Log += Buf;
			Log += '\n';
		}

		class FScriptedOracle : public IContactOracle
		{
		public:
			bool bPlayerHits = false;
			bool bBossHits = false;
			bool Contacts(const FDuel& Duel, ESide Attacker) override
			{
				(void)Duel;
				return Attacker == ESide::Player ? bPlayerHits : bBossHits;
			}
		};

		/** A duel stepped by hand in the RecordFrame protocol: [decide] <player input> Step, RecordFrame. */
		struct FHandDuel
		{
			FDuel Duel;
			FRLSession Session;
			FRLObserver Obs;
			FScriptedOracle Oracle;
			FDuelGeometry Geo;
			std::vector<FDuelEvent> Events;

			void BeginAt(const FDuelGeometry& G)
			{
				Duel.Reset();
				Session.Reset();
				Geo = G;
				Obs.BeginEncounter(&Session, Duel, Geo);
			}
			void Begin(float Distance)
			{
				FDuelGeometry G;
				G.PlayerX = 0.f;
				G.PlayerY = 0.f;
				G.BossX = Distance;
				G.BossY = 0.f;
				BeginAt(G);
			}
			void Step(ESym Movement = ESym::Neutral)
			{
				Duel.Step(Oracle);
				Duel.TakeEvents(Events);
				Obs.RecordFrame(Events, Duel, Geo, Movement);
			}
		};

		bool SameRow(const int8_t* Row, std::initializer_list<int32_t> Expect)
		{
			int32_t F = 0;
			for (int32_t V : Expect)
			{
				if (Row[F++] != V) { return false; }
			}
			return true;
		}

		void RowText(const int8_t* Row, char* Out, size_t Size)
		{
			std::snprintf(Out, Size, "{%d %d %d %d %d %d}", Row[0], Row[1], Row[2], Row[3], Row[4], Row[5]);
		}

		/** A procedural habit player (RL.md §6) with a clear table, for the population-shaped fights below. */
		FBotProfile HabitProfile()
		{
			FBotProfile P = MakeBotProfile(EBotKind::Varied, 0.7f);
			P.Kind = EBotKind::Habit;
			P.SwitchRate = 0.f;
			// [table][class: fast, heavy, feint, killer][Parry, Block, StepL, StepR, StepB, StepF, Attack, None]
			P.Habit[0][0][2] = 8.f; P.Habit[0][0][0] = 1.f;
			P.Habit[0][1][0] = 6.f; P.Habit[0][1][1] = 2.f;
			P.Habit[0][2][1] = 5.f; P.Habit[0][2][6] = 2.f;
			P.Habit[0][3][4] = 5.f; P.Habit[0][3][3] = 1.f;
			P.HabitNoise = 0.05f;
			return P;
		}

		// ------------------------------------------------------------------------------------------
		// Observation sanity: every feature inside its documented range, one-hots well formed, tokens valid
		// ------------------------------------------------------------------------------------------
		int32_t CountBadObservation(const float* O, const int8_t* Tok, int32_t NumTokens, std::string& FirstProblem)
		{
			int32_t Bad = 0;
			auto Fail = [&Bad, &FirstProblem, O](const char* What, int32_t I)
			{
				++Bad;
				if (FirstProblem.empty())
				{
					char Buf[192];
					std::snprintf(Buf, sizeof(Buf), "%s: feature %d (%s) = %.4f", What, I, RL::ObsFeatureName(I), O[I]);
					FirstProblem = Buf;
				}
			};
			for (int32_t I = 0; I < RL::ObsDim; ++I)
			{
				float Lo = 0.f;
				float Hi = 1.f;
				switch (I)
				{
				case RL::ObsSelfUntilActionable:
				case RL::ObsPlayerUntilActionable:
				case RL::ObsPlayerSinceCommit:   Hi = 2.f; break;
				case RL::ObsPlayerDistance:      Hi = 3.f; break;
				case RL::ObsPlayerRadialVel:
				case RL::ObsPlayerLateralVel:
				case RL::ObsFrameAdvantage:      Lo = -2.f; Hi = 2.f; break;
				case RL::ObsPlayerBearingSin:
				case RL::ObsPlayerBearingCos:
				case RL::ObsSwingDeficit:        Lo = -1.f; break;
				default: break;
				}
				if (!std::isfinite(O[I]) || O[I] < Lo || O[I] > Hi) { Fail("out of range", I); }
			}
			struct FGroup { int32_t First; int32_t Count; bool bExactlyOne; };
			const FGroup Groups[] = {
				{ RL::ObsSelfState, 7, true }, { RL::ObsSelfMove, RL::NumBossMoves, false }, { RL::ObsSelfPhase, 4, false },
				{ RL::ObsSelfLastOutcome, 5, true }, { RL::ObsPlayerState, 7, true }, { RL::ObsPlayerMove, 13, false },
				{ RL::ObsPlayerPhase, 4, false }, { RL::ObsPlayerWeapon, 2, true }, { RL::ObsPlayerLastOutcome, 5, true },
			};
			for (const FGroup& G : Groups)
			{
				float Sum = 0.f;
				bool bBinary = true;
				for (int32_t K = 0; K < G.Count; ++K)
				{
					const float V = O[G.First + K];
					Sum += V;
					bBinary = bBinary && (V == 0.f || V == 1.f);
				}
				if (!bBinary || Sum > 1.f || (G.bExactlyOne && Sum != 1.f)) { Fail("one-hot", G.First); }
			}
			// A phase is shown exactly when a move is running.
			float SelfPhase = 0.f;
			float PlayerPhase = 0.f;
			for (int32_t K = 0; K < 4; ++K) { SelfPhase += O[RL::ObsSelfPhase + K]; PlayerPhase += O[RL::ObsPlayerPhase + K]; }
			if (SelfPhase != O[RL::ObsSelfState + static_cast<int32_t>(EFighterState::Acting)]) { Fail("self phase vs Acting", RL::ObsSelfPhase); }
			if (PlayerPhase != O[RL::ObsPlayerState + static_cast<int32_t>(EFighterState::Acting)]) { Fail("player phase vs Acting", RL::ObsPlayerPhase); }
			const int32_t Flags[] = { RL::ObsSelfArmor, RL::ObsSelfInvulnerable, RL::ObsSelfDefended, RL::ObsSelfChainWindow,
				RL::ObsPlayerGuardHeld, RL::ObsPlayerGuarding, RL::ObsPlayerParryLive, RL::ObsPlayerInvulnerable, RL::ObsPlayerArmor };
			for (int32_t I : Flags)
			{
				if (O[I] != 0.f && O[I] != 1.f) { Fail("flag not 0/1", I); }
			}
			for (int32_t K = 0; K < RL::HistoryTokens; ++K)
			{
				const int8_t* Row = Tok + K * RL::TokenFields;
				for (int32_t F = 0; F < RL::TokenFields; ++F)
				{
					const bool bOk = K < NumTokens ? (Row[F] >= 1 && Row[F] < RL::TokenVocab[F]) : Row[F] == 0;
					if (!bOk)
					{
						++Bad;
						if (FirstProblem.empty())
						{
							char Buf[128];
							std::snprintf(Buf, sizeof(Buf), "token row %d field %d = %d (%d tokens)", K, F, Row[F], NumTokens);
							FirstProblem = Buf;
						}
					}
				}
			}
			return Bad;
		}

		// ------------------------------------------------------------------------------------------
		// A brain that picks uniformly among the MASKED-allowed actions, and checks every fairness rule independently
		// ------------------------------------------------------------------------------------------
		struct FLegality
		{
			int32_t Fights = 0;
			int32_t Decisions = 0;
			int32_t Waits = 0;
			int32_t Continues = 0;
			int32_t Attacks = 0;
			int32_t Chains = 0;
			int32_t Grabs = 0;
			int32_t Killers = 0;
			int32_t NonAttacks = 0;
			int32_t MaskMismatches = 0;
			int32_t Violations = 0;
			int32_t BadObservations = 0;
			int32_t IllegalTotal = 0;
			int32_t MaxTokens = 0;
			int32_t MaxString = 0;
			std::string FirstProblem;
			std::string FirstObsProblem;

			void Problem(const char* Fmt, ...)
			{
				if (!FirstProblem.empty()) { return; }
				char Buf[256];
				va_list Args;
				va_start(Args, Fmt);
				std::vsnprintf(Buf, sizeof(Buf), Fmt, Args);
				va_end(Args);
				FirstProblem = Buf;
			}
		};

		class FUniformBrain : public IBossBrain
		{
		public:
			FRLSession* Session = nullptr;
			FLegality* Stats = nullptr;
			FRLObserver Obs;

			EBrainMode Mode() const override { return EBrainMode::Hellwalker; }
			const char* Name() const override { return "RL test: uniform over the mask"; }
			void BeginEncounter(int32_t Seed) override
			{
				bBegun = false;
				Rng.Initialize(Seed * 31 + 7);
				DecisionCount = 0;
				Last = FBrainDecision{};
				PrevDecision = -100000;
				String = 0;
				LastGrab = -100000;
				LastKiller = -100000;
				GeoHist.clear();
			}
			bool Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision) override;
			void OnFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement) override
			{
				if (bBegun) { Obs.RecordFrame(Events, Duel, Geo, PlayerMovement); }
			}
			bool PopReadMeter(FReadMeterEvent& Out) override { return Obs.PopReadMeter(Out); }
			const FBrainDecision& LastDecision() const override { return Last; }
			int32_t Decisions() const override { return DecisionCount; }
			int32_t CountersLanded() const override { return Obs.ReadCountersLanded(); }

		private:
			FRandom Rng;
			bool bBegun = false;
			int32_t FirstFrame = 0;
			int32_t DecisionCount = 0;
			FBrainDecision Last;
			int32_t PrevDecision = -100000;
			int32_t String = 0;
			int32_t LastGrab = -100000;
			int32_t LastKiller = -100000;
			std::vector<FDuelGeometry> GeoHist; // the geometry at the start of every frame (ThinkBoss's view)
		};

		bool FUniformBrain::Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision)
		{
			if (Session == nullptr || Stats == nullptr) { return false; }
			if (!bBegun)
			{
				Obs.BeginEncounter(Session, Duel, Geo);
				bBegun = true;
				FirstFrame = Duel.Frame;
			}
			const int32_t D = Duel.Frame;
			if (static_cast<int32_t>(GeoHist.size()) <= D) { GeoHist.resize(static_cast<size_t>(D) + 1); }
			GeoHist[static_cast<size_t>(D)] = Geo;
			if (!Obs.IsDecisionPoint(Duel)) { return false; }

			float ObsV[RL::ObsDim];
			int8_t Tok[RL::HistoryTokens * RL::TokenFields];
			Obs.BuildObservation(Duel, ObsV, Tok);
			Stats->BadObservations += CountBadObservation(ObsV, Tok, Session->NumTokens, Stats->FirstObsProblem);
			if (Session->NumTokens > Stats->MaxTokens) { Stats->MaxTokens = Session->NumTokens; }
			uint8_t Mask[RL::NumActions];
			Obs.BuildMask(Duel, Mask);

			// The contract, recomputed here from first principles: perception = the player as it stood PerceptionFrames
			// ago, the keeper as last recorded (the previous frame's geometry; this frame's on the fight's first frame).
			const FFighter& B = Duel.Get(ESide::Boss);
			const bool bChain = B.State == EFighterState::Acting && B.CurrentMove().IsAttack() && B.IsActionable();
			const int32_t Floor = bChain ? ChainStartupFloor(B.CurrentMove().Symbol) : 0;
			const int32_t PFrame = D - RL::PerceptionFrames > FirstFrame ? D - RL::PerceptionFrames : FirstFrame;
			const int32_t KFrame = D - 1 > FirstFrame ? D - 1 : FirstFrame;
			const FDuelGeometry& Pg = GeoHist[static_cast<size_t>(PFrame)];
			const FDuelGeometry& Kg = GeoHist[static_cast<size_t>(KFrame)];
			const float Dist = std::sqrt((Pg.PlayerX - Kg.BossX) * (Pg.PlayerX - Kg.BossX) + (Pg.PlayerY - Kg.BossY) * (Pg.PlayerY - Kg.BossY));
			for (int32_t A = 0; A < RL::NumBossMoves; ++A)
			{
				const EMoveId M = RL::ActionMove(A);
				const FMoveData& Md = Move(M);
				bool bExpect = Duel.CanCommit(ESide::Boss, M);
				bool bBorderline = false;
				if (bExpect && Md.IsAttack())
				{
					const float Reach = Md.Range + RL::ReachSlack;
					bBorderline = std::fabs(Dist - Reach) < 1.0e-3f;
					bExpect = Dist <= Reach;
					if (bExpect && bChain) { bExpect = String < RL::MaxStringAttacks && Md.Startup >= Floor; }
				}
				if (bExpect && M == EMoveId::BGrab) { bExpect = D - LastGrab >= RL::GrabCooldownFrames; }
				if (bExpect && M == EMoveId::BKillerThrust) { bExpect = D - LastKiller >= RL::KillerCooldownFrames; }
				if (Mask[A] != 0 && !Duel.CanCommit(ESide::Boss, M))
				{
					++Stats->Violations;
					Stats->Problem("frame %d: %s allowed but not committable", D, Md.Name);
				}
				if (!bBorderline && (Mask[A] != 0) != bExpect)
				{
					++Stats->MaskMismatches;
					Stats->Problem("frame %d: mask[%s] = %d, contract says %d (perceived distance %.1f, chain %d, string %d)",
						D, Md.Name, Mask[A], bExpect ? 1 : 0, Dist, bChain ? 1 : 0, String);
				}
			}
			if (Mask[RL::ActionWait] != 1) { ++Stats->Violations; Stats->Problem("frame %d: Wait masked", D); }
			if (Dist < 1400.f && std::fabs(ObsV[RL::ObsPlayerDistance] * 500.f - Dist) > 0.05f)
			{
				++Stats->BadObservations;
				if (Stats->FirstObsProblem.empty()) { Stats->FirstObsProblem = "distance feature disagrees with the perceived geometry"; }
			}
			if (D - PrevDecision < RL::DecisionGapFrames)
			{
				++Stats->Violations;
				Stats->Problem("decisions %d frames apart (frame %d)", D - PrevDecision, D);
			}

			int32_t Allowed[RL::NumActions];
			int32_t NumAllowed = 0;
			for (int32_t A = 0; A < RL::NumActions; ++A) { if (Mask[A] != 0) { Allowed[NumAllowed++] = A; } }
			const int32_t Pick = Allowed[Rng.RandHelper(NumAllowed)];
			const EMoveId M = RL::ActionMove(Pick);
			const bool bWalking = M != EMoveId::None && B.State == EFighterState::Acting && B.Move == M && Move(M).Kind == EMoveKind::Locomotion;
			const int32_t IllegalBefore = Obs.IllegalActions();
			const EMoveId C = Obs.ApplyAction(Duel, Pick, nullptr);
			if (Obs.IllegalActions() != IllegalBefore)
			{
				++Stats->Violations;
				Stats->Problem("frame %d: allowed action %s refused", D, RL::ActionName(Pick));
			}
			if (Pick == RL::ActionWait)
			{
				++Stats->Waits;
				if (C != EMoveId::None) { ++Stats->Violations; Stats->Problem("frame %d: Wait committed %s", D, Move(C).Name); }
			}
			else if (C == EMoveId::None)
			{
				if (bWalking) { ++Stats->Continues; }
				else { ++Stats->Violations; Stats->Problem("frame %d: %s did not commit", D, RL::ActionName(Pick)); }
			}
			else if (C != M)
			{
				++Stats->Violations;
				Stats->Problem("frame %d: asked %s, committed %s", D, Move(M).Name, Move(C).Name);
			}
			else if (Move(C).IsAttack())
			{
				++Stats->Attacks;
				if (Dist > Move(C).Range + RL::ReachSlack + 1.0e-3f)
				{
					++Stats->Violations;
					Stats->Problem("frame %d: %s committed at perceived %.1f > reach %.1f", D, Move(C).Name, Dist, Move(C).Range + RL::ReachSlack);
				}
				String = bChain ? String + 1 : 1;
				if (String > Stats->MaxString) { Stats->MaxString = String; }
				if (String > RL::MaxStringAttacks) { ++Stats->Violations; Stats->Problem("frame %d: string of %d attacks", D, String); }
				if (bChain)
				{
					++Stats->Chains;
					if (Move(C).Startup < Floor) { ++Stats->Violations; Stats->Problem("frame %d: chained %s faster than the floor %d", D, Move(C).Name, Floor); }
				}
				if (C == EMoveId::BGrab)
				{
					++Stats->Grabs;
					if (D - LastGrab < RL::GrabCooldownFrames) { ++Stats->Violations; Stats->Problem("frame %d: re-grab after %d frames", D, D - LastGrab); }
					LastGrab = D;
				}
				if (C == EMoveId::BKillerThrust)
				{
					++Stats->Killers;
					if (D - LastKiller < RL::KillerCooldownFrames) { ++Stats->Violations; Stats->Problem("frame %d: killer again after %d frames", D, D - LastKiller); }
					LastKiller = D;
				}
			}
			else
			{
				++Stats->NonAttacks;
				String = 0;
			}
			PrevDecision = D;
			++Stats->Decisions;

			FBrainDecision Dec;
			Dec.Frame = D;
			Dec.ScriptIndex = -1;
			Dec.Kind = EDecisionKind::Policy;
			Dec.Chosen = C;
			++DecisionCount;
			Last = Dec;
			if (OutDecision != nullptr) { *OutDecision = Dec; }
			return true;
		}

		/** Seeded fights of the uniform brain vs every reference bot and a habit player; 3 immortal + 1 lethal each. */
		void RunUniformFights(FLegality& L, int32_t SeedBase)
		{
			struct FCase { EBotKind Kind; float Skill; };
			const FCase Cases[] = {
				{ EBotKind::Masher, 0.1f }, { EBotKind::Turtle, 0.7f }, { EBotKind::Habitual, 0.8f }, { EBotKind::Varied, 0.6f },
				{ EBotKind::DodgerLeft, 0.8f }, { EBotKind::RhythmParrier, 0.8f }, { EBotKind::Habit, 0.7f },
			};
			for (const FCase& Case : Cases)
			{
				FRLSession Memory;
				FUniformBrain Brain;
				Brain.Session = &Memory;
				Brain.Stats = &L;
				for (int32_t F = 0; F < 4; ++F)
				{
					FRunConfig Cfg;
					Cfg.Bot = Case.Kind == EBotKind::Habit ? HabitProfile() : MakeBotProfile(Case.Kind, Case.Skill);
					Cfg.Seed = SeedBase + 97 * F + 13 * static_cast<int32_t>(Case.Kind);
					Cfg.bImmortal = F < 3;
					Cfg.MaxFrames = 60 * FramesPerSecond;
					Cfg.StartDistance = 300.f + 150.f * static_cast<float>(F);
					RunEncounter(Brain, Cfg);
					++L.Fights;
					L.IllegalTotal += Brain.Obs.IllegalActions();
				}
			}
		}

		// ------------------------------------------------------------------------------------------
		// A tiny .hwrl image, written in memory (the exact format of HWRLPolicy.h)
		// ------------------------------------------------------------------------------------------
		class FHwrlWriter
		{
		public:
			FHwrlWriter()
			{
				Raw("HWRL", 4);
				U32(1);
				U32(0); // tensor count, patched by Finish
			}
			void Scalar(const char* Name, float V)
			{
				Header(Name, { 1 });
				F32(V);
			}
			void Random(const char* Name, std::initializer_list<int32_t> Dims, FRandom& Rng, float Scale)
			{
				const int64_t N = Header(Name, Dims);
				for (int64_t I = 0; I < N; ++I) { F32((Rng.GetFraction() * 2.f - 1.f) * Scale); }
			}
			std::vector<uint8_t> Finish()
			{
				for (int32_t B = 0; B < 4; ++B) { Bytes[static_cast<size_t>(8 + B)] = static_cast<uint8_t>((Count >> (8 * B)) & 0xffu); }
				return Bytes;
			}

		private:
			int64_t Header(const char* Name, std::initializer_list<int32_t> Dims)
			{
				const uint32_t Len = static_cast<uint32_t>(std::strlen(Name));
				U32(Len);
				Raw(Name, Len);
				U32(static_cast<uint32_t>(Dims.size()));
				int64_t N = 1;
				for (int32_t Dim : Dims)
				{
					U32(static_cast<uint32_t>(Dim));
					N *= Dim;
				}
				++Count;
				return N;
			}
			void Raw(const void* P, size_t N)
			{
				const uint8_t* B = static_cast<const uint8_t*>(P);
				Bytes.insert(Bytes.end(), B, B + N);
			}
			void U32(uint32_t V)
			{
				const uint8_t B[4] = { static_cast<uint8_t>(V & 0xffu), static_cast<uint8_t>((V >> 8) & 0xffu),
					static_cast<uint8_t>((V >> 16) & 0xffu), static_cast<uint8_t>((V >> 24) & 0xffu) };
				Raw(B, 4);
			}
			void F32(float V)
			{
				uint32_t U = 0;
				std::memcpy(&U, &V, sizeof(U));
				U32(U);
			}

			std::vector<uint8_t> Bytes;
			uint32_t Count = 0;
		};

		struct FFixtureSpec
		{
			int32_t Seed = 1;
			int32_t Enc = 16;
			int32_t Embed = 8;
			int32_t Hidden = 12;
			bool bRecurrent = true;
			int32_t LayoutVersion = RL::ObsLayoutVersion;
			int32_t NumActions = RL::NumActions;
			float PiScale = 2.f;        // policy weights, in units of 1.5/sqrt(fan-in): larger = more decisive
			const char* Skip = nullptr; // leave this tensor out (a corrupt image)
		};

		/** Deterministic random weights of the documented network, in the documented byte format. */
		std::vector<uint8_t> MakeFixture(const FFixtureSpec& S)
		{
			FRandom Rng(S.Seed);
			FHwrlWriter W;
			auto Skipped = [&S](const char* Name) { return S.Skip != nullptr && std::strcmp(S.Skip, Name) == 0; };
			auto Put = [&W, &Rng, &Skipped](const char* Name, std::initializer_list<int32_t> Dims, float Scale)
			{
				if (!Skipped(Name)) { W.Random(Name, Dims, Rng, Scale); }
			};
			auto Meta = [&W, &Skipped](const char* Name, int32_t V)
			{
				if (!Skipped(Name)) { W.Scalar(Name, static_cast<float>(V)); }
			};
			Meta("meta.obs_layout_version", S.LayoutVersion);
			Meta("meta.obs_dim", RL::ObsDim);
			Meta("meta.num_actions", S.NumActions);
			Meta("meta.aux_classes", RL::NumAnswerClasses);
			Meta("meta.enc_hidden", S.Enc);
			Meta("meta.embed_dim", S.Embed);
			Meta("meta.hidden", S.Hidden);
			Meta("meta.recurrent", S.bRecurrent ? 1 : 0);
			Meta("meta.history_tokens", RL::HistoryTokens);
			Meta("meta.token_fields", RL::TokenFields);
			Meta("meta.side", 0);
			const int32_t Z = S.Enc + S.Embed;
			auto Fan = [](int32_t In) { return 1.5f / std::sqrt(static_cast<float>(In)); };
			Put("enc1.weight", { S.Enc, RL::ObsDim }, Fan(RL::ObsDim));
			Put("enc1.bias", { S.Enc }, 0.1f);
			Put("enc2.weight", { S.Enc, S.Enc }, Fan(S.Enc));
			Put("enc2.bias", { S.Enc }, 0.1f);
			for (int32_t F = 0; F < RL::TokenFields; ++F)
			{
				char Name[16];
				std::snprintf(Name, sizeof(Name), "tok.emb%d", F);
				Put(Name, { RL::TokenVocab[F], S.Embed }, 0.6f);
			}
			Put("tok.age", { RL::HistoryTokens, S.Embed }, 0.2f);
			if (S.bRecurrent)
			{
				Put("gru.weight_ih", { 3 * S.Hidden, Z }, Fan(Z));
				Put("gru.weight_hh", { 3 * S.Hidden, S.Hidden }, Fan(S.Hidden));
				Put("gru.bias_ih", { 3 * S.Hidden }, 0.1f);
				Put("gru.bias_hh", { 3 * S.Hidden }, 0.1f);
			}
			else
			{
				Put("ff.weight", { S.Hidden, Z }, Fan(Z));
				Put("ff.bias", { S.Hidden }, 0.1f);
			}
			Put("pi.weight", { S.NumActions, S.Hidden }, S.PiScale * Fan(S.Hidden));
			Put("pi.bias", { S.NumActions }, 0.3f);
			Put("v.weight", { 1, S.Hidden }, Fan(S.Hidden));
			Put("v.bias", { 1 }, 0.1f);
			Put("aux.weight", { RL::NumAnswerClasses, S.Hidden }, 2.f * Fan(S.Hidden));
			Put("aux.action", { RL::NumAnswerClasses, S.NumActions }, 0.8f);
			Put("aux.bias", { RL::NumAnswerClasses }, 0.3f);
			return W.Finish();
		}

		bool SameOutput(const FRLPolicyOutput& A, const FRLPolicyOutput& B)
		{
			return std::memcmp(A.Logits, B.Logits, sizeof(A.Logits)) == 0 && std::memcmp(A.Probs, B.Probs, sizeof(A.Probs)) == 0
				&& std::memcmp(&A.Value, &B.Value, sizeof(float)) == 0 && A.Argmax == B.Argmax && A.NumActions == B.NumActions;
		}

		// ==========================================================================================
		// The tests
		// ==========================================================================================

		bool TestActionSpace(std::string& Log)
		{
			int32_t Bad = 0;
			for (int32_t A = 0; A < RL::NumBossMoves; ++A)
			{
				const EMoveId M = RL::ActionMove(A);
				if (M == EMoveId::None || RL::MoveAction(M) != A || Move(M).Owner != ESide::Boss || std::strcmp(RL::ActionName(A), Move(M).Name) != 0)
				{
					++Bad;
					Logf(Log, "action %d (%s): round trip / owner / name WRONG", A, RL::ActionName(A));
				}
			}
			int32_t Foreign = 0;
			for (int32_t I = 0; I < RL::FirstBossMove; ++I) { if (RL::MoveAction(static_cast<EMoveId>(I)) != -1) { ++Foreign; } }
			if (RL::MoveAction(EMoveId::Count) != -1) { ++Foreign; }
			const bool bEnds = RL::ActionMove(0) == EMoveId::BFastSlash && RL::ActionMove(RL::NumBossMoves - 1) == EMoveId::BRetreat
				&& RL::ActionMove(RL::ActionWait) == EMoveId::None && RL::ActionMove(-1) == EMoveId::None
				&& RL::ActionMove(RL::NumActions) == EMoveId::None && std::strcmp(RL::ActionName(RL::ActionWait), "Wait") == 0;
			int32_t Unnamed = 0;
			for (int32_t I = 0; I < RL::ObsDim; ++I) { if (std::strcmp(RL::ObsFeatureName(I), "?") == 0) { ++Unnamed; } }
			const int32_t Vocab[RL::TokenFields] = { 10, 13, 6, 5, 10, 4 };
			bool bLayout = RL::NumActions == 23 && RL::ActionWait == 22 && RL::ObsDim == 107 && RL::HistoryTokens == 32
				&& RL::TokenFields == 6 && RL::NumAnswerClasses == 12;
			for (int32_t F = 0; F < RL::TokenFields; ++F) { bLayout = bLayout && RL::TokenVocab[F] == Vocab[F]; }
			Logf(Log, "%d actions = %d boss moves (%s .. %s) + %s: %d round-trip/owner/name errors, %d non-boss moves mapped",
				RL::NumActions, RL::NumBossMoves, RL::ActionName(0), RL::ActionName(RL::NumBossMoves - 1), RL::ActionName(RL::ActionWait), Bad, Foreign);
			Logf(Log, "observation %d floats (%d unnamed), tokens %d x %d, %d answer classes, out-of-range actions -> None: %s",
				RL::ObsDim, Unnamed, RL::HistoryTokens, RL::TokenFields, RL::NumAnswerClasses, bEnds ? "yes" : "NO");
			return Bad == 0 && Foreign == 0 && bEnds && Unnamed == 0 && bLayout;
		}

		bool TestChainStartupFloor(std::string& Log)
		{
			const int32_t Fast = ChainStartupFloor(ESym::BFast);
			const int32_t Heavy = ChainStartupFloor(ESym::BHeavy);
			const int32_t Feint = ChainStartupFloor(ESym::BFeint);
			const int32_t Killer = ChainStartupFloor(ESym::BKiller);
			const int32_t Guard = ChainStartupFloor(ESym::BGuard);
			// The floor must never forbid what a script itself does.
			int32_t Chains = 0;
			int32_t Below = 0;
			for (int32_t S = 0; S < NumBossScripts; ++S)
			{
				const FScriptSlot* Slots = nullptr;
				const int32_t N = BossScript(S, Slots);
				for (int32_t I = 0; I < N; ++I)
				{
					const FScriptSlot& Prev = Slots[(I + N - 1) % N];
					if (!Slots[I].bChain || !Move(Slots[I].Move).IsAttack() || !Move(Prev.Move).IsAttack()) { continue; }
					++Chains;
					if (Move(Slots[I].Move).Startup < ChainStartupFloor(Move(Prev.Move).Symbol)) { ++Below; }
				}
			}
			Logf(Log, "derived from %d scripted attack chains: after BFast %d, BHeavy %d, BFeint %d, BKiller %d, BGuard %d (%d scripted chains below their floor)",
				Chains, Fast, Heavy, Feint, Killer, Guard, Below);
			return Fast == 12 && Heavy == 12 && Feint == 14 && Killer >= 9999 && Guard >= 9999 && Chains > 0 && Below == 0;
		}

		bool TestMaskLegality(std::string& Log)
		{
			FLegality L;
			RunUniformFights(L, 7001);
			Logf(Log, "%d fights, %d decisions: %d attacks (%d chained, %d grabs), %d other commits, %d walk continues, %d waits",
				L.Fights, L.Decisions, L.Attacks, L.Chains, L.Grabs, L.NonAttacks, L.Continues, L.Waits);
			Logf(Log, "illegal (refused) actions %d, rule violations %d, mask != contract %d, longest string %d, most tokens %d",
				L.IllegalTotal, L.Violations, L.MaskMismatches, L.MaxString, L.MaxTokens);
			if (!L.FirstProblem.empty()) { Logf(Log, "first problem: %s", L.FirstProblem.c_str()); }
			return L.IllegalTotal == 0 && L.Violations == 0 && L.MaskMismatches == 0 && L.Decisions > 2000 && L.Attacks > 300
				&& L.Chains > 0 && L.Grabs > 0 && L.Killers > 0 && L.Continues > 0 && L.MaxString <= RL::MaxStringAttacks;
		}

		bool TestObservationBounds(std::string& Log)
		{
			FLegality L;
			RunUniformFights(L, 3301);
			Logf(Log, "%d observations over %d fights: %d out of range / malformed one-hots / bad tokens; history peaked at %d tokens",
				L.Decisions, L.Fights, L.BadObservations, L.MaxTokens);
			if (!L.FirstObsProblem.empty()) { Logf(Log, "first problem: %s", L.FirstObsProblem.c_str()); }
			return L.BadObservations == 0 && L.Decisions > 2000 && L.MaxTokens == RL::HistoryTokens;
		}

		bool TestPerceptionDelay(std::string& Log)
		{
			FHandDuel H;
			H.Begin(400.f);
			const int32_t Commit = 30;                  // the player swings at frame 30 (whiffs: nothing connects)
			const FMoveData& Light = Move(EMoveId::PLight1);
			const int32_t Whiff = Commit + Light.Startup + Light.Active - 1;
			const int32_t Moved = 60;                   // the player steps 100 cm closer DURING frame 60
			const int32_t OwnCommit = 70;               // the keeper's own guard: known at once
			float O[RL::ObsDim];
			bool bOk = true;
			const int32_t Acting = RL::ObsPlayerState + static_cast<int32_t>(EFighterState::Acting);
			for (int32_t F = 0; F <= 80; ++F)
			{
				H.Obs.BuildObservation(H.Duel, O, nullptr);
				if (F == Commit + 6 || F == Commit + 7)
				{
					const bool bSeen = F == Commit + 7;
					const bool bPass = (O[Acting] == (bSeen ? 1.f : 0.f)) && (O[RL::ObsPlayerMove + 0] == (bSeen ? 1.f : 0.f))
						&& (O[RL::ObsPlayerPhase + 0] == (bSeen ? 1.f : 0.f))
						&& (bSeen ? std::fabs(O[RL::ObsPlayerSinceCommit] - 7.f / 60.f) < 1.0e-6f : O[RL::ObsPlayerSinceCommit] == 2.f);
					Logf(Log, "swing at frame %d, keeper looks at frame %d: player %s, move PLight1 %.0f, since-commit %.3f -> %s",
						Commit, F, O[Acting] > 0.5f ? "Acting" : "Idle", O[RL::ObsPlayerMove + 0], O[RL::ObsPlayerSinceCommit], bPass ? "ok" : "WRONG");
					bOk = bOk && bPass;
				}
				if (F == Whiff + 6 || F == Whiff + 7)
				{
					const bool bSeen = F == Whiff + 7;
					const bool bPass = O[RL::ObsPlayerLastOutcome + static_cast<int32_t>(EHitOutcome::Whiff)] == (bSeen ? 1.f : 0.f)
						&& O[RL::ObsPlayerLastOutcome + static_cast<int32_t>(EHitOutcome::None)] == (bSeen ? 0.f : 1.f);
					Logf(Log, "its whiff at frame %d, keeper looks at frame %d: last player outcome %s -> %s", Whiff, F,
						bSeen ? "Whiff expected" : "None expected", bPass ? "ok" : "WRONG");
					bOk = bOk && bPass;
				}
				if (F == Moved + 6 || F == Moved + 7)
				{
					const float Want = (F == Moved + 7 ? 300.f : 400.f) / 500.f;
					const bool bPass = std::fabs(O[RL::ObsPlayerDistance] - Want) < 1.0e-6f;
					Logf(Log, "step closer during frame %d, keeper looks at frame %d: distance %.0f -> %s", Moved, F,
						O[RL::ObsPlayerDistance] * 500.f, bPass ? "ok" : "WRONG");
					bOk = bOk && bPass;
				}
				if (F == Commit) { bOk = bOk && H.Duel.CommitPlayerAttack(false); }
				if (F == OwnCommit)
				{
					bOk = bOk && H.Duel.Commit(ESide::Boss, EMoveId::BGuard);
					H.Obs.BuildObservation(H.Duel, O, nullptr);
					const bool bPass = O[RL::ObsSelfState + static_cast<int32_t>(EFighterState::Acting)] == 1.f
						&& O[RL::ObsSelfMove + RL::MoveAction(EMoveId::BGuard)] == 1.f;
					Logf(Log, "keeper raises its own guard at frame %d: seen in the same frame -> %s", F, bPass ? "ok" : "WRONG");
					bOk = bOk && bPass;
				}
				if (F == Moved + 1) { H.Geo.PlayerX = 100.f; } // the geometry at the start of frame Moved + 1
				H.Step();
			}
			return bOk;
		}

		bool TestTokens(std::string& Log)
		{
			bool bOk = true;
			char Txt[3][40];
			// ---- A: FastSlash, parried 4 frames before its impact; then Waits (one token), then a guard ---------------
			{
				FHandDuel H;
				H.Oracle.bBossHits = true;
				H.Begin(150.f);
				const int32_t Press = Move(EMoveId::BFastSlash).Startup - 4;
				int32_t Close = -1;
				int32_t Close2 = -1;
				int32_t WaitDecisions = 0;
				int32_t Early = 0;
				int32_t Label = -2;
				for (int32_t F = 0; F < 100; ++F)
				{
					const int32_t Expect = (Close >= 0 && F >= Close + RL::PerceptionFrames ? 1 : 0) + (Close2 >= 0 && F >= Close2 + RL::PerceptionFrames ? 1 : 0);
					if (H.Session.NumTokens != Expect) { ++Early; }
					if (H.Obs.IsDecisionPoint(H.Duel))
					{
						if (F == 0)
						{
							bOk = bOk && H.Obs.ApplyAction(H.Duel, RL::MoveAction(EMoveId::BFastSlash)) == EMoveId::BFastSlash;
						}
						else if (WaitDecisions < 5)
						{
							if (Close < 0) { Close = F; Label = H.Obs.AnswerToLastDecision(); }
							bOk = bOk && H.Obs.ApplyAction(H.Duel, RL::ActionWait) == EMoveId::None;
							++WaitDecisions;
						}
						else if (Close2 < 0)
						{
							Close2 = F;
							bOk = bOk && H.Obs.ApplyAction(H.Duel, RL::MoveAction(EMoveId::BGuard)) == EMoveId::BGuard;
						}
					}
					if (F == Press) { bOk = bOk && H.Duel.Commit(ESide::Player, EMoveId::PParry); }
					H.Step();
				}
				int8_t Tok[RL::HistoryTokens * RL::TokenFields];
				float O[RL::ObsDim];
				H.Obs.BuildObservation(H.Duel, O, Tok);
				const int8_t* Slash = &H.Session.Tokens[1][0];
				const int8_t* Wait = &H.Session.Tokens[0][0];
				const bool bSlash = SameRow(Slash, { 1, SymIndex(ESym::Parry) + 1, 1 + static_cast<int32_t>(EHitOutcome::Parried), 1, RL::LeadBucket(4), 1 });
				const bool bWait = SameRow(Wait, { 9, SymIndex(ESym::Neutral) + 1, 1, 1, 1, 1 });
				const bool bCopy = std::memcmp(Tok, &H.Session.Tokens[0][0], sizeof(Tok)) == 0;
				RowText(Slash, Txt[0], sizeof(Txt[0]));
				RowText(Wait, Txt[1], sizeof(Txt[1]));
				Logf(Log, "FastSlash at 0, parried (pressed at %d, lead 4): token closed at %d -> %s (want {1 7 5 1 %d 1}) %s",
					Press, Close, Txt[0], RL::LeadBucket(4), bSlash ? "ok" : "WRONG");
				Logf(Log, "%d Wait decisions from frame %d -> ONE token %s (want {9 1 1 1 1 1}) %s; closed by a guard at %d",
					WaitDecisions, Close, Txt[1], bWait ? "ok" : "WRONG", Close2);
				Logf(Log, "tokens in the session at every frame vs CloseFrame+6: %d frames wrong; %d tokens; observation copies them: %s; label at the close: %s",
					Early, H.Session.NumTokens, bCopy ? "yes" : "NO", Label >= 0 ? SymName(static_cast<ESym>(Label)) : "none");
				bOk = bOk && bSlash && bWait && bCopy && Early == 0 && H.Session.NumTokens == 2 && WaitDecisions == 5
					&& Label == SymIndex(ESym::Parry) && Close > Press;
			}
			// ---- B: a bait bitten, then an ANSWERED Wait (not extended), then an unanswered one (extended) -----------
			{
				FHandDuel H;
				H.Oracle.bBossHits = true;
				H.Begin(150.f);
				const int32_t Press = 8;  // at the FeintEarly's fake impact (12) minus 4: it bites
				int32_t Index = 0;
				int32_t Close1 = -1;
				for (int32_t F = 0; F < 120; ++F)
				{
					if (Index < 5 && H.Obs.IsDecisionPoint(H.Duel))
					{
						switch (Index++)
						{
						case 0:  bOk = bOk && H.Obs.ApplyAction(H.Duel, RL::MoveAction(EMoveId::BFeintEarly)) == EMoveId::BFeintEarly; break;
						case 1:  Close1 = F; H.Obs.ApplyAction(H.Duel, RL::ActionWait); break;
						case 2:
						case 3:  H.Obs.ApplyAction(H.Duel, RL::ActionWait); break;
						case 4:  H.Obs.ApplyAction(H.Duel, RL::MoveAction(EMoveId::BGuard)); break;
						default: break;
						}
					}
					if (F == Press) { bOk = bOk && H.Duel.Commit(ESide::Player, EMoveId::PParry); }
					if (Close1 >= 0 && F == Close1 + 2) { bOk = bOk && H.Duel.Commit(ESide::Player, EMoveId::PStepL); }
					H.Step();
				}
				const int8_t* Feint = &H.Session.Tokens[2][0];
				const int8_t* W1 = &H.Session.Tokens[1][0];
				const int8_t* W2 = &H.Session.Tokens[0][0];
				const bool bFeint = SameRow(Feint, { 3, SymIndex(ESym::Parry) + 1, 1 + static_cast<int32_t>(EHitOutcome::Hit), 2, RL::LeadBucket(12 - Press), 2 });
				const bool bW1 = SameRow(W1, { 9, SymIndex(ESym::StepL) + 1, 1, 1, 1, 1 });
				const bool bW2 = SameRow(W2, { 9, SymIndex(ESym::Neutral) + 1, 1, 1, 1, 1 });
				RowText(Feint, Txt[0], sizeof(Txt[0]));
				RowText(W1, Txt[1], sizeof(Txt[1]));
				RowText(W2, Txt[2], sizeof(Txt[2]));
				Logf(Log, "FeintEarly, parry pressed at the fake (frame %d), real strike hits: %s (want {3 7 2 2 %d 2}) %s",
					Press, Txt[0], RL::LeadBucket(12 - Press), bFeint ? "ok" : "WRONG");
				Logf(Log, "Wait answered by StepL %s %s; next Waits (unanswered, one token) %s %s; %d tokens (want 3)",
					Txt[1], bW1 ? "ok" : "WRONG", Txt[2], bW2 ? "ok" : "WRONG", H.Session.NumTokens);
				bOk = bOk && bFeint && bW1 && bW2 && H.Session.NumTokens == 3 && Index == 5;
			}
			return bOk;
		}

		bool TestAnswerLabel(std::string& Log)
		{
			bool bOk = true;
			// 1. The FIRST commitment wins (a parry, then a guard raise).
			{
				FHandDuel H;
				H.Begin(400.f);
				const int32_t Before = H.Obs.AnswerToLastDecision();
				H.Obs.ApplyAction(H.Duel, RL::ActionWait);
				for (int32_t F = 0; F < 6; ++F)
				{
					if (F == 1) { bOk = bOk && H.Duel.Commit(ESide::Player, EMoveId::PParry); }
					if (F == 2) { H.Duel.SetGuardHeld(ESide::Player, true); }
					H.Step();
				}
				const int32_t Label = H.Obs.AnswerToLastDecision();
				Logf(Log, "before any decision: %d (want -1); parry then guard raise -> %s (want Parry)", Before, Label >= 0 ? SymName(static_cast<ESym>(Label)) : "none");
				bOk = bOk && Before == -1 && Label == SymIndex(ESym::Parry);
			}
			// 2. Guard held through the whole window, no commitment in it -> Block.
			{
				FHandDuel H;
				H.Begin(400.f);
				H.Obs.ApplyAction(H.Duel, RL::ActionWait);
				for (int32_t F = 0; F < 6; ++F)
				{
					if (F == 1) { H.Duel.SetGuardHeld(ESide::Player, true); } // the raise is a commitment of the FIRST window
					H.Step();
				}
				const int32_t First = H.Obs.AnswerToLastDecision();
				H.Obs.ApplyAction(H.Duel, RL::ActionWait); // answered token: a new one opens
				for (int32_t F = 0; F < 6; ++F) { H.Step(); }
				const int32_t Held = H.Obs.AnswerToLastDecision();
				Logf(Log, "guard raised in window 1 -> %s; held through window 2 with no commitment -> %s (want Block, Block)",
					First >= 0 ? SymName(static_cast<ESym>(First)) : "none", Held >= 0 ? SymName(static_cast<ESym>(Held)) : "none");
				bOk = bOk && First == SymIndex(ESym::Block) && Held == SymIndex(ESym::Block);
			}
			// 3. Nothing but walking -> the movement symbol (the latest one seen).
			{
				FHandDuel H;
				H.Begin(400.f);
				H.Obs.ApplyAction(H.Duel, RL::ActionWait);
				for (int32_t F = 0; F < 6; ++F) { H.Step(ESym::Advance); }
				const int32_t Walk1 = H.Obs.AnswerToLastDecision();
				H.Obs.ApplyAction(H.Duel, RL::ActionWait); // unanswered: extends
				for (int32_t F = 0; F < 6; ++F) { H.Step(ESym::Retreat); }
				const int32_t Walk2 = H.Obs.AnswerToLastDecision();
				Logf(Log, "advancing -> %s, then retreating -> %s (want Advance, Retreat)",
					Walk1 >= 0 ? SymName(static_cast<ESym>(Walk1)) : "none", Walk2 >= 0 ? SymName(static_cast<ESym>(Walk2)) : "none");
				bOk = bOk && Walk1 == SymIndex(ESym::Advance) && Walk2 == SymIndex(ESym::Retreat);
			}
			// 4. Ground truth at call time (no perception delay for labels): a light swing answering a FastSlash.
			{
				FHandDuel H;
				H.Begin(150.f);
				bOk = bOk && H.Obs.ApplyAction(H.Duel, RL::MoveAction(EMoveId::BFastSlash)) == EMoveId::BFastSlash;
				for (int32_t F = 0; F < 4; ++F)
				{
					if (F == 3) { bOk = bOk && H.Duel.CommitPlayerAttack(false); }
					H.Step();
				}
				const int32_t Light = H.Obs.AnswerToLastDecision();
				Logf(Log, "a light swing 1 frame ago, answering a FastSlash -> %s at once (want Light)", Light >= 0 ? SymName(static_cast<ESym>(Light)) : "none");
				bOk = bOk && Light == SymIndex(ESym::Light);
			}
			return bOk;
		}

		bool TestReadMeter(std::string& Log)
		{
			struct FCase { const char* What; ESym Predicted; float P; bool bGuard; bool bExpect; };
			const FCase Cases[] = {
				{ "confident, right, hit", ESym::Neutral, 0.9f, false, true },
				{ "confident, wrong",      ESym::Parry,   0.9f, false, false },
				{ "right but unsure",      ESym::Neutral, 0.5f, false, false },
				{ "right, but blocked",    ESym::Block,   0.9f, true,  false },
			};
			bool bOk = true;
			for (const FCase& C : Cases)
			{
				FHandDuel H;
				H.Oracle.bBossHits = true;
				H.Begin(150.f);
				FRLRead Read;
				Read.Predicted = C.Predicted;
				Read.P = C.P;
				H.Obs.ApplyAction(H.Duel, RL::MoveAction(EMoveId::BFastSlash), &Read);
				for (int32_t F = 0; F < 20; ++F)
				{
					if (F == 0 && C.bGuard) { H.Duel.SetGuardHeld(ESide::Player, true); }
					H.Step();
				}
				FReadMeterEvent E;
				const bool bPopped = H.Obs.PopReadMeter(E);
				FReadMeterEvent E2;
				const bool bTwice = H.Obs.PopReadMeter(E2);
				const bool bPass = bPopped == C.bExpect && H.Obs.ReadCountersLanded() == (C.bExpect ? 1 : 0) && !bTwice
					&& (!C.bExpect || (E.Frame == Move(EMoveId::BFastSlash).Startup && E.Predicted == C.Predicted && E.Confidence == C.P && E.Counter == EMoveId::BFastSlash));
				Logf(Log, "%-22s (%s p=%.2f): READ %s -> %s", C.What, SymName(C.Predicted), C.P, bPopped ? "shown" : "not shown", bPass ? "ok" : "WRONG");
				bOk = bOk && bPass;
			}
			return bOk;
		}

		bool TestMirrorY(std::string& Log)
		{
			// The same fight in the simulator's handedness and mirrored (Unreal's): the keeper must see the same thing.
			std::vector<float> Seen[2];
			float MaxLateral = 0.f;
			float MaxSin = 0.f;
			for (int32_t Pass = 0; Pass < 2; ++Pass)
			{
				const bool bMirror = Pass == 1;
				const float Sy = bMirror ? -1.f : 1.f;
				FHandDuel H;
				FDuelGeometry G;
				G.PlayerX = 0.f;
				G.PlayerY = 0.f;
				G.BossX = 200.f;
				G.BossY = 30.f * Sy;
				G.bMirrorY = bMirror;
				H.BeginAt(G);
				float O[RL::ObsDim];
				for (int32_t F = 0; F < 40; ++F)
				{
					H.Geo.PlayerX = 2.f * static_cast<float>(F);
					H.Geo.PlayerY = 3.f * static_cast<float>(F) * Sy; // the player drifts to its left (simulator handedness)
					if (F == 0)
					{
						H.Obs.ApplyAction(H.Duel, RL::MoveAction(EMoveId::BFastSlash));
						FFighter& B = H.Duel.Get(ESide::Boss);
						B.CommitFacingX = -0.9f;
						B.CommitFacingY = 0.3f * Sy;
					}
					H.Obs.BuildObservation(H.Duel, O, nullptr);
					Seen[Pass].insert(Seen[Pass].end(), O, O + RL::ObsDim);
					if (!bMirror)
					{
						if (std::fabs(O[RL::ObsPlayerLateralVel]) > MaxLateral) { MaxLateral = std::fabs(O[RL::ObsPlayerLateralVel]); }
						if (std::fabs(O[RL::ObsPlayerBearingSin]) > MaxSin) { MaxSin = std::fabs(O[RL::ObsPlayerBearingSin]); }
					}
					H.Step();
				}
			}
			const bool bSame = Seen[0].size() == Seen[1].size() && std::memcmp(Seen[0].data(), Seen[1].data(), Seen[0].size() * sizeof(float)) == 0;
			Logf(Log, "40 observations, simulator vs mirrored geometry (bMirrorY, facings mirrored too): %s; lateral velocity up to %.2f, bearing sin up to %.2f",
				bSame ? "identical" : "DIFFERENT", MaxLateral, MaxSin);
			return bSame && MaxLateral > 0.05f && MaxSin > 0.05f;
		}

		bool TestPolicyFixture(std::string& Log)
		{
			bool bOk = true;
			for (int32_t Pass = 0; Pass < 2; ++Pass)
			{
				FFixtureSpec Spec;
				Spec.Seed = 1234 + Pass;
				Spec.bRecurrent = Pass == 0;
				const char* Kind = Spec.bRecurrent ? "recurrent" : "feed-forward";
				const std::vector<uint8_t> Image = MakeFixture(Spec);
				FRLPolicy Pol;
				if (!Pol.LoadFromMemory(Image.data(), Image.size()))
				{
					Logf(Log, "%s fixture: load FAILED: %s", Kind, Pol.GetError().c_str());
					bOk = false;
					continue;
				}
				const int32_t H = Pol.HiddenSize();
				bool bDims = Pol.ObsDim() == RL::ObsDim && Pol.NumActions() == RL::NumActions && Pol.AuxClasses() == RL::NumAnswerClasses
					&& H == Spec.Hidden && Pol.IsRecurrent() == Spec.bRecurrent && Pol.Side() == 0;

				FRandom Rng(99 + Pass);
				float Obs[RL::ObsDim];
				for (float& V : Obs) { V = Rng.FRandRange(-1.f, 1.f); }
				int8_t Tok[RL::HistoryTokens * RL::TokenFields] = {};
				for (int32_t K = 0; K < 5; ++K)
				{
					for (int32_t F = 0; F < RL::TokenFields; ++F) { Tok[K * RL::TokenFields + F] = static_cast<int8_t>(1 + Rng.RandHelper(RL::TokenVocab[F] - 1)); }
				}
				uint8_t All[RL::NumActions];
				std::memset(All, 1, sizeof(All));
				float H0[FRLSession::MaxHidden] = {};
				for (int32_t I = 0; I < H; ++I) { H0[I] = Rng.FRandRange(-0.5f, 0.5f); }
				float Ha[FRLSession::MaxHidden] = {};
				float Hb[FRLSession::MaxHidden] = {};
				FRLPolicyOutput Oa;
				FRLPolicyOutput Ob;
				Pol.Forward(Obs, Tok, All, H0, Ha, Oa);
				Pol.Forward(Obs, Tok, All, H0, Hb, Ob);
				const bool bRepeat = SameOutput(Oa, Ob) && std::memcmp(Ha, Hb, sizeof(float) * static_cast<size_t>(H)) == 0;

				// Masks: the unmasked favourite is masked out (with two others): it gets probability 0 and is not chosen.
				uint8_t Mask[RL::NumActions];
				std::memset(Mask, 1, sizeof(Mask));
				Mask[Oa.Argmax] = 0;
				Mask[3] = 0;
				Mask[17] = 0;
				FRLPolicyOutput Om;
				float Hm[FRLSession::MaxHidden] = {};
				Pol.Forward(Obs, Tok, Mask, H0, Hm, Om);
				float Sum = 0.f;
				bool bMasked = Om.Argmax >= 0 && Mask[Om.Argmax] != 0;
				for (int32_t A = 0; A < RL::NumActions; ++A)
				{
					Sum += Om.Probs[A];
					if (Mask[A] == 0) { bMasked = bMasked && Om.Probs[A] == 0.f; }
					bMasked = bMasked && Om.Probs[A] >= 0.f && (Mask[A] == 0 || Om.Probs[A] <= Om.Probs[Om.Argmax]);
				}
				bMasked = bMasked && std::fabs(Sum - 1.f) < 1.0e-5f;
				uint8_t OnlyWait[RL::NumActions] = {};
				OnlyWait[RL::ActionWait] = 1;
				FRLPolicyOutput Ow;
				Pol.Forward(Obs, Tok, OnlyWait, H0, Hm, Ow);
				bMasked = bMasked && Ow.Argmax == RL::ActionWait && Ow.Probs[RL::ActionWait] == 1.f;

				// Memory: the recurrent net's state moves and matters; the feed-forward net ignores it.
				bool bMemory = false;
				float Hc[FRLSession::MaxHidden] = {};
				FRLPolicyOutput Oc;
				if (Spec.bRecurrent)
				{
					float Moved = 0.f;
					for (int32_t I = 0; I < H; ++I) { Moved += std::fabs(Ha[I] - H0[I]); }
					Pol.Forward(Obs, Tok, All, Ha, Hc, Oc);
					bMemory = Moved > 1.0e-3f && !SameOutput(Oa, Oc);
				}
				else
				{
					float Zero[FRLSession::MaxHidden] = {};
					Pol.Forward(Obs, Tok, All, Zero, Hc, Oc);
					bMemory = SameOutput(Oa, Oc);
				}
				// HiddenIn may alias HiddenOut; null HiddenIn = zeros.
				float Alias[FRLSession::MaxHidden] = {};
				std::memcpy(Alias, H0, sizeof(Alias));
				FRLPolicyOutput Oal;
				Pol.Forward(Obs, Tok, All, Alias, Alias, Oal);
				bool bAlias = SameOutput(Oa, Oal) && std::memcmp(Alias, Ha, sizeof(float) * static_cast<size_t>(H)) == 0;
				float Zeros[FRLSession::MaxHidden] = {};
				FRLPolicyOutput On;
				FRLPolicyOutput Oz;
				Pol.Forward(Obs, Tok, All, nullptr, Hc, On);
				Pol.Forward(Obs, Tok, All, Zeros, Hc, Oz);
				bAlias = bAlias && SameOutput(On, Oz);

				// The read head: a distribution per action, conditioned on the action.
				bool bAux = true;
				float P0[FRLPolicyOutput::MaxAux] = {};
				float P7[FRLPolicyOutput::MaxAux] = {};
				float P22[FRLPolicyOutput::MaxAux] = {};
				Pol.Aux(Ha, 0, P0);
				Pol.Aux(Ha, 7, P7);
				Pol.Aux(Ha, RL::ActionWait, P22);
				float Diff = 0.f;
				for (const float* Pr : { P0, P7, P22 })
				{
					float S = 0.f;
					for (int32_t C = 0; C < RL::NumAnswerClasses; ++C) { S += Pr[C]; bAux = bAux && Pr[C] >= 0.f && Pr[C] <= 1.f; }
					bAux = bAux && std::fabs(S - 1.f) < 1.0e-5f;
				}
				for (int32_t C = 0; C < RL::NumAnswerClasses; ++C) { Diff += std::fabs(P0[C] - P7[C]); }
				bAux = bAux && Diff > 1.0e-3f;

				Logf(Log, "%s fixture (%zu bytes, enc %d, embed %d, hidden %d): dims %s, repeatable %s, masks %s (argmax %s -> %s), memory %s, alias/null %s, aux %s",
					Kind, Image.size(), Spec.Enc, Spec.Embed, H, bDims ? "ok" : "WRONG", bRepeat ? "ok" : "WRONG", bMasked ? "ok" : "WRONG",
					RL::ActionName(Oa.Argmax), RL::ActionName(Om.Argmax), bMemory ? "ok" : "WRONG", bAlias ? "ok" : "WRONG", bAux ? "ok" : "WRONG");
				bDims = bDims && Om.Argmax != Oa.Argmax;
				bOk = bOk && bDims && bRepeat && bMasked && bMemory && bAlias && bAux;
			}

			// Broken images are refused (and leave the policy unloaded).
			FFixtureSpec Good;
			const std::vector<uint8_t> Valid = MakeFixture(Good);
			FRLPolicy Pol;
			const bool bValid = Pol.LoadFromMemory(Valid.data(), Valid.size());
			struct FBroken { const char* What; std::vector<uint8_t> Bytes; const char* ErrorHas; };
			std::vector<FBroken> Broken;
			Broken.push_back({ "truncated", std::vector<uint8_t>(Valid.begin(), Valid.begin() + static_cast<std::ptrdiff_t>(Valid.size() / 2)), "" });
			std::vector<uint8_t> Magic = Valid;
			Magic[0] = 'X';
			Broken.push_back({ "bad magic", Magic, "not a .hwrl" });
			FFixtureSpec WrongLayout;
			WrongLayout.LayoutVersion = RL::ObsLayoutVersion + 1;
			Broken.push_back({ "wrong obs_layout_version", MakeFixture(WrongLayout), "layout" });
			FFixtureSpec Missing;
			Missing.Skip = "pi.bias";
			Broken.push_back({ "missing pi.bias", MakeFixture(Missing), "pi.bias" });
			FFixtureSpec WrongActions;
			WrongActions.NumActions = RL::NumActions - 1;
			Broken.push_back({ "22 actions", MakeFixture(WrongActions), "must match" });
			int32_t Refused = 0;
			for (const FBroken& Bk : Broken)
			{
				const bool bLoaded = Pol.LoadFromMemory(Bk.Bytes.data(), Bk.Bytes.size());
				const bool bRight = !bLoaded && !Pol.IsLoaded() && !Pol.GetError().empty() && Pol.GetError().find(Bk.ErrorHas) != std::string::npos;
				Refused += bRight ? 1 : 0;
				Logf(Log, "  %-26s -> %s (%s)", Bk.What, bRight ? "refused" : "WRONG", Pol.GetError().c_str());
			}
			const bool bNoFile = !Pol.LoadFromFile("__hwrl_no_such_dir__/missing.hwrl") && !Pol.IsLoaded();
			Logf(Log, "valid image loads: %s; %d/%d broken images refused; missing file refused: %s", bValid ? "yes" : "NO",
				Refused, static_cast<int32_t>(Broken.size()), bNoFile ? "yes" : "NO");
			return bOk && bValid && Refused == static_cast<int32_t>(Broken.size()) && bNoFile;
		}

		/** The fixture the brain plays with: small, recurrent, and decisive enough to vary its actions. */
		bool LoadBrainFixture(FRLPolicy& Pol, int32_t Seed = 4715)
		{
			FFixtureSpec Spec;
			Spec.Seed = Seed;
			Spec.Enc = 32;
			Spec.Embed = 8;
			Spec.Hidden = 16;
			Spec.PiScale = 12.f; // the hidden state, not the biases, decides: the actions vary with what it sees
			const std::vector<uint8_t> Image = MakeFixture(Spec);
			return Pol.LoadFromMemory(Image.data(), Image.size());
		}

		FEncounterStats RunBrainFight(FRLBrain& Brain, const FRLPolicy& Pol, FRLSession& Memory, int32_t Seed, std::vector<FExchangeRow>& Rows)
		{
			Brain.Bind(&Pol, &Memory);
			FRunConfig Cfg;
			Cfg.Bot = MakeBotProfile(EBotKind::Varied, 0.6f);
			Cfg.Seed = Seed;
			Cfg.bImmortal = true;
			Cfg.MaxFrames = 60 * FramesPerSecond;
			Rows.clear();
			return RunEncounter(Brain, Cfg, &Rows);
		}

		bool SameDecisions(const std::vector<FExchangeRow>& A, const std::vector<FExchangeRow>& B)
		{
			if (A.size() != B.size()) { return false; }
			for (size_t I = 0; I < A.size(); ++I)
			{
				const FBrainDecision& X = A[I].Decision;
				const FBrainDecision& Y = B[I].Decision;
				if (X.Frame != Y.Frame || X.Chosen != Y.Chosen || X.Predicted != Y.Predicted || X.NumTop != Y.NumTop
					|| std::memcmp(&X.Value, &Y.Value, sizeof(float)) != 0 || std::memcmp(&X.PredictedP, &Y.PredictedP, sizeof(float)) != 0
					|| std::strcmp(X.Reason, Y.Reason) != 0 || A[I].Outcome != B[I].Outcome)
				{
					return false;
				}
			}
			return true;
		}

		bool TestBrainDeterminism(std::string& Log)
		{
			FRLPolicy Pol;
			if (!LoadBrainFixture(Pol)) { Logf(Log, "fixture failed to load: %s", Pol.GetError().c_str()); return false; }

			// Two runs, a fresh session each: bit-identical.
			FRLSession SA;
			FRLSession SB;
			FRLBrain BA;
			FRLBrain BB;
			std::vector<FExchangeRow> RowsA;
			std::vector<FExchangeRow> RowsB;
			const FEncounterStats A = RunBrainFight(BA, Pol, SA, 321, RowsA);
			const FEncounterStats B = RunBrainFight(BB, Pol, SB, 321, RowsB);
			bool bStats = A.Frames == B.Frames && A.PlayerDamageTaken == B.PlayerDamageTaken && A.BossDamageTaken == B.BossDamageTaken
				&& A.BossSwings == B.BossSwings && A.PlayerSwings == B.PlayerSwings && A.Decisions == B.Decisions && A.CountersLanded == B.CountersLanded;
			for (int32_t I = 0; I < 5; ++I) { bStats = bStats && A.BossOutcomes[I] == B.BossOutcomes[I] && A.PlayerOutcomes[I] == B.PlayerOutcomes[I]; }
			const bool bRows = SameDecisions(RowsA, RowsB);
			const bool bMemory = std::memcmp(SA.Hidden, SB.Hidden, sizeof(SA.Hidden)) == 0 && std::memcmp(SA.Tokens, SB.Tokens, sizeof(SA.Tokens)) == 0
				&& SA.NumTokens == SB.NumTokens && SA.HiddenSize == Pol.HiddenSize();
			int32_t Distinct = 0;
			bool bUsed[RL::NumActions + 1] = {};
			bool bRecords = A.Decisions == BA.Decisions() && A.DecisionKinds[static_cast<int32_t>(EDecisionKind::Policy)] == A.Decisions;
			for (const FExchangeRow& R : RowsA)
			{
				const int32_t Act = R.Decision.Chosen == EMoveId::None ? RL::ActionWait : RL::MoveAction(R.Decision.Chosen);
				if (Act >= 0 && !bUsed[Act]) { bUsed[Act] = true; ++Distinct; }
				bRecords = bRecords && R.Decision.Kind == EDecisionKind::Policy && R.Decision.ScriptIndex == -1 && R.Decision.NumTop >= 1
					&& R.Decision.bArgmaxHolds && R.Decision.Reason[0] != '\0';
			}
			Logf(Log, "fresh session x2 (seed 321, 60 s vs Varied): %d decisions, %d swings, %d distinct actions, player dmg %.1f, boss dmg %.1f",
				A.Decisions, A.BossSwings, Distinct, A.PlayerDamageTaken, A.BossDamageTaken);
			Logf(Log, "  stats %s, decision sequences %s, session memory %s, records well-formed %s, illegal %d; first: \"%s\"",
				bStats ? "identical" : "DIFFERENT", bRows ? "identical" : "DIFFERENT", bMemory ? "identical" : "DIFFERENT",
				bRecords ? "yes" : "NO", BA.Observer().IllegalActions(), RowsA.empty() ? "" : RowsA[0].Decision.Reason);

			// A carried session: fight 2 knows it is fight 2, has the history of fight 1, and its memory changes its play.
			const int32_t TokensAfter1 = SA.NumTokens;
			std::vector<FExchangeRow> RowsC;
			std::vector<FExchangeRow> RowsD;
			RunBrainFight(BA, Pol, SA, 322, RowsC);
			FRLSession Fresh;
			FRLBrain BD;
			RunBrainFight(BD, Pol, Fresh, 322, RowsD);
			const bool bCarry = SA.FightIndex == 1 && SA.FightsBegun == 2 && SA.NumTokens >= TokensAfter1 && SA.NumTokens > 0
				&& Fresh.FightIndex == 0 && !RowsC.empty() && !RowsD.empty()
				&& std::memcmp(&RowsC[0].Decision.Value, &RowsD[0].Decision.Value, sizeof(float)) != 0;
			Logf(Log, "carried session: fight index %d, fights begun %d, tokens %d -> %d; first value %+.4f vs a fresh session's %+.4f -> %s",
				SA.FightIndex, SA.FightsBegun, TokensAfter1, SA.NumTokens, RowsC.empty() ? 0.f : RowsC[0].Decision.Value,
				RowsD.empty() ? 0.f : RowsD[0].Decision.Value, bCarry ? "ok" : "WRONG");

			// Reset forgets everything.
			SA.Reset();
			bool bReset = SA.HiddenSize == 0 && SA.NumTokens == 0 && SA.FightsBegun == 0 && SA.FightIndex == 0 && SA.BossSwingsSeen == 0;
			for (float V : SA.Hidden) { bReset = bReset && V == 0.f; }
			for (int32_t K = 0; K < RL::HistoryTokens; ++K) { for (int32_t F = 0; F < RL::TokenFields; ++F) { bReset = bReset && SA.Tokens[K][F] == 0; } }
			Logf(Log, "FRLSession::Reset clears hidden state, tokens and counters: %s", bReset ? "yes" : "NO");
			return bStats && bRows && bMemory && bRecords && bCarry && bReset && A.Decisions > 50 && A.BossSwings > 0 && Distinct >= 3
				&& BA.Observer().IllegalActions() == 0;
		}

		bool TestSessionCarry(std::string& Log)
		{
			bool bOk = true;
			// 1. The history holds 32 tokens, newest first; the oldest falls off.
			{
				FRLSession S;
				int32_t MaxSeen = 0;
				for (int32_t I = 0; I < 40; ++I)
				{
					const int8_t T[RL::TokenFields] = { static_cast<int8_t>(1 + I % 9), static_cast<int8_t>(1 + I % 12), 1, 1, 1, 1 };
					S.PushToken(T);
					if (S.NumTokens > MaxSeen) { MaxSeen = S.NumTokens; }
				}
				bool bOrder = S.NumTokens == RL::HistoryTokens;
				for (int32_t K = 0; K < RL::HistoryTokens; ++K)
				{
					const int32_t I = 39 - K;
					bOrder = bOrder && S.Tokens[K][0] == 1 + I % 9 && S.Tokens[K][1] == 1 + I % 12;
				}
				Logf(Log, "40 pushes: %d kept (never more than %d), newest first, oldest dropped: %s", S.NumTokens, MaxSeen, bOrder ? "yes" : "NO");
				bOk = bOk && bOrder && MaxSeen == RL::HistoryTokens;
			}
			// 2. Exchanges persist across fights on one session; a Reset session (a new player) starts empty.
			{
				FHandDuel H;
				H.Begin(400.f);
				const EMoveId Plan1[] = { EMoveId::BBackstep, EMoveId::BGuard, EMoveId::None };
				int32_t N = 0;
				for (int32_t F = 0; F < 90; ++F)
				{
					if (H.Obs.IsDecisionPoint(H.Duel) && N < 3)
					{
						const EMoveId M = Plan1[N++];
						H.Obs.ApplyAction(H.Duel, M == EMoveId::None ? RL::ActionWait : RL::MoveAction(M));
					}
					H.Step();
				}
				const int32_t InFight1 = H.Session.NumTokens;  // backstep and guard finalised; the Wait still open
				H.Duel.Reset();
				H.Obs.BeginEncounter(&H.Session, H.Duel, H.Geo);
				const bool bFight2 = H.Session.NumTokens == 3 && H.Session.FightIndex == 1 && H.Session.FightsBegun == 2
					&& H.Session.Tokens[0][0] == 9 && H.Session.Tokens[1][0] == 5 && H.Session.Tokens[2][0] == 6;
				char Rows[3][40];
				for (int32_t K = 0; K < 3; ++K) { RowText(&H.Session.Tokens[K][0], Rows[K], sizeof(Rows[K])); }
				Logf(Log, "fight 1 (backstep, guard, wait): %d tokens in the fight; fight 2 begins with %d (the open Wait joins): %s %s %s -> %s",
					InFight1, H.Session.NumTokens, Rows[0], Rows[1], Rows[2], bFight2 ? "ok" : "WRONG");
				int32_t N2 = 0;
				for (int32_t F = 0; F < 60; ++F)
				{
					if (H.Obs.IsDecisionPoint(H.Duel) && N2 < 2)
					{
						H.Obs.ApplyAction(H.Duel, N2 == 0 ? RL::MoveAction(EMoveId::BGuard) : RL::ActionWait);
						++N2;
					}
					H.Step();
				}
				const bool bNewest = H.Session.NumTokens == 4 && H.Session.Tokens[0][0] == 5 && H.Session.Tokens[1][0] == 9 && H.Session.Tokens[3][0] == 6;
				Logf(Log, "fight 2's guard joins on top of fight 1's exchanges (%d tokens, newest first): %s", H.Session.NumTokens, bNewest ? "ok" : "WRONG");
				H.Session.Reset();
				H.Duel.Reset();
				H.Obs.BeginEncounter(&H.Session, H.Duel, H.Geo);
				const bool bClean = H.Session.NumTokens == 0 && H.Session.FightIndex == 0 && H.Session.FightsBegun == 1;
				Logf(Log, "a Reset session (new player) does not inherit the open Wait: %d tokens, fight index %d -> %s",
					H.Session.NumTokens, H.Session.FightIndex, bClean ? "ok" : "WRONG");
				bOk = bOk && InFight1 == 2 && bFight2 && bNewest && bClean;
			}
			// 3. A long session through the real frame loop: the history fills and stays capped.
			{
				FLegality L;
				FRLSession Memory;
				FUniformBrain Brain;
				Brain.Session = &Memory;
				Brain.Stats = &L;
				int32_t Before = -1;
				bool bGrows = true;
				for (int32_t F = 0; F < 3; ++F)
				{
					FRunConfig Cfg;
					Cfg.Bot = MakeBotProfile(EBotKind::Habitual, 0.8f);
					Cfg.Seed = 555 + F;
					Cfg.bImmortal = true;
					Cfg.MaxFrames = 30 * FramesPerSecond;
					RunEncounter(Brain, Cfg);
					bGrows = bGrows && Memory.NumTokens >= Before;
					Before = Memory.NumTokens;
				}
				Logf(Log, "3 fights of 30 s on one session: %d tokens (cap %d, peak %d), fight index %d, keeper swings seen %d",
					Memory.NumTokens, RL::HistoryTokens, L.MaxTokens, Memory.FightIndex, Memory.BossSwingsSeen);
				bOk = bOk && bGrows && Memory.NumTokens == RL::HistoryTokens && L.MaxTokens <= RL::HistoryTokens && Memory.FightIndex == 2
					&& Memory.BossSwingsSeen > 0 && L.BadObservations == 0;
			}
			return bOk;
		}

		// ------------------------------------------------------------------------------------------
		// Layout 3: difficulty, identity, learning players, the notebook
		// ------------------------------------------------------------------------------------------

		/** First boss attack with a cancel window (a chained attack can follow it). */
		int32_t ChainableAttack()
		{
			for (int32_t A = 0; A < RL::NumBossMoves; ++A)
			{
				const FMoveData& M = Move(RL::ActionMove(A));
				if (M.IsAttack() && M.CancelFrame > 0 && M.CancelFrame < M.TotalFrames() && M.Range >= 120.f) { return A; }
			}
			return -1;
		}

		/** Attacks the mask allows once the keeper is in the cancel window of a first attack, at a given skill. */
		int32_t AttacksAllowedInChain(float Skill, int32_t First)
		{
			FHandDuel H;
			H.Obs.Config.Skill = Skill;
			H.Begin(120.f);
			if (!H.Obs.IsDecisionPoint(H.Duel)) { return -1; }
			H.Obs.ApplyAction(H.Duel, First);
			for (int32_t F = 0; F < 120; ++F)
			{
				H.Step();
				const FFighter& B = H.Duel.Get(ESide::Boss);
				if (B.State == EFighterState::Acting && B.CurrentMove().IsAttack() && B.IsActionable())
				{
					uint8_t Mask[RL::NumActions];
					H.Obs.BuildMask(H.Duel, Mask);
					int32_t N = 0;
					for (int32_t A = 0; A < RL::NumBossMoves; ++A) { N += (Mask[A] != 0 && Move(RL::ActionMove(A)).IsAttack()) ? 1 : 0; }
					return N;
				}
			}
			return -1;
		}

		bool TestSkill(std::string& Log)
		{
			// 1. Skill 1 reproduces the fixed constants; skill 0 is the slowest keeper; every knob moves one way.
			const RL::FSkillParams Full = RL::SkillParams(1.f);
			const RL::FSkillParams Zero = RL::SkillParams(0.f);
			const bool bFull = Full.Perception == RL::PerceptionFrames && Full.DecisionGap == RL::DecisionGapFrames
				&& Full.MaxString == RL::MaxStringAttacks && Full.GrabCooldown == RL::GrabCooldownFrames
				&& Full.KillerCooldown == RL::KillerCooldownFrames && RL::TargetSwingsPerMin(66.f, 1.f) == 66.f;
			const bool bZero = Zero.Perception == RL::MaxPerceptionFrames && Zero.DecisionGap == 12 && Zero.MaxString == 1
				&& Zero.GrabCooldown == 2 * RL::GrabCooldownFrames && Zero.KillerCooldown == 2 * RL::KillerCooldownFrames
				&& RL::TargetSwingsPerMin(66.f, 0.f) == 50.f;
			bool bMono = true;
			RL::FSkillParams Prev = Zero;
			for (int32_t I = 1; I <= 20; ++I)
			{
				const RL::FSkillParams P = RL::SkillParams(static_cast<float>(I) / 20.f);
				bMono = bMono && P.Perception <= Prev.Perception && P.DecisionGap <= Prev.DecisionGap && P.MaxString >= Prev.MaxString
					&& P.GrabCooldown <= Prev.GrabCooldown && P.KillerCooldown <= Prev.KillerCooldown;
				Prev = P;
			}
			const bool bClamp = RL::SkillParams(-3.f).Perception == Zero.Perception && RL::SkillParams(7.f).Perception == Full.Perception;
			Logf(Log, "skill 1: perception %d, gap %d, strings %d, grab %d, killer %d (the fixed constants: %s); skill 0: %d, %d, %d, %d, %d (%s); monotone %s, clamped %s",
				Full.Perception, Full.DecisionGap, Full.MaxString, Full.GrabCooldown, Full.KillerCooldown, bFull ? "yes" : "NO",
				Zero.Perception, Zero.DecisionGap, Zero.MaxString, Zero.GrabCooldown, Zero.KillerCooldown, bZero ? "ok" : "WRONG",
				bMono ? "yes" : "NO", bClamp ? "yes" : "NO");

			// 2. At skill 0 a commitment at frame e is invisible at D = e + 16 and visible at e + 17; the keeper inputs say
			//    who it is and how hard it plays.
			bool bLate = true;
			bool bKeeper = true;
			{
				FHandDuel H;
				H.Obs.Config.Skill = 0.f;
				H.Obs.Config.Identity = 2;
				H.Begin(400.f);
				const int32_t Commit = 30;
				const int32_t Acting = RL::ObsPlayerState + static_cast<int32_t>(EFighterState::Acting);
				float O[RL::ObsDim];
				for (int32_t F = 0; F <= 60; ++F)
				{
					H.Obs.BuildObservation(H.Duel, O, nullptr);
					if (F == 0)
					{
						bKeeper = O[RL::ObsSkill] == 0.f && O[RL::ObsIdentity + 2] == 1.f && O[RL::ObsIdentity] == 0.f && O[RL::ObsIdentity + 1] == 0.f;
					}
					if (F == Commit + RL::MaxPerceptionFrames || F == Commit + RL::MaxPerceptionFrames + 1)
					{
						const bool bSeen = F == Commit + RL::MaxPerceptionFrames + 1;
						bLate = bLate && O[Acting] == (bSeen ? 1.f : 0.f);
					}
					if (F == Commit) { bLate = bLate && H.Duel.CommitPlayerAttack(false); }
					H.Step();
				}
				FHandDuel W;
				W.Obs.Config.Identity = 7; // out of range -> the Warden
				W.Begin(400.f);
				W.Obs.BuildObservation(W.Duel, O, nullptr);
				bKeeper = bKeeper && O[RL::ObsIdentity] == 1.f && O[RL::ObsSkill] == 1.f;
			}
			Logf(Log, "skill 0, identity Returned: a swing at frame 30 is hidden at frame 46 and seen at 47 -> %s; keeper inputs (skill, one-hot identity, out-of-range -> Warden) -> %s",
				bLate ? "ok" : "WRONG", bKeeper ? "ok" : "WRONG");

			// 3. Skill 0 decides every 12 frames and strings a single attack; skill 1 every 6 and may chain.
			bool bGap = true;
			{
				FHandDuel H;
				H.Obs.Config.Skill = 0.f;
				H.Begin(400.f);
				bGap = H.Obs.IsDecisionPoint(H.Duel);
				H.Obs.ApplyAction(H.Duel, RL::ActionWait);
				for (int32_t F = 1; F <= 12; ++F)
				{
					H.Step();
					bGap = bGap && H.Obs.IsDecisionPoint(H.Duel) == (F == 12);
				}
			}
			const int32_t First = ChainableAttack();
			const int32_t Chain1 = First >= 0 ? AttacksAllowedInChain(1.f, First) : -1;
			const int32_t Chain0 = First >= 0 ? AttacksAllowedInChain(0.f, First) : -1;
			const bool bChain = Chain1 > 0 && Chain0 == 0;
			Logf(Log, "skill 0: next decision exactly 12 frames after a Wait -> %s; in %s's cancel window the mask allows %d chained attacks at skill 1, %d at skill 0 -> %s",
				bGap ? "ok" : "WRONG", First >= 0 ? RL::ActionName(First) : "?", Chain1, Chain0, bChain ? "ok" : "WRONG");
			return bFull && bZero && bMono && bClamp && bLate && bKeeper && bGap && bChain;
		}

		bool TestBreather(std::string& Log)
		{
			// The game's Easy breather (FRLConfig::MinSwingGap): after a swing, attack OPENERS stay masked until that many
			// frames have passed, while guarding and moving stay allowed; with no breather it may open again as soon as it
			// is free. Skill 0 (Easy's): single-attack strings, so no cancel window muddies the count.
			struct FResult { int32_t Swing = -1; int32_t Free = -1; int32_t Open = -1; bool bOthers = true; };
			auto Run = [](int32_t Gap)
			{
				FResult R;
				FHandDuel H;
				H.Obs.Config.Skill = 0.f;
				H.Obs.Config.MinSwingGap = Gap;
				H.Begin(250.f);
				const int32_t A = RL::MoveAction(EMoveId::BFastSlash);
				uint8_t Mask[RL::NumActions];
				H.Obs.BuildMask(H.Duel, Mask);
				if (Mask[A] == 0 || H.Obs.ApplyAction(H.Duel, A) != EMoveId::BFastSlash) { return R; }
				R.Swing = H.Duel.Frame;
				for (int32_t F = 0; F < 400 && R.Open < 0; ++F)
				{
					H.Step();
					if (H.Duel.Get(ESide::Boss).State == EFighterState::Acting) { continue; }
					if (R.Free < 0) { R.Free = H.Duel.Frame; }
					H.Obs.BuildMask(H.Duel, Mask);
					R.bOthers = R.bOthers && Mask[RL::MoveAction(EMoveId::BGuard)] == 1 && Mask[RL::MoveAction(EMoveId::BRetreat)] == 1;
					if (Mask[A] == 1) { R.Open = H.Duel.Frame; }
				}
				return R;
			};
			const int32_t Gap = RL::EasySwingGap;
			const FResult Off = Run(0);
			const FResult On = Run(Gap);
			const bool bOff = Off.Swing >= 0 && Off.Open == Off.Free && Off.Open - Off.Swing < Gap;
			const bool bOn = On.Swing >= 0 && On.Open - On.Swing == Gap && On.bOthers;
			Logf(Log, "a fast slash at frame %d: free again at %d; no breather -> it may open at %d (%s); Easy's %d-frame breather -> at %d, guarding and retreating allowed meanwhile (%s)",
				Off.Swing, Off.Free, Off.Open, bOff ? "ok" : "WRONG", Gap, On.Open, bOn ? "ok" : "WRONG");
			return bOff && bOn;
		}

		/** A keeper that only throws one move (and walks in until it can): the player's lesson is unambiguous. */
		class FOneMoveBrain : public IBossBrain
		{
		public:
			EMoveId Attack = EMoveId::BSweepLeft;
			EBrainMode Mode() const override { return EBrainMode::Pathbreaker; }
			const char* Name() const override { return "one move"; }
			void BeginEncounter(int32_t Seed) override { (void)Seed; }
			bool Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision) override
			{
				(void)OutDecision;
				if (!Duel.Get(ESide::Boss).IsActionable()) { return false; }
				if (Geo.Distance() <= Move(Attack).Range) { Duel.Commit(ESide::Boss, Attack); }
				else { Duel.Commit(ESide::Boss, EMoveId::BApproach); }
				return false;
			}
			void OnFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement) override
			{
				(void)Events; (void)Duel; (void)Geo; (void)PlayerMovement;
			}
			bool PopReadMeter(FReadMeterEvent& Out) override { (void)Out; return false; }
			const FBrainDecision& LastDecision() const override { return None; }
			int32_t Decisions() const override { return 0; }
			int32_t CountersLanded() const override { return 0; }
		private:
			FBrainDecision None;
		};

		/** A step-left habit player vs FOneMoveBrain for 120 s: its share of step-left answers early (swings 1-8) and late (31+). */
		void LearningRun(float LearnRate, FBotMemory* Memory, int32_t Seed, float& OutEarly, float& OutLate, int32_t& OutSwings,
			EMoveId Attack = EMoveId::BSweepLeft, float* OutHitRate = nullptr)
		{
			FBotProfile P = MakeBotProfile(EBotKind::Varied, 0.8f);
			P.Kind = EBotKind::Habit;
			for (int32_t C = 0; C < FBotProfile::HabitClasses; ++C) { P.Habit[0][C][2] = 1.f; P.Habit[1][C][2] = 1.f; } // always StepL
			P.HabitNoise = 0.f;
			P.bAdapts = false;
			P.AggroRate = 0.f;
			P.PunishRate = 0.f;
			P.LearnRate = LearnRate;
			FOneMoveBrain Brain;
			Brain.Attack = Attack;
			FEncounter Enc;
			Enc.bRecordRows = false;
			Enc.Begin(&Brain, Seed, true);
			FSimArena Arena;
			Arena.Reset(300.f);
			FPlayerBot Bot;
			Bot.Reset(P, Seed * 7919 + 17);
			Bot.SetMemory(Memory);
			int32_t Swings = 0;
			int32_t EarlyL = 0, EarlyN = 0, LateL = 0, LateN = 0;
			bool bAnswered = true;
			for (int32_t F = 0; F < 120 * FramesPerSecond; ++F)
			{
				Enc.ThinkBoss(Arena.Geometry());
				float Fwd = 0.f;
				float Lat = 0.f;
				Bot.Act(Enc.Duel, Arena, Fwd, Lat);
				Arena.LatchCommits(Enc.Duel);
				Arena.SnapshotPreStep(Enc.Duel);
				Enc.StepFrame(Arena, Bot.MovementSym());
				Bot.OnEvents(Enc.FrameEvents(), Enc.Duel);
				Arena.Integrate(Enc.Duel, Fwd, Lat);
				for (const FDuelEvent& E : Enc.FrameEvents())
				{
					if (E.Type == EDuelEvent::Commit && E.Side == ESide::Boss && Move(E.Move).IsAttack()) { ++Swings; bAnswered = false; }
					if (E.Type == EDuelEvent::Commit && E.Side == ESide::Player && !bAnswered && Swings > 0)
					{
						bAnswered = true;
						const bool bLeft = E.Sym == ESym::StepL;
						if (Swings <= 8) { ++EarlyN; EarlyL += bLeft ? 1 : 0; }
						else if (Swings > 30) { ++LateN; LateL += bLeft ? 1 : 0; }
					}
				}
			}
			OutEarly = EarlyN > 0 ? static_cast<float>(EarlyL) / static_cast<float>(EarlyN) : -1.f;
			OutLate = LateN > 0 ? static_cast<float>(LateL) / static_cast<float>(LateN) : -1.f;
			OutSwings = Swings;
			if (OutHitRate != nullptr)
			{
				const int32_t Hits = Enc.Stats.BossOutcomes[static_cast<int32_t>(EHitOutcome::Hit)];
				*OutHitRate = Enc.Stats.BossSwings > 0 ? static_cast<float>(Hits) / static_cast<float>(Enc.Stats.BossSwings) : 0.f;
			}
		}

		bool TestLearningPlayer(std::string& Log)
		{
			// A player who always dodges left meets a keeper that only throws the attack that punishes that habit hardest
			// (found by measurement, so the test follows the frame data). A fixed habit keeps walking into it; a learning
			// player stops — and carries the lesson into the session's next fight.
			EMoveId Worst = EMoveId::None;
			float WorstHit = -1.f;
			for (int32_t A = 0; A < RL::NumBossMoves; ++A)
			{
				const EMoveId M = RL::ActionMove(A);
				if (!Move(M).IsAttack() || Move(M).bUnblockable || M == EMoveId::BGrab) { continue; }
				float E = 0.f, L = 0.f, Hit = 0.f;
				int32_t N = 0;
				LearningRun(0.f, nullptr, 77, E, L, N, M, &Hit);
				if (N > 30 && Hit > WorstHit) { WorstHit = Hit; Worst = M; }
			}
			const int32_t Class = HabitClassOf(Move(Worst));
			float FixedEarly = 0.f, FixedLate = 0.f, LearnEarly = 0.f, LearnLate = 0.f, LearnHit = 0.f;
			int32_t FixedSwings = 0, LearnSwings = 0;
			FBotMemory Mem;
			LearningRun(0.f, nullptr, 77, FixedEarly, FixedLate, FixedSwings, Worst);
			LearningRun(0.3f, &Mem, 77, LearnEarly, LearnLate, LearnSwings, Worst, &LearnHit);
			const float QLeft = Class >= 0 ? Mem.Q[Class][2] : 0.f;
			Logf(Log, "always-step-left vs a %s-only keeper (it hits that habit %.0f%% of the time), 120 s: fixed habit steps left %.2f (answers 1-8) -> %.2f (31+); learning player %.2f -> %.2f (hit %.0f%%), Q(StepL) %+.2f after %d updates",
				Move(Worst).Name, 100.f * WorstHit, FixedEarly, FixedLate, LearnEarly, LearnLate, 100.f * LearnHit, QLeft, Mem.Updates);
			float NextEarly = 0.f, NextLate = 0.f;
			int32_t NextSwings = 0;
			LearningRun(0.3f, &Mem, 78, NextEarly, NextLate, NextSwings, Worst);
			Logf(Log, "next fight on the same memory: steps left %.2f in its first 8 answers (a fresh learner: %.2f)", NextEarly, LearnEarly);
			return Worst != EMoveId::None && WorstHit > 0.5f && FixedSwings > 40 && LearnSwings > 40 && FixedEarly > 0.6f
				&& LearnLate < FixedEarly - 0.3f && LearnHit < WorstHit - 0.05f && QLeft < -0.3f && Mem.Updates > 20 && NextEarly < LearnEarly;
		}

		bool TestNotebook(std::string& Log)
		{
			FRLPolicy Pol;
			if (!LoadBrainFixture(Pol)) { Logf(Log, "fixture failed to load: %s", Pol.GetError().c_str()); return false; }
			// 1. The notebook: every decision with a known answer is written down once, in the class of the move it threw.
			FRLSession S;
			FRLNotebook N;
			FRLBrain B;
			B.Bind(&Pol, &S, &N);
			FRunConfig Cfg;
			Cfg.Bot = HabitProfile();
			Cfg.Seed = 99;
			Cfg.bImmortal = true;
			Cfg.MaxFrames = 60 * FramesPerSecond;
			RunEncounter(B, Cfg);
			B.FlushNotebook();
			int32_t SumA = 0, SumP = 0;
			for (int32_t C = 0; C < FRLNotebook::Classes; ++C)
			{
				for (int32_t K = 0; K < NumPlayerSymbols; ++K) { SumA += N.Answers[C][K]; SumP += N.Predicted[C][K]; }
			}
			const bool bBook = N.Predictions > 20 && SumA == N.Predictions && SumP == N.Predictions && N.Correct <= N.Predictions
				&& N.ConfidentCorrect <= N.Confident && N.FightPredictions[0] == N.Predictions && N.Fights == 1;
			Logf(Log, "60 s vs a habit player: %d predictions written down (%d right, %.0f%%), %d confident (%d right); rows sum to the count: %s",
				N.Predictions, N.Correct, 100.f * N.Accuracy(), N.Confident, N.ConfidentCorrect, bBook ? "yes" : "NO");

			// 2. Sampling (the easiest difficulties) is reproducible per fight seed and differs from the argmax sometimes.
			auto Run = [&Pol](float Temperature, std::vector<FExchangeRow>& Rows)
			{
				FRLSession Mem;
				FRLBrain Brain;
				Brain.Bind(&Pol, &Mem);
				Brain.Configure(0.3f, 1);
				Brain.SetTemperature(Temperature);
				FRunConfig C;
				C.Bot = MakeBotProfile(EBotKind::Varied, 0.6f);
				C.Seed = 321;
				C.bImmortal = true;
				C.MaxFrames = 40 * FramesPerSecond;
				Rows.clear();
				RunEncounter(Brain, C, &Rows);
				return Brain.Observer().Params().Perception;
			};
			std::vector<FExchangeRow> A1, A2, G;
			const int32_t Perception = Run(1.f, A1);
			Run(1.f, A2);
			Run(0.f, G);
			int32_t Sampled = 0;
			for (const FExchangeRow& R : A1) { Sampled += std::strstr(R.Decision.Reason, "sampled") != nullptr ? 1 : 0; }
			const bool bSame = SameDecisions(A1, A2);
			const bool bDiffers = !SameDecisions(A1, G);
			const bool bSample = bSame && Sampled > 0 && bDiffers && Perception == RL::SkillParams(0.3f).Perception;
			Logf(Log, "temperature 1 at skill 0.3 (perception %d): two runs identical %s, %d of %d decisions off the argmax, differs from greedy %s -> %s",
				Perception, bSame ? "yes" : "NO", Sampled, static_cast<int32_t>(A1.size()), bDiffers ? "yes" : "NO", bSample ? "ok" : "WRONG");
			return bBook && bSample;
		}

		const FCoreTest RLTests[] = {
			{ "RL.ActionSpace",        "RL-0", &TestActionSpace },
			{ "RL.ChainStartupFloor",  "RL-3", &TestChainStartupFloor },
			{ "RL.MaskLegality",       "RL-0", &TestMaskLegality },
			{ "RL.ObservationBounds",  "RL-0", &TestObservationBounds },
			{ "RL.PerceptionDelay",    "RL-3", &TestPerceptionDelay },
			{ "RL.Tokens",             "RL-2", &TestTokens },
			{ "RL.AnswerLabel",        "RL-2", &TestAnswerLabel },
			{ "RL.ReadMeter",          "RL-5", &TestReadMeter },
			{ "RL.MirrorY",            "RL-5", &TestMirrorY },
			{ "RL.PolicyFixture",      "RL-5", &TestPolicyFixture },
			{ "RL.BrainDeterminism",   "RL-5", &TestBrainDeterminism },
			{ "RL.SessionCarry",       "RL-2", &TestSessionCarry },
			{ "RL.Skill",              "RL-7", &TestSkill },
			{ "RL.LearningPlayer",     "RL-4", &TestLearningPlayer },
			{ "RL.Notebook",           "RL-7", &TestNotebook },
			{ "RL.Breather",           "RL-7", &TestBreather },
		};
	}

	int32_t GetRLTests(const FCoreTest*& OutTests)
	{
		OutTests = RLTestImpl::RLTests;
		return static_cast<int32_t>(sizeof(RLTestImpl::RLTests) / sizeof(RLTestImpl::RLTests[0]));
	}
}
