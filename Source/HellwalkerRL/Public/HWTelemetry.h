// HellwalkerRL — anonymous research telemetry (web/CONTRACT.md is the contract; web/README.md the setup).
//
// Each copy of the game signs in to Firebase anonymously (plain REST, no window, no account), creates players/{uid}
// once with a made-up nickname, and after every finished duel sends ONE Firestore documents:commit: the fight
// (fights/{id}, immutable) plus server-side increments of the player's totals. Failed uploads wait in a save slot and
// are retried at the next duel and at start-up; a retried fight is idempotent (the commit requires the fight to be new).
//
// Off — silently — until Config/DefaultGame.ini [HWTelemetry] has ApiKey and ProjectId (or -HWNoTelemetry). Research
// integrity: fights touched by a tool are never uploaded — the autoplay bot, parity runs, hw.Kill / hw.InjectParry /
// hw.Hold, and any scripted launch (-unattended, -HWExec) — unless -HWTelemetryEndpoint (a local mock) AND
// -HWTelemetryAllowAutoplay are both given (tests). Tokens are never logged. A test launch (-HWTelemetryEndpoint) uses its
// own save slot, so it never touches the real identity or queue.
// Command line for tests: -HWTelemetryEndpoint=<url> (all three REST bases -> web/dev/mock_firebase.py),
// -HWTelemetryApiKey=, -HWTelemetryProjectId=, -HWTelemetrySiteUrl=.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "HWCore/HWRLBrain.h"
#include "HWTelemetry.generated.h"

class UHWDuelSubsystem;

/** One duel, as the contract's fights/{id} document carries it (pure data: built from the duel, tested). */
struct HELLWALKERRL_API FHWFightRecord
{
	FString Player;                 // uid
	FDateTime ClientTime;
	FString Session;                // 32 hex, per launch
	int32 FightInSession = 1;
	FString GameVersion;
	FString Mode;                   // openworld | arena
	FString PlayMode;               // pathbreaker ("Normal") | hellwalker ("Adaptive AI") | arena (66days: builds before 1.4.0)
	FString Brain;                  // rl | script
	int32 Keeper = 0;
	FString KeeperName;
	FString Difficulty;             // Easy | Normal | Hard | Hellwalker (Adaptive: builds before 1.4.0)
	float Skill = 1.f;
	bool bAdaptive = false;         // the keeper's skill came from the insight ramp (every RL fight)
	FString Result;                 // win | loss | timeout | quit
	float Seconds = 0.f;
	float PlayerHealth = 1.f, KeeperHealth = 1.f;
	float DmgDealt = 0.f, DmgTaken = 0.f;
	int32 PlayerSwings = 0, PlayerHits = 0;
	int32 KeeperSwings = 0, KeeperHits = 0, KeeperBlocked = 0, KeeperParried = 0, KeeperWhiffed = 0;
	int32 ParryAttempts = 0, Dodges = 0, GuardBreaks = 0, KeeperExposed = 0, ReadsLanded = 0;
	int32 Predictions = 0, PredictionsCorrect = 0, Confident = 0, ConfidentCorrect = 0;
	struct FExpected { FString Attack; FString Answer; float P = 0.f; };
	TArray<FExpected> Expected;     // 8 rows for the RL keeper, empty for the script
	int32 Answers[4][8] = {};       // [fast, heavy, feint, killer][parry, block, stepL, stepR, stepB, stepF, attack, none]
	// v2 (1.4.0): how the fight was set up when it began.
	FString Assist = TEXT("off");   // off | ring | ring+slowmo (the parry assist)
	float SlowmoScale = 1.f;        // the duel's speed while the ring was lit: 1 unless ring+slowmo
	float KeeperDamageScale = 1.f;  // the keeper's health damage was multiplied by this (dmgTaken is after it)
	int32 ParryWindowFrames = 12;   // HW::Move(PParry).Active
	float Insight = 0.f;            // how well the keeper knew the player, 0..1 (0 for the script)
};

namespace HWTelemetry
{
	/** The fight document's schema version (web/CONTRACT.md; the players document stays at v 1). */
	constexpr int32 FightSchemaVersion = 2;

	HELLWALKERRL_API extern const TCHAR* const ClassKeys[4];   // fast heavy feint killer
	HELLWALKERRL_API extern const TCHAR* const AnswerKeys[8];  // parry block stepL stepR stepB stepF attack none
	HELLWALKERRL_API extern const TCHAR* const KeeperKeys[3];  // warden sage returned

