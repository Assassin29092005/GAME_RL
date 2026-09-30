// Hellwalker — engine-free core. Headless simulation: a 2-D arena, simulated player profiles, and an
// encounter runner. B0's thesis simulator is built on this, and so are the Unreal automation tests,
// so "the thesis holds" is checked against exactly the rules the game ships with.

#pragma once

#include "HWEncounter.h"

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

		void Reset(float StartDistance);
		float Distance() const;
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

	enum class EBotKind : uint8_t { Masher, Turtle, Habitual, Varied, DodgerLeft, RhythmParrier };
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
	};
	FBotProfile MakeBotProfile(EBotKind Kind, float Skill);

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
	};

	struct FRunConfig
	{
		EBrainMode  Mode = EBrainMode::Pathbreaker;
		FBotProfile Bot;
		int32_t     Seed = 1;
		int32_t     MaxFrames = 90 * FramesPerSecond;
		bool        bImmortal = false;
		float       StartDistance = 650.f;
		const FBrainConfig* BrainConfig = nullptr;
		int32_t     Script = 0;          // HW::BossScript index (0 the Warden, 1 the Monkey Sage)
	};

	/** Run one encounter on a (session-persistent) model. Optional logs. */
	FEncounterStats RunEncounter(FPlaystyleModel& Model, const FRunConfig& Cfg,
		std::vector<FExchangeRow>* OutRows = nullptr, std::vector<FSymbolRecord>* OutSymbols = nullptr);
}
