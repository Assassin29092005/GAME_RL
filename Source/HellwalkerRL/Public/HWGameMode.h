// Hellwalker — game mode. Builds the arena and the boss in C++ on the engine's empty Entry map (no
// project .umap), places the player without a PlayerStart, and wires both fighters to the duel.
//
// Command-line switches (for headless demos and end-to-end checks):
//   -HWAutoStart                 skip the start screen
//   -HWTier=Pathbreaker|Hellwalker
//   -HWAutoplay=<masher|turtle|habitual|varied|dodger|rhythm>  -HWAutoplaySkill=0.7
//   -HWBlind                     B4 blind labels (Variant A / B)
//   -HWDebug                     debug overlay + hit volumes
//   -HWShotAt=<s>  -HWQuitAt=<s> take a screenshot / quit after N seconds of play
//   -HWShotEvery=<s> -HWShots=<n>  after the first, keep taking one every <s> seconds, <n> in all
//   -HWExec="<cmd>[|<cmd>...]" -HWExecAt=<s>  run console commands once, N seconds in (default 1)
//   -HWBoss=Sevarog|Wukong  -HWGreybox  -HWAnimSurvey   C2 casts (HWAnimCasts.cpp)
//   -HWEncounters=N              run N encounters back to back (the Warden keeps learning), then quit

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HWGameMode.generated.h"

class AHWArena;
class AHWBossCharacter;
class UHWCombatAnimConfig;

UCLASS()
class HELLWALKERRL_API AHWGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHWGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Designer-editable frame data (PLAN §5 "UCombatAnimConfig"). Null = the core's compiled defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hellwalker")
	TObjectPtr<UHWCombatAnimConfig> CombatConfig;

	UPROPERTY(EditAnywhere, Category = "Hellwalker")
	FVector PlayerSpawn = FVector(-450.f, 0.f, 100.f);

	UPROPERTY(EditAnywhere, Category = "Hellwalker")
	FVector BossSpawn = FVector(450.f, 0.f, 140.f);

protected:
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

private:
	UPROPERTY() TObjectPtr<AHWArena> Arena;
	UPROPERTY() TObjectPtr<AHWBossCharacter> Boss;

	float PlaySeconds = 0.f;
	int32 AutoEncounters = 0;       // -HWEncounters=N: restart automatically until N encounters have run
	float EncounterOverSeconds = -1.f;
	float ShotAt = -1.f;
	float QuitAt = -1.f;
	int32 ShotsTaken = 0;
	float ShotEvery = -1.f;
	FString ExecCmds;
	float ExecAt = 1.f;
	int32 ShotsWanted = 1;
};
