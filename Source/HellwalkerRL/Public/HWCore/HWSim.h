// HellwalkerRL — engine-free core. Headless simulation: a 2-D arena, simulated player profiles, and an
// encounter runner. B0's thesis simulator is built on this, and so are the Unreal automation tests,
// so "the thesis holds" is checked against exactly the rules the game ships with.

#pragma once

#include "HWEncounter.h"
#include "HWRandom.h"

namespace HW
{
	struct FVec2
	{
		float X = 0.f;
		float Y = 0.f;
	};

	/** 2-D arena. Geometry is the only thing it decides (IContactOracle); rules live in FDuel. */
	class FSimArena : public IContactOracle
	{
	public:
		FVec2 Pos[2];
		float ArenaRadius = 1400.f;
		float BodyRadius = 38.f;
		float MinSeparation = 90.f;
		/** Forward locomotion (Approach, DashIn) stops this close — here and in the Unreal boss's locomotion. */
		static constexpr float ApproachStopDistance = 130.f;

		void Reset(float StartDistance);
		float Distance() const;
		/** Where both fighters stand (what a brain may look at). */
		FDuelGeometry Geometry() const { return FDuelGeometry{ Pos[0].X, Pos[0].Y, Pos[1].X, Pos[1].Y }; }
		/** Unit vector from Side toward its opponent (lock-on is always on in the sim). */
		FVec2 FacingOf(ESide Side) const;
		/** Latch commit facing / step direction for moves committed on the current frame (before Step). */
		void LatchCommits(FDuel& Duel);
		/** Record each fighter's move and frame before Step (motion is integrated from these). */
		void SnapshotPreStep(const FDuel& Duel);
		/** Integrate one frame of motion after Step. PlayerFwd/PlayerLat: walk intent in target space (-1..1). */
		void Integrate(const FDuel& Duel, float PlayerFwd, float PlayerLat);

		bool Contacts(const FDuel& Duel, ESide Attacker) override;

	private:
		FVec2 StepDir[2];
		EMoveId PreMove[2] = { EMoveId::None, EMoveId::None };
		int32_t PreT[2] = { 0, 0 };
		EFighterState PreState[2] = { EFighterState::Idle, EFighterState::Idle };
		bool PreGuard[2] = { false, false };
	};

	/** The reference bots (B0's instrument), plus Habit: a procedurally generated habit player (RL.md §6, the RL keeper's
	 *  training population) that answers every boss swing from a response table. */
	enum class EBotKind : uint8_t { Masher, Turtle, Habitual, Varied, DodgerLeft, RhythmParrier, Habit };
	inline constexpr int32_t NumReferenceBotKinds = 6;
	const char* BotKindName(EBotKind K);

	/** A simulated player. Skill in [0, 1] scales reaction, timing noise, reads and punish rate. */
	struct FBotProfile
	{
		EBotKind Kind = EBotKind::Varied;
		float    Skill = 0.5f;
		float    ReactMean = 13.f;    // frames from a boss commitment to recognising it
		float    ReactSigma = 3.f;
		float    TimingSigma = 2.f;   // sd of parry / step execution, frames
		float    ParryAim = 4.f;      // intends to press this many frames before impact
		float    StepAim = 8.f;
		float    FeintRead = 0.2f;    // P(recognise a feint and wait for the real strike)
		float    KillerRead = 0.7f;   // P(recognise a killer-move telegraph)
		float    PunishRate = 0.6f;
		float    AggroRate = 0.25f;
		int32_t  ChainLen = 3;
		float    HeavyRate = 0.2f;
		float    SwitchRate = 0.f;
		float    PreferredRange = 170.f;

