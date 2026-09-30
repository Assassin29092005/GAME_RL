// Hellwalker — the open world game (the default mode).
//
// Explore a generated valley as the Game Animation Sample's character (motion matching; vault, mantle).
// Ring bells to rest. At a shrine's seal, challenge its boss: the explorer steps out, the arena duel —
// unchanged, the one B0 validated — is played on the shrine plaza, and the explorer steps back in when it
// ends. Every boss reads the same you: the session's playstyle model is shared across the whole world, so
// the final shrine's Warden has watched you fight the whole way there.
//
// Modes: Pathbreaker (scripted bosses; the final shrine still reads you), Hellwalker, 66 Days (Hellwalker
// with 66 lives — the save is erased when the last day passes). Progress is saved at bells and shrines.
//
// Command line (on top of FHWAutomation's): -HWWorldMode=pathbreaker|hellwalker|66 (skip the title),
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

	struct FMarker
	{
		FVector Location;
		FString Label;
		FLinearColor Color;
		bool bPrimary = false;
	};
	void GetMarkers(TArray<FMarker>& Out) const;

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
	bool bLoggedLookFix = false;
	int32 PendingDuel = -1;
	float PendingDuelAt = -1.f;
	FHWAutomation Automation;
};