	/** A player symbol (HW::ESym, 0..11) as the contract's answer column; -1 if none. */
	HELLWALKERRL_API int32 AnswerColumn(int32 PlayerSym);
	/** The contract's answer key for a player symbol (e.g. "stepL"; Light/Heavy -> "attack", Neutral/moves -> "none"). */
	HELLWALKERRL_API FString AnswerKey(HW::ESym Sym);
	/** The contract's assist key for an assist name: "ring+slowmo" (also "RingSlow", "Ring + slow motion"), "ring", else "off". */
	HELLWALKERRL_API FString AssistKey(const FString& Name);
	/** Per-fight notebook changes: Now minus Before, clamped at 0 (a reset notebook starts again from zero). */
	HELLWALKERRL_API void NotebookDelta(const HW::FRLNotebook& Before, const HW::FRLNotebook& Now, FHWFightRecord& InOut);
	/** 32 lowercase hex characters. */
	HELLWALKERRL_API FString NewId();
	/** "<Adjective> <Noun> <4 digits>", e.g. "Ashen Wanderer 4821" (at most 24 characters). */
	HELLWALKERRL_API FString MakeNickname(FRandomStream& Rng);
	/** "projects/{P}/databases/(default)/documents" — the prefix of every document name. */
	HELLWALKERRL_API FString DocumentsRoot(const FString& ProjectId);
	/** The fight document's typed Firestore fields (JSON object text, no `at`: the server sets it). */
	HELLWALKERRL_API FString FightFieldsJson(const FHWFightRecord& R);
	/** The contract's per-fight commit body: create fights/{FightId} + update players/{uid} with increments. */
	HELLWALKERRL_API FString FightCommitJson(const FHWFightRecord& R, const FString& ProjectId, const FString& FightId);
	/** The one-time players/{uid} creation commit (nickname, zero totals, server-time createdAt). */
	HELLWALKERRL_API FString PlayerCreateCommitJson(const FString& ProjectId, const FString& Uid, const FString& Nickname,
		const FString& GameVersion, const FString& Difficulty);
}

USTRUCT()
struct FHWTelemetryPending
{
	GENERATED_BODY()
	UPROPERTY() FString FightId;
	UPROPERTY() FString ProjectId;   // the project the body names (a body for another project is dropped)
	UPROPERTY() FString Body;        // the documents:commit JSON, with "{uid}" where the player id goes (filled at send time)
	UPROPERTY() FString RecordedAt;  // ISO 8601 (UTC): a website reset after this drops it
	UPROPERTY() int32 Attempts = 0;
};

/** The telemetry identity and the upload queue (its own slot: the settings and the walk saves never carry tokens). */
UCLASS()
class HELLWALKERRL_API UHWTelemetrySave : public USaveGame
{
	GENERATED_BODY()
public:
	static constexpr const TCHAR* SlotName = TEXT("HellwalkerRL_Telemetry");
	static constexpr const TCHAR* TestSlotName = TEXT("HellwalkerRL_Telemetry_Test"); // -HWTelemetryEndpoint launches
	UPROPERTY() FString ProjectId;   // the identity below belongs to this project
	UPROPERTY() FString Uid;
	UPROPERTY() FString RefreshToken;
	UPROPERTY() FString Nickname;
	UPROPERTY() bool bPlayerCreated = false;
	UPROPERTY() TArray<FHWTelemetryPending> Pending;
};

UCLASS()
class HELLWALKERRL_API UHWTelemetrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Configured (ApiKey + ProjectId) and not switched off. */
	bool IsEnabled() const { return bEnabled; }
	/** A duel starts: the notebook baseline for this fight's numbers (UHWDuelSubsystem::ResetEncounter). */
	void BeginFight();
	/** Called once per finished duel (UHWDuelSubsystem, after the frame's READ is counted). */
	void RecordFight(const UHWDuelSubsystem& Duel, bool bPlayerWon, bool bTimeout);
	/** "Open my stats page": the site's #/me linked to this copy of the game. Empty reason = available. */
	FString StatsPageUnavailableReason() const;
	bool OpenStatsPage() const;
	int32 GetPendingCount() const { return Save != nullptr ? Save->Pending.Num() : 0; }
	FString GetNickname() const { return Save != nullptr ? Save->Nickname : FString(); }

	/** Build the record for a duel (pure apart from reading the subsystems; exposed for tests and logs). */
	bool BuildRecord(const UHWDuelSubsystem& Duel, bool bPlayerWon, bool bTimeout, FHWFightRecord& Out, FString& OutSkipReason);

private:
	FString AuthBase() const;
	FString TokenBase() const;
	FString FirestoreDocsUrl() const;
	void EnsureIdentity();
	void SignUp();
	void RefreshIdToken();
	void CreatePlayer();
	void Flush();
	void OnCommitDone(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk, FString FightId);
	/** After a refused commit: does the fight exist already (a delivered retry refused by the rules)? */
	void CheckFightDelivered(const FString& FightId, int32 Code);
	/** Once per launch: a reset on the website drops the fights queued before it. */
	void SyncReset();
	int32 FirstPendingIndex() const;
	const TCHAR* SlotName() const;
	void PersistSave();
	bool HasFreshToken() const;

	UPROPERTY() TObjectPtr<UHWTelemetrySave> Save;
	bool bEnabled = false;
	bool bAllowAutoplay = false;
	/** A development build with no mock endpoint and no -HWTelemetryDev: telemetry is off (the developer's own play). */
	bool bDevBuildOff = false;
	FString ApiKey, ProjectId, SiteUrl, Endpoint, GameVersion;
	FString Session;
	int32 FightsThisSession = 0;
	FString IdToken;
	double IdTokenTime = -1.0;
	bool bBusy = false;              // one request in flight at a time (identity, creation, or the queue head)
	bool bResetSynced = false;
	HW::FRLNotebook StartBook;       // the notebook when this fight began (BeginFight)
	bool bHaveStartBook = false;
};
