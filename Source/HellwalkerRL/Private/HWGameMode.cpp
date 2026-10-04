#include "HWGameMode.h"

#include "HellwalkerRL.h"
#include "HWAnimTypes.h"
#include "HWArena.h"
#include "HWBossCharacter.h"
#include "HWCombatAnimConfig.h"
#include "HWDuelSubsystem.h"
#include "HWHUD.h"
#include "HWPlayerCharacter.h"
#include "HWPlayerController.h"
#include "HWSessionSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

namespace
{
	bool ParseBotKind(const FString& S, HW::EBotKind& Out)
	{
		const FString L = S.ToLower();
		if (L == TEXT("masher"))   { Out = HW::EBotKind::Masher; return true; }
		if (L == TEXT("turtle"))   { Out = HW::EBotKind::Turtle; return true; }
		if (L == TEXT("habitual")) { Out = HW::EBotKind::Habitual; return true; }
		if (L == TEXT("varied"))   { Out = HW::EBotKind::Varied; return true; }
		if (L == TEXT("dodger"))   { Out = HW::EBotKind::DodgerLeft; return true; }
		if (L == TEXT("rhythm"))   { Out = HW::EBotKind::RhythmParrier; return true; }
		return false;
	}
}

AHWGameMode::AHWGameMode()
{
	DefaultPawnClass = AHWPlayerCharacter::StaticClass();
	PlayerControllerClass = AHWPlayerController::StaticClass();
	HUDClass = AHWHUD::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
	// The scripted checks (-HWExec / -HWShotAt / -HWQuitAt) keep their clock while the pause menu is open; everything
	// this tick does is automation (the duel itself is paused by the world).
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AHWGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	if (CombatConfig != nullptr)
	{
		CombatConfig->ApplyToCore();
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("HWAnimSurvey")))
	{
		for (const FName& Cast : HWCastNames()) { UHWAnimSet::Survey(Cast); } // C2: choose clips from data
	}
}

void AHWGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// The Entry map's PlayerStart is not where the arena is: place the player explicitly.
	RestartPlayerAtTransform(NewPlayer, FTransform(FRotator::ZeroRotator, PlayerSpawn));
}

void AHWGameMode::StartPlay()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Arena = World->SpawnActor<AHWArena>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Boss = World->SpawnActor<AHWBossCharacter>(BossSpawn, FRotator(0.f, 180.f, 0.f), Params);

	Super::StartPlay();

	UHWDuelSubsystem* Duel = World->GetSubsystem<UHWDuelSubsystem>();
	AHWPlayerCharacter* Player = Cast<AHWPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (Duel == nullptr || Player == nullptr || Boss == nullptr)
	{
		UE_LOG(LogHellwalkerRL, Error, TEXT("Hellwalker could not wire the duel (subsystem %d, player %d, boss %d)."),
			Duel != nullptr, Player != nullptr, Boss != nullptr);
		return;
	}
	Duel->SetArenaSpawns(PlayerSpawn, BossSpawn);
	Duel->RegisterFighters(Player, Boss);

	// ---- command-line automation
	const TCHAR* Cmd = FCommandLine::Get();
	UHWSessionSubsystem* Session = Duel->GetSession();
	FString Value;
	if (Session != nullptr)
	{
		if (FParse::Value(Cmd, TEXT("HWTier="), Value))
		{
			// Pathbreaker = the mode shown as Normal (the script); anything else (Hellwalker, Adaptive) is Adaptive AI.
			const bool bScript = Value.Equals(TEXT("Pathbreaker"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Normal"), ESearchCase::IgnoreCase);
			Session->Tier = bScript ? EHWTier::Pathbreaker : EHWTier::Hellwalker;
		}
		Session->bBlind = FParse::Param(Cmd, TEXT("HWBlind"));
	}
	if (FParse::Value(Cmd, TEXT("HWAutoplay="), Value))
	{
		HW::EBotKind Kind = HW::EBotKind::Varied;
		float Skill = 0.7f;
		FParse::Value(Cmd, TEXT("HWAutoplaySkill="), Skill);
		if (ParseBotKind(Value, Kind)) { Duel->SetAutoplay(true, Kind, Skill); }
	}
	Duel->bDebugDraw = FParse::Param(Cmd, TEXT("HWDebug"));
	FParse::Value(Cmd, TEXT("HWShotAt="), ShotAt);
	FParse::Value(Cmd, TEXT("HWShotEvery="), ShotEvery);
	FParse::Value(Cmd, TEXT("HWExec="), ExecCmds, false);
	FParse::Value(Cmd, TEXT("HWExecAt="), ExecAt);
	FParse::Value(Cmd, TEXT("HWShots="), ShotsWanted);
	FParse::Value(Cmd, TEXT("HWQuitAt="), QuitAt);
	FParse::Value(Cmd, TEXT("HWEncounters="), AutoEncounters);
	if (FParse::Param(Cmd, TEXT("HWAutoStart")) && Session != nullptr)
	{
		Duel->StartEncounter(Session->Tier);
	}
}

void AHWGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PlaySeconds += DeltaSeconds;
	if (!ExecCmds.IsEmpty() && PlaySeconds >= ExecAt)
	{
		TArray<FString> Cmds;
		ExecCmds.TrimQuotes().ParseIntoArray(Cmds, TEXT("|"));
		ExecCmds.Reset();
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			for (const FString& C : Cmds) { PC->ConsoleCommand(C.TrimStartAndEnd()); }
		}
	}
	if (ShotAt >= 0.f && PlaySeconds >= ShotAt && ShotsTaken < FMath::Max(ShotsWanted, 1))
	{
		++ShotsTaken;
		ShotAt = ShotEvery > 0.f ? ShotAt + ShotEvery : -1.f;
		FScreenshotRequest::RequestScreenshot(true);
		UE_LOG(LogHellwalkerRL, Log, TEXT("Screenshot requested at %.1f s."), PlaySeconds);
	}
	if (AutoEncounters > 0)
	{
		UHWDuelSubsystem* Duel = GetWorld()->GetSubsystem<UHWDuelSubsystem>();
		const UHWSessionSubsystem* Session = Duel != nullptr ? Duel->GetSession() : nullptr;
		const bool bOver = Duel != nullptr && (Duel->GetState() == EHWEncounterState::PlayerWon || Duel->GetState() == EHWEncounterState::PlayerLost);
		if (bOver && EncounterOverSeconds < 0.f) { EncounterOverSeconds = PlaySeconds; }
		if (bOver && PlaySeconds - EncounterOverSeconds > 1.5f && Session != nullptr)
		{
			EncounterOverSeconds = -1.f;
			if (Session->EncountersStarted < AutoEncounters) { Duel->ResetEncounter(-1); }
			else if (QuitAt < 0.f) { QuitAt = PlaySeconds; }
		}
	}
	if (QuitAt >= 0.f && PlaySeconds >= QuitAt)
	{
		QuitAt = -1.f;
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			PC->ConsoleCommand(TEXT("quit"));
		}
	}
}
