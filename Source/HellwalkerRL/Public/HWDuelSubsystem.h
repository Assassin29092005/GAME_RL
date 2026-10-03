// Hellwalker — the duel conductor.
//
// One tick-driven frame cursor for both fighters (PLAN §5.2 — not FTimerManager): each whole frame,
//   1. the boss brain decides (it cannot see anything the player pressed on this frame),
//   2. timestamped player input due on this frame is applied,
//   3. new commitments are latched (facing, target-space ghoststep direction),
//   4. FDuel steps — hit detection is a per-frame SWEEP of the attack's volume (§5.4), not an overlap event,
//   5. events fan out to the model, the brain, telemetry (A2), presentation and delegates.
// The rules themselves are HW::FEncounter — the exact code the B0 simulator validated. The boss brain is the tier's:
// Pathbreaker = HW::FScriptBrain (the script verbatim), Hellwalker = HW::FRLBrain (the RL keeper, RL.md §8).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HWTypesUE.h"
#include "HWCore/HWEncounter.h"
#include "HWCore/HWRLBrain.h"
#include "HWCore/HWScriptBrain.h"
#include "HWCore/HWSim.h"
#include "HWContactOracle.h"
#include "HWCore/HWRLTypes.h"
#include "HWDuelSubsystem.generated.h"

class AHWCharacterBase;
class UHWSessionSubsystem;

