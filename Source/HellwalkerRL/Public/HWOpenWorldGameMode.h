// Hellwalker — the open world game (the default mode).
//
// Explore a generated valley as the Game Animation Sample's character (motion matching; vault, mantle).
// Ring bells to rest. At a shrine's seal, challenge its boss: the explorer steps out, the arena duel —
// unchanged, the one B0 validated — is played on the shrine plaza, and the explorer steps back in when it
// ends. In Adaptive AI every boss reads the same you: the session's memory and insight are shared across the
// whole world, so the final shrine's keeper has watched you fight the whole way there.
//
// Modes (EHWPlayMode): Normal (= Pathbreaker: every keeper scripted, the final shrine too) and Adaptive AI
// (= Hellwalker: the RL keepers that learn you). The retired 66 Days loads and plays as Adaptive AI. Progress is saved
// at bells and shrines; dying wakes you at your bell.
//
// The valley map (M, AHWPlayerController) and keeper tracking read the keepers, bells and the tracked keeper from here
// (GetKeepers / GetBells / GetTrackedKeeper); the rule itself is HWMap::ResolveTrackedKeeper.
//
// Command line (on top of FHWAutomation's): -HWWorldMode=normal|pathbreaker|adaptive|hellwalker (skip the title; the
//   retired 66 starts Adaptive AI with a warning),
//   -HWContinue, -HWDuel=<shrine index> (challenge at once), -HWGoto=<site id>, -HWWorldSeed=<n>,
//   -HWNoSave (never write the save slot), -HWAutoplay=<bot> [-HWAutoplaySkill=] (the bot fights the duels),
//   -HWTour (the explorer walks the paths site to site, jumping at the ruins — demos and traversal checks).
//
// BUILD PHASE: new games and respawns put you just outside the gate of the next keeper you have not beaten
// (fast iteration on the duels). -HWSpawnAtBell restores the real rule: the first bell, then your last bell.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HWAutomation.h"
#include "HWTypesUE.h"
#include "HWOpenWorldGameMode.generated.h"

class AHWOpenWorld;
class AHWBell;
class AHWShrine;
class AHWPlayerCharacter;
class AHWBossCharacter;
class UHWSaveGame;
class ACameraActor;

UENUM()
enum class EHWWorldPhase : uint8
{
	Title,
	Exploring,
	Duel,
	DuelOver,
	Ending,
	Regret
};

UCLASS()
class HELLWALKERRL_API AHWOpenWorldGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHWOpenWorldGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	// ---- flow (controller keys, tests) --------------------------------------------------------------
	void NewGame(EHWPlayMode Mode);
	bool ContinueGame();
	void Interact();
	void BeginDuel(int32 ShrineIndex);
	void TeleportExplorer(FName SiteId);

	// ---- views for the HUD ------------------------------------------------------------------------------
	EHWWorldPhase GetPhase() const { return Phase; }
	float GetPhaseSeconds() const { return PhaseSeconds; }
	const UHWSaveGame* GetSave() const { return Save; }
	bool HasSavedGame() const { return bSaveExists; }
	const AHWOpenWorld* GetOpenWorld() const { return OpenWorld; }
	const FString& GetPrompt() const { return Prompt; }
	/** Big fading text (a place's name, a result). */
	bool GetBanner(FString& OutTitle, FString& OutSub, float& OutAlpha) const;
	FString ObjectiveText() const;
	int32 GetActiveShrine() const { return ActiveShrine; }
	bool LastDuelWon() const { return bLastWon; }

	/** The compass's marks: every keeper still standing (cleared ones drop off), then the bells. */
	struct FMarker
	{
		FVector Location;
		FString Label;
		FLinearColor Color;
		bool bPrimary = false;
		int32 Keeper = -1;        // the shrine index for a keeper, -1 for a bell
		bool bTracked = false;    // the tracked keeper (GetTrackedKeeper)
	};
	void GetMarkers(TArray<FMarker>& Out) const;

	// ---- the valley map and keeper tracking (HWMap.h) ------------------------------------------------------
	/** A keeper as the map shows it (Index = the shrine's, FHWWorldGen::Shrines() order). */
	struct FKeeperView
	{
		int32 Index = -1;
		FString Title;
		FVector Location = FVector::ZeroVector;   // the shrine's centre
		bool bCleared = false;
		bool bSealed = false;                     // the final gate, until the others fall
		bool bFinal = false;
	};
	void GetKeepers(TArray<FKeeperView>& Out) const;
	struct FBellView
	{
		FString Title;
		FVector Location = FVector::ZeroVector;
		bool bLit = false;
		bool bCheckpoint = false;                 // where you wake
	};
	void GetBells(TArray<FBellView>& Out) const;
	/** The keeper the HUD tracks: the player's pick (the map) while it stands, else the objective — the nearest open
	 * keeper, the final gate once it opens (HWMap::ResolveTrackedKeeper). -1 when every keeper has fallen. */
	int32 GetTrackedKeeper() const;
	/** The player's pick (-1: follow the objective). Cleared keepers cannot be picked. */
	void SetTrackedKeeper(int32 ShrineIndex);
	int32 GetPickedKeeper() const { return PickedKeeper; }
	/** The map opens while exploring only (not at the title, in a duel or an ending). */
	bool CanOpenMap() const { return Phase == EHWWorldPhase::Exploring && OpenWorld != nullptr; }
	/** Where the player stands and faces (the explorer pawn; else the camera). False before there is either. */
	bool GetPlayerSpot(FVector& OutLocation, float& OutYaw) const;
	/** The colour a keeper's marks use everywhere (compass, indicators, map): open, sealed, cleared. */
	static FLinearColor KeeperColor(bool bCleared, bool bSealed);

	/** The explorer pawn class (the Game Animation Sample's character, or the fallback). */
	UClass* GetExplorerClass() const { return ExplorerClass; }