		// ---- Kind == Habit only (RL.md §6): a response table per boss swing class, two tables for habit switches.
		static constexpr int32_t HabitClasses = 4;    // the boss swing's symbol: BFast, BHeavy, BFeint, BKiller
		static constexpr int32_t HabitResponses = 8;  // Parry, Block, StepL, StepR, StepB, StepF, Attack, None
		float    Habit[2][HabitClasses][HabitResponses] = {}; // [table A / B][class][response] weights (need not sum to 1)
		float    HabitNoise = 0.f;    // P(a uniformly random response instead of the table)
		bool     bAdapts = true;      // the reference bots' wariness (warier of feints once caught, dodges flip side)
		// ---- Kind == Habit, learning players (RL.md §6 "players who adapt"): a value per (boss swing class, response),
		// learned from what each response earned against THIS keeper (hit -1, blocked +0.25, dodged +1, parried +1.5),
		// tilts the table: P(r) ~ table(r) * exp(Q(r) / LearnTemp). A keeper that leans on one counter teaches the player
		// to stop walking into it — the cheap stand-in for a human noticing the boss's favourite move.
		float    LearnRate = 0.f;     // 0 = a fixed habit; > 0 = learning (per answered swing)
		float    LearnTemp = 0.35f;
	};

	/** What a learning player keeps across the fights of a session (the owner resets it per session). */
	struct FBotMemory
	{
		float Q[FBotProfile::HabitClasses][FBotProfile::HabitResponses] = {};
		int32_t Updates = 0;
		void Reset() { *this = FBotMemory{}; }
	};
	/** What a simulated player saw and did, counted once per Act() (diagnostics for the Unreal-vs-simulator parity: the
	 *  same bot runs in both, so these say where the two worlds put it differently). */
	struct FBotDiag
	{
		int32_t Frames = 0;
		int32_t Actionable = 0;          // it could commit a move
		int32_t InRange = 0;             // within its attack range (185)
		int32_t InRangeActionable = 0;
		int32_t BossOpen = 0;            // a punish window: stagger, guard broken, hitstun, a long recovery
		int32_t BossOpenInRange = 0;
		int32_t BossSwinging = 0;        // a keeper swing in flight
		int32_t DefencePending = 0;      // an answer planned and not yet executed
		int32_t PunishStarts = 0;        // strings begun, by reason
		int32_t AggroStarts = 0;
		int32_t ResponseAttacks = 0;     // a keeper swing answered by swinging
		int32_t AttackCommits = 0;       // attacks actually committed
		int32_t WalkFwd = 0;
		int32_t WalkBack = 0;
		int32_t Guarding = 0;
		float   DistanceSum = 0.f;
		void Add(const FBotDiag& O)
		{
			Frames += O.Frames; Actionable += O.Actionable; InRange += O.InRange; InRangeActionable += O.InRangeActionable;
			BossOpen += O.BossOpen; BossOpenInRange += O.BossOpenInRange; BossSwinging += O.BossSwinging;
			DefencePending += O.DefencePending; PunishStarts += O.PunishStarts; AggroStarts += O.AggroStarts;
			ResponseAttacks += O.ResponseAttacks; AttackCommits += O.AttackCommits; WalkFwd += O.WalkFwd; WalkBack += O.WalkBack;
			Guarding += O.Guarding; DistanceSum += O.DistanceSum;
		}
	};

	FBotProfile MakeBotProfile(EBotKind Kind, float Skill);
	/** A habit player's weights over its 8 answers to a boss swing of class C (table, tilted by a learning player's values;
	 *  before HabitNoise and the killer override). Returns their sum. The bot draws from these; tests read them. */
	float HabitWeights(const FBotProfile& P, const FBotMemory* Mem, int32_t Phase, int32_t C, float OutW[FBotProfile::HabitResponses]);
	/** The probability a habit player answers class C with response R (from HabitWeights). */
	float HabitChoiceProbability(const FBotProfile& P, const FBotMemory* Mem, int32_t Phase, int32_t C, int32_t R);
	/** Habit class of a boss swing (its symbol): 0 BFast, 1 BHeavy, 2 BFeint, 3 BKiller; -1 not a boss attack. */
	int32_t HabitClassOf(const FMoveData& M);