UCLASS()
class HELLWALKERRL_API UHWDuelSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UHWDuelSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// ---- wiring ---------------------------------------------------------------------------------
	void RegisterFighters(AHWCharacterBase* InPlayer, AHWCharacterBase* InBoss);
	AHWCharacterBase* GetFighterActor(HW::ESide Side) const;
	void SetArenaSpawns(const FVector& InPlayerSpawn, const FVector& InBossSpawn, float InArenaYaw = 0.f)
	{
		PlayerSpawn = InPlayerSpawn;
		BossSpawn = InBossSpawn;
		ArenaYaw = InArenaYaw;
	}
	/** Forget the fighters (their actors are about to go) and return to WaitingToStart. */
	void ClearFighters();
	/** Open world: which boss this is — its script (HW::BossScript), a health scale and which keeper the RL network plays
	 *  (HW::RL::EKeeper: 0 Warden, 1 Sage, 2 Returned; -1 = from the boss's look / the script). Applied at every reset. */
	void ConfigureBoss(int32 InScript, float InHealthScale, int32 InKeeper = -1)
	{
		BossScript = InScript;
		BossHealthScale = InHealthScale;
		KeeperOverride = InKeeper;
		bBossConfigured = true;
	}
	/** Which keeper a boss is: -HWKeeper=<0|1|2> wins, then an explicit choice, then its look (Sevarog the Warden, Wukong
	 *  the Sage, the Golem the Returned), then its script (1 is the Sage's). Pure: tested. */
	static int32 KeeperIdentityFor(FName Cast, int32 Script, int32 Explicit, int32 CommandLine);
	/** The RL keeper's configuration for the running fight (F3 overlay, result screens). */
	int32 GetKeeperIdentity() const { return KeeperIdentity; }
	float GetKeeperSkill() const { return KeeperSkill; }
	float GetKeeperTemperature() const { return KeeperTemperature; }
	int32 GetKeeperSwingGap() const { return KeeperSwingGap; }
	bool IsKeeperAdaptive() const { return bKeeperAdaptive; }
	/** Debug / flow tests (hw.Kill): drop a fighter's health to zero; the duel ends on its next frame. */
	void DebugKill(HW::ESide Side);

	/** Open world: no start / end screens or tier keys; the game mode owns the flow. */
	bool bOpenWorld = false;
	/** Shown over the boss health bar. */
	FString BossTitle = TEXT("The Ninefold Warden");
	/** Raised once when an encounter ends (true = the player won). */
	DECLARE_MULTICAST_DELEGATE_OneParam(FHWEncounterEnded, bool);
	FHWEncounterEnded OnEncounterEnded;

	// ---- encounter control ----------------------------------------------------------------------
	/** Begin an encounter at the given tier (from the start screen, or a tier switch). */
	void StartEncounter(EHWTier Tier);
	/** A1: ResetEncounter() — duel + brain reset and re-seeded; the session model persists. Seed < 0 = next seed. */
	void ResetEncounter(int32 InSeed = -1);
	EHWEncounterState GetState() const { return State; }
	EHWTier GetTier() const;
	UHWSessionSubsystem* GetSession() const;

	// ---- input (timestamped, PLAN §5.2) ---------------------------------------------------------
	void QueuePlayerInput(EHWPlayerAction Action, HW::EDir StepDir = HW::EDir::None);
	/** A3 console exec: press parry exactly N frames before the next boss swing's impact. */
	void InjectParry(int32 FramesBeforeImpact);
	/** Let a simulated player (the B0 instrument) drive the player character — demos and end-to-end checks. */
	void SetAutoplay(bool bEnable, HW::EBotKind Kind = HW::EBotKind::Varied, float Skill = 0.7f);
	bool IsAutoplay() const { return bAutoplay; }
	/** Walk intent in target space (x = right, y = forward), for the player controller when autoplay drives. */
	FVector2D GetAutoplayWalk() const { return AutoplayWalk; }

	// ---- views for presentation / HUD -------------------------------------------------------------
	const HW::FEncounter* GetEncounter() const { return Encounter.Get(); }
	/** The RL keeper when it is the one fighting (F3 overlay), else null. */
	const HW::FRLBrain* GetRLBrain() const { return Encounter.IsValid() && Encounter->Brain() == &RLBrain ? &RLBrain : nullptr; }
	/** Where the fighters stand, for the brain (simulator handedness via bMirrorY). */
	HW::FDuelGeometry CurrentGeometry() const;
	const HW::FFighter& GetFighterState(HW::ESide Side) const;
	int32 GetDuelFrame() const;
	/** Fraction of the next frame already elapsed — smooths procedural animation between frames. */
	float GetFrameAlpha() const { return static_cast<float>(FrameCursor); }
	bool GetActiveReadMeter(HW::FReadMeterEvent& Out, float& OutAlpha) const;
	const TArray<FString>& GetDecisionLog() const { return DecisionLog; }
	float GetMeanFrameMs() const;
	const FString& GetTelemetryPath() const { return TelemetryPath; }
	/** Horizontal unit vector from a fighter toward its opponent (the lock-on / target-space axis). */
	FVector TargetDirection(HW::ESide From) const;
	float FighterDistance() const;
	bool CanPlayerMove() const;
	float PlayerMoveScale() const;

	struct FFlash
	{
		FVector Location;
		FLinearColor Color;
		float Size = 1.f;
		int32 FramesLeft = 0;
		int32 FramesTotal = 1;
	};
	const TArray<FFlash>& GetFlashes() const { return Flashes; }

	/** Hit volume of the last contact query per side (debug draw). */
	struct FHitVolume
	{
		FVector Center = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		int32 Frame = -1;
		bool bContact = false;
	};
	const FHitVolume& GetLastHitVolume(HW::ESide Side) const { return LastVolume[HW::SideIndex(Side)]; }

	bool bDebugDraw = false;

	/** PLAN A1: EHitOutcome on one delegate. */
	UPROPERTY(BlueprintAssignable, Category = "Hellwalker")
	FHWSwingOutcomeSignature OnSwingOutcome;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	friend class FHWContactOracle;

	void StepOneFrame();
	void ApplyDueInput();
	void RunAutoplay();
	void LatchCommits();
	void HandleEvents();
	void EndEncounter(bool bPlayerWon);
	void PlaceFighters();
	HW::ESym PlayerMovementSymbol() const;
	FVector StepWorldDirection(HW::ESide Side, HW::EDir Dir) const;
	void AddFlash(const FVector& Where, const FLinearColor& Color, float Size, int32 Frames);
	void Rumble(float Intensity, float Seconds) const;
	void RefreshKeeperIdentity();

	void OpenTelemetry();
	void FlushTelemetryRows();
	void CloseTelemetry();

	struct FQueuedInput
	{
		EHWPlayerAction Action = EHWPlayerAction::Light;
		HW::EDir Dir = HW::EDir::None;
		int32 Frame = 0;
	};

	TUniquePtr<FHWContactOracle> Oracle;
	TUniquePtr<HW::FEncounter> Encounter;
	HW::FScriptBrain ScriptBrain;
	HW::FRLBrain RLBrain;
	TUniquePtr<HW::FPlayerBot> Bot;
	HW::FSimArena BotArena;

	TWeakObjectPtr<AHWCharacterBase> PlayerFighter;
	TWeakObjectPtr<AHWCharacterBase> BossFighter;
	FVector PlayerSpawn = FVector(-450.f, 0.f, 100.f);
	FVector BossSpawn = FVector(450.f, 0.f, 140.f);
	float ArenaYaw = 0.f;
	int32 BossScript = 0;
	float BossHealthScale = 1.f;
	bool bBossConfigured = false; // ConfigureBoss was called (the open world); otherwise the arena: health from the keeper
	int32 KeeperOverride = -1;
	int32 KeeperIdentity = 0;
	float KeeperSkill = 1.f;
	float KeeperTemperature = 0.f;
	int32 KeeperSwingGap = 0;
	bool bKeeperAdaptive = false;

	EHWEncounterState State = EHWEncounterState::WaitingToStart;
	double FrameCursor = 0.0;
	int32 Seed = 1;
	int32 HitstopFrames = 0;

	TArray<FQueuedInput> InputQueue;
	int32 InjectParryOffset = -1;
	int32 InjectedParryFrame = -1;

	bool bAutoplay = false;
	HW::EBotKind AutoplayKind = HW::EBotKind::Varied;
	float AutoplaySkill = 0.7f;
	FVector2D AutoplayWalk = FVector2D::ZeroVector;

	HW::FReadMeterEvent ReadMeter;
	int32 ReadMeterFramesLeft = 0;
	static constexpr int32 ReadMeterFrames = 24; // ~0.4 s (PLAN §2.4)

	TArray<FString> DecisionLog;
	TArray<FFlash> Flashes;
	FHitVolume LastVolume[2];

	// ---- the Unreal-vs-simulator parity harness (RL/parity.py; -HWParity=<file.jsonl>) -------------------------------
	// One JSON line per encounter with the C++ eval's own statistics (hwrl_eval_sessions: FEncounterStats + the
	// keeper's moves and the distance at its commits), the start distance drawn as the simulator draws it (450-800)
	// and fights capped at the simulator's 180 s. -HWSeedBase=<n> offsets the encounter seeds (one launch = one session).
	void WriteParityRecord(bool bPlayerWon);
	FString ParityPath;
	int32 SeedBase = 0;
	bool bParityTimeout = false;
	float ParityStartDistance = 0.f;
	double ParityDistanceSum = 0.0;
	int32 ParityDistanceSamples = 0;
	int32 ParityBossMoves[HW::RL::NumBossMoves] = {};
	double ParityRealSeconds = 0.0;
	// Experiments on the gap (command line): -HWMoveAccel= / -HWMoveBraking= (cm/s², both duelists' walking),
	// -HWNoHitstop (the duel never holds still on a hit).
	float MoveAccelOverride = -1.f;
	float MoveBrakingOverride = -1.f;
	bool bNoHitstop = false;
	static constexpr int32 ParityMaxFrames = 180 * 60;

	FString TelemetryPath;
	double LastRealTime = 0.0;
	double FrameTimeAccumMs = 0.0;
	int32 FrameTimeSamples = 0;
	int32 TelemetryRows = 0;
	std::vector<HW::FExchangeRow> RowScratch;

};