private:
	APawn* SpawnExplorer(const FVector& Where, float Yaw);
	/** The explorer wears Soul: the combat Manny, retargeted live from the sample's motion-matched mannequin. */
	void DressExplorerAsSoul(APawn* Pawn);
	/** Where you (re)appear: outside the next keeper's gate in the build phase, else at your bell. */
	void SpawnAtStart(const FString& Subtitle);
	/** The next shrine to face: uncleared and unsealed (-1 when none). */
	int32 NextShrine() const;
	bool bSpawnAtBell = false;
	void RetireExplorer();
	void FinishDuel();
	void OnEncounterEnded(bool bPlayerWon);
	void RefreshSites();
	void WriteSave();
	void Banner(const FString& Title, const FString& Sub, float Seconds = 4.f);
	void SetPhase(EHWWorldPhase NewPhase);
	bool AllOthersCleared(int32 ShrineIndex) const;
	APlayerController* PC() const;

	UPROPERTY() TObjectPtr<AHWOpenWorld> OpenWorld;
	UPROPERTY() TArray<TObjectPtr<AHWBell>> Bells;
	UPROPERTY() TArray<TObjectPtr<AHWShrine>> Shrines;
	UPROPERTY() TObjectPtr<UHWSaveGame> Save;
	UPROPERTY() TObjectPtr<AHWPlayerCharacter> Soul;
	UPROPERTY() TObjectPtr<AHWBossCharacter> Boss;
	UPROPERTY() TObjectPtr<ACameraActor> TitleCamera;
	UPROPERTY() TObjectPtr<UClass> ExplorerClass;

	EHWWorldPhase Phase = EHWWorldPhase::Title;
	float PhaseSeconds = 0.f;
	int32 ActiveShrine = -1;
	bool bLastWon = false;
	bool bSaveExists = false;
	bool bNoSave = false;
	int32 WorldSeed = 7;
	FString Prompt;
	FString BannerTitle;
	FString BannerSub;
	float BannerLeft = 0.f;
	float BannerTotal = 1.f;
	void TickTour(float DeltaSeconds);
	bool bTour = false;
	TArray<int32> TourRoute;       // site indices
	int32 TourStep = 0;
	float TourJumpCooldown = 0.f;
	float TourStuckTime = 0.f;
	FVector TourLastPos = FVector::ZeroVector;
	UPROPERTY() TObjectPtr<class UInputAction> TourJumpAction;
	/** The explorer's own move action: the tour steers through it (a Mover pawn ignores AddMovementInput). */
	UPROPERTY() TObjectPtr<class UInputAction> TourMoveAction;
	/** The explorer's hidden sample body and Soul's look on it: kept that way every frame (the sample's ragdoll
	 * shows its body again when it falls). */
	TWeakObjectPtr<class USkeletalMeshComponent> ExplorerBody;
	TWeakObjectPtr<class USkeletalMeshComponent> ExplorerLook;
	// -HWPoseDump=<file> (diagnostics): the explorer's transforms per frame, written even in Shipping.
	FString PoseDumpPath;
	float PoseDumpClock = 0.f;
	bool bPoseDumpChecked = false;
	void TickPoseDump(float DeltaSeconds);
	bool bLoggedLookFix = false;
	int32 PendingDuel = -1;
	float PendingDuelAt = -1.f;
	/** The keeper picked on the map (-1: follow the objective). Not saved: a new walk or a continue follows the objective. */
	int32 PickedKeeper = -1;
	FHWAutomation Automation;
};