	class FPlayerBot
	{
	public:
		void Reset(const FBotProfile& InProfile, int32_t Seed);
		/** Decide and apply this frame's input. Outputs the walk intent in target space. */
		void Act(FDuel& Duel, const FSimArena& Arena, float& OutFwd, float& OutLat);
		/**
		 * The player learns too: getting caught by a feint makes it warier of feints; getting caught
		 * mid-dodge by a tracking sweep pushes it to dodge the other way. Wariness fades. A B0 that only
		 * tested players who never adjust would flatter the adaptive boss.
		 */
		void OnEvents(const std::vector<FDuelEvent>& Events, const FDuel& Duel);
		ESym MovementSym() const;
		const FBotProfile& Profile() const { return Prof; }
		/** Habit players: which table answers (0 = A, 1 = B after a habit switch). The owner schedules switches. */
		void SetHabitPhase(int32_t Phase) { HabitPhase = Phase > 0 ? 1 : 0; }
		int32_t GetHabitPhase() const { return HabitPhase; }
		/** The boss swing (its commit frame) the bot last planned an answer for, and the habit table it used (-1 = none). */
		int32_t PlannedSwingFrame() const { return PlannedCommitFrame; }
		int32_t PlannedHabitPhase() const { return PlannedPhase; }
		/** Learning players: the session's memory (null = the bot's own, which lasts one fight). Set after Reset. */
		void SetMemory(FBotMemory* InMemory) { Memory = InMemory; }
		const FBotMemory& GetMemory() const { return Memory != nullptr ? *Memory : OwnMemory; }
		/** Diagnostics since Reset (parity). */
		const FBotDiag& Diag() const { return DiagC; }

	private:
		enum class EResp : uint8_t { None, Parry, Block, StepL, StepR, StepB, StepF, Attack };
		EResp ChooseResponse(const FMoveData& M, bool& bOutSawFeint);
		EResp ChooseResponseCore(const FMoveData& M, bool bKiller, bool bSeesKiller);

		FBotProfile Prof;
		FRandom Rng;
		float   FeintWariness = 0.f;   // added to FeintRead
		float   SideFlip = 0.f;        // probability a habitual lateral dodge goes the other way

		int32_t PlannedCommitFrame = -1;
		int32_t RecogniseAt = -1;
		EResp   Planned = EResp::None;
		int32_t ExecuteAt = 0;
		int32_t GiveUpAt = 0;
		bool    bExecuted = true;
		int32_t GuardUntil = -1;
		int32_t StringLeft = 0;
		bool    bStringHeavy = false;
		int32_t NextAggroCheck = 0;
		int32_t PunishedWindowFrame = -1;
		float   Fwd = 0.f;
		float   Lat = 0.f;
		int32_t HabitPhase = 0;
		// Learning players: the response planned for the boss swing committed at RespSwingFrame, and its class.
		FBotMemory* Memory = nullptr;
		FBotMemory OwnMemory;
		int32_t RespSwingFrame = -1;
		int32_t RespClass = -1;
		int32_t RespColumn = -1;
		int32_t PlannedPhase = -1;
		FBotDiag DiagC;
	};

	struct FRunConfig
	{
		FBotProfile Bot;
		int32_t     Seed = 1;
		int32_t     MaxFrames = 90 * FramesPerSecond;
		bool        bImmortal = false;
		float       StartDistance = 650.f;
		/** The keeper's health relative to the tuning (RL::KeeperHealthScale for the RL keepers); mortal runs only. */
		float       BossHealthScale = 1.f;
	};

	/**
	 * Run one encounter of Brain against a simulated player. The brain is configured by the caller (script, session
	 * memory); only its per-fight state is reset here, so a session is several calls on the same brain.
	 */
	FEncounterStats RunEncounter(IBossBrain& Brain, const FRunConfig& Cfg, std::vector<FExchangeRow>* OutRows = nullptr);
}
