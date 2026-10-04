// Hellwalker — console commands (PLAN A1 "ResetEncounter() + console exec", A3 "console exec injects a
// press at an exact frame offset").

#include "HellwalkerRL.h"
#include "HWDuelSubsystem.h"
#include "HWSessionSubsystem.h"
#include "HWPlayerController.h"
#include "HWCharacterBase.h"
#include "HWOpenWorldGameMode.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "HWAutomation.h"
#include "HWHUD.h"
#include "HWBuild.h"
#include "HWOpenWorld.h"
#include "EngineUtils.h"
#include "InputAction.h"
#include "Misc/Paths.h"

namespace
{
	UHWDuelSubsystem* DuelOf(UWorld* World)
	{
		return World != nullptr ? World->GetSubsystem<UHWDuelSubsystem>() : nullptr;
	}

	bool ParseKind(const FString& S, HW::EBotKind& Out)
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

	FAutoConsoleCommandWithWorldAndArgs GHWReset(
		TEXT("hw.Reset"),
		TEXT("hw.Reset [seed] - ResetEncounter(): reset and re-seed the duel and the boss brain. The session's playstyle model persists."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UHWDuelSubsystem* D = DuelOf(World)) { D->ResetEncounter(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : -1); }
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWTier(
		TEXT("hw.Tier"),
		TEXT("hw.Tier normal|adaptive - start a new encounter in that mode: Normal (the scripted keeper) or Adaptive AI (the RL keeper); pathbreaker|hellwalker also work."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UHWDuelSubsystem* D = DuelOf(World);
			if (D == nullptr || Args.Num() == 0) { return; }
			D->StartEncounter((Args[0].Equals(TEXT("pathbreaker"), ESearchCase::IgnoreCase) || Args[0].Equals(TEXT("normal"), ESearchCase::IgnoreCase)) ? EHWTier::Pathbreaker : EHWTier::Hellwalker);
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWInjectParry(
		TEXT("hw.InjectParry"),
		TEXT("hw.InjectParry <frames> - press parry exactly <frames> before the next boss swing's impact (A3: 13 fails, 11 succeeds)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UHWDuelSubsystem* D = DuelOf(World)) { D->InjectParry(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 7); }
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWAutoplay(
		TEXT("hw.Autoplay"),
		TEXT("hw.Autoplay off | <masher|turtle|habitual|varied|dodger|rhythm> [skill 0-1] - a simulated player (the B0 instrument) drives Soul."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UHWDuelSubsystem* D = DuelOf(World);
			if (D == nullptr) { return; }
			HW::EBotKind Kind = HW::EBotKind::Varied;
			if (Args.Num() == 0 || !ParseKind(Args[0], Kind)) { D->SetAutoplay(false); return; }
			D->SetAutoplay(true, Kind, Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.7f);
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWDebug(
		TEXT("hw.Debug"),
		TEXT("hw.Debug [0|1] - A2 numeric overlay and hit-volume drawing."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UHWDuelSubsystem* D = DuelOf(World)) { D->bDebugDraw = Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !D->bDebugDraw; }
		}));

	AHWPlayerController* MenuController(UWorld* World)
	{
		return World != nullptr ? Cast<AHWPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GHWMenu(
		TEXT("hw.Menu"),
		TEXT("hw.Menu pause|settings|controls|graphics|audio|accessibility|tutorial|notebook|close - open a menu page (scripted checks)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AHWPlayerController* PC = MenuController(World)) { PC->OpenMenuPage(Args.Num() > 0 ? Args[0] : FString(TEXT("pause"))); }
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWMenuNav(
		TEXT("hw.MenuNav"),
		TEXT("hw.MenuNav up|down|left|right|accept|back|tabnext|tabprev|toggle [repeat] - drive the open menu like a pad (scripted checks)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AHWPlayerController* PC = MenuController(World);
			if (PC == nullptr || Args.Num() == 0) { return; }
			const int32 Repeat = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 50) : 1;
			for (int32 I = 0; I < Repeat; ++I) { PC->MenuCommand(Args[0]); }
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWMap(
		TEXT("hw.Map"),
		TEXT("hw.Map [open|close|track <0|1|2|auto>|zoom <1-4>] - open world: the valley map (no argument toggles it) and the tracked keeper (scripted checks)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AHWPlayerController* PC = MenuController(World)) { PC->MapConsole(Args); }
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWResetModel(
		TEXT("hw.ResetModel"),
		TEXT("hw.ResetModel - forget everything the keepers have learned about you this session."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			(void)Args;
			if (UHWDuelSubsystem* D = DuelOf(World))
			{
				if (UHWSessionSubsystem* S = D->GetSession()) { S->ResetMemory(); }
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWBlind(
		TEXT("hw.Blind"),
		TEXT("hw.Blind [0|1] - B4 blind labels: Variant A / Variant B instead of tier names."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UHWDuelSubsystem* D = DuelOf(World))
			{
				if (UHWSessionSubsystem* S = D->GetSession()) { S->bBlind = Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !S->bBlind; }
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWStrafeTest(
		TEXT("hw.StrafeTest"),
		TEXT("hw.StrafeTest [seconds] [toleranceDeg] - A0: strafe around the locked-on boss; the CHARACTER must keep facing it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AHWPlayerController* PC = World != nullptr ? Cast<AHWPlayerController>(World->GetFirstPlayerController()) : nullptr;
			if (PC != nullptr)
			{
				PC->StartStrafeTest(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 16.f, Args.Num() > 1 ? FCString::Atof(*Args[1]) : 10.f);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWBlade(
		TEXT("hw.Blade"),
		TEXT("hw.Blade <0 right twin|1 left twin|2 glaive> <pitch> <yaw> <roll> [x y z] - C2: seat the player's hand weapons in the fists."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UHWDuelSubsystem* D = DuelOf(World);
			AHWCharacterBase* P = D != nullptr ? D->GetFighterActor(HW::ESide::Player) : nullptr;
			if (P == nullptr || Args.Num() < 4) { return; }
			auto Arg = [&Args](int32 I) { return Args.IsValidIndex(I) ? FCString::Atof(*Args[I]) : 0.f; };
			const FRotator R(Arg(1), Arg(2), Arg(3));
			const FVector L(Arg(4), Arg(5), Arg(6));
			P->SetHandWeaponOffset(FCString::Atoi(*Args[0]), FTransform(R, L));
			UE_LOG(LogHellwalkerRL, Display, TEXT("hw.Blade %s: rot %s loc %s"), *Args[0], *R.ToString(), *L.ToString());
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWPhoto(
		TEXT("hw.Photo"),
		TEXT("hw.Photo [player|boss|off] [yaw] [distance] [height] - C2: a still camera on one fighter (yaw 0 = facing it)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UHWDuelSubsystem* D = DuelOf(World);
			APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
			if (D == nullptr || PC == nullptr) { return; }
			if (Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				PC->SetViewTarget(PC->GetPawn());
				return;
			}
			const bool bBoss = Args.Num() > 0 && Args[0].Equals(TEXT("boss"), ESearchCase::IgnoreCase);
			AActor* Subject = D->GetFighterActor(bBoss ? HW::ESide::Boss : HW::ESide::Player);
			if (Subject == nullptr && !bBoss) { Subject = PC->GetPawn(); } // the open world's explorer
			if (Subject == nullptr) { return; }
			const float Yaw = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 30.f;
			const float Dist = Args.Num() > 2 ? FCString::Atof(*Args[2]) : (bBoss ? 520.f : 330.f);
			const float Height = Args.Num() > 3 ? FCString::Atof(*Args[3]) : (bBoss ? 60.f : 20.f);
			const FVector Target = Subject->GetActorLocation() + FVector(0.f, 0.f, Height);
			const FRotator Around(0.f, Subject->GetActorRotation().Yaw + Yaw, 0.f);
			const FVector Eye = Target + Around.Vector() * Dist + FVector(0.f, 0.f, 30.f);
			ACameraActor* Cam = World->SpawnActor<ACameraActor>(Eye, (Target - Eye).Rotation());
			if (Cam != nullptr)
			{
				Cam->AttachToActor(Subject, FAttachmentTransformRules::KeepWorldTransform);
				PC->SetViewTarget(Cam);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWKill(
		TEXT("hw.Kill"),
		TEXT("hw.Kill boss|player - end the running duel (flow tests)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UHWDuelSubsystem* D = DuelOf(World))
			{
				D->DebugKill(Args.Num() > 0 && Args[0].Equals(TEXT("player"), ESearchCase::IgnoreCase) ? HW::ESide::Player : HW::ESide::Boss);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWDuel(
		TEXT("hw.Duel"),
		TEXT("hw.Duel <shrine 0|1|2> - open world: challenge that shrine's keeper now."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AHWOpenWorldGameMode* GM = World != nullptr ? World->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr)
			{
				if (GM->GetPhase() == EHWWorldPhase::Exploring) { GM->BeginDuel(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0); }
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWGoto(
		TEXT("hw.Goto"),
		TEXT("hw.Goto <site id> - open world: put the explorer just outside a site, facing it (Ruin_Watch, Bell_Crossroads, ...)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AHWOpenWorldGameMode* GM = World != nullptr ? World->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr;
			if (GM != nullptr && Args.Num() > 0) { GM->TeleportExplorer(FName(*Args[0])); }
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWInteract(
		TEXT("hw.Interact"),
		TEXT("hw.Interact - as E while exploring: ring the bell / challenge the keeper in reach."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			(void)Args;
			if (AHWOpenWorldGameMode* GM = World != nullptr ? World->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr) { GM->Interact(); }
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWGotoBlock(
		TEXT("hw.GotoBlock"),
		TEXT("hw.GotoBlock <n> - put the explorer 2.5 m in front of traversal block n (six per ruin: vault, mantle, platform, step, vault, climb), facing it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
			APawn* P = PC != nullptr ? PC->GetPawn() : nullptr;
			AHWOpenWorld* OW = nullptr;
			for (TActorIterator<AHWOpenWorld> It(World); It; ++It) { OW = *It; break; }
			const int32 N = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
			if (P == nullptr || OW == nullptr || !OW->GetTraversalBlocks().IsValidIndex(N) || OW->GetTraversalBlocks()[N] == nullptr) { return; }
			const AActor* B = OW->GetTraversalBlocks()[N];
			FVector Origin;
			FVector Extent;
			B->GetActorBounds(false, Origin, Extent);
			// Approach across the block's short side (its local Y when it is a wall).
			const FVector Across = B->GetActorScale3D().Y <= B->GetActorScale3D().X ? B->GetActorRightVector() : B->GetActorForwardVector();
			const double Half = FMath::Min(B->GetActorScale3D().X, B->GetActorScale3D().Y) * 50.0;
			const FVector Stand = FVector(Origin.X, Origin.Y, Origin.Z - Extent.Z) - Across * (Half + 250.0) + FVector(0.0, 0.0, 100.0);
			const FRotator Face = Across.Rotation();
			HWBuild::TeleportPawn(P, Stand, static_cast<float>(Face.Yaw));
			PC->SetControlRotation(FRotator(-10.f, Face.Yaw, 0.f));
			UE_LOG(LogHellwalkerRL, Display, TEXT("GotoBlock %d: %.0f cm tall, standing %.0f cm off it. Actor scale %s"), N, Extent.Z * 2.0, 250.0, *B->GetActorScale3D().ToString());
			TArray<USceneComponent*> Comps;
			B->GetComponents(Comps);
			for (const USceneComponent* C : Comps)
			{
				const UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(C);
				UE_LOG(LogHellwalkerRL, Display, TEXT("  %s (%s) parent=%s world=%s at %s collision=%s/%d pawn=%d"), *C->GetName(), *C->GetClass()->GetName(),
					*GetNameSafe(C->GetAttachParent()), *C->GetComponentScale().ToString(), *C->GetComponentLocation().ToString(),
					Prim != nullptr ? *Prim->GetCollisionProfileName().ToString() : TEXT("-"), Prim != nullptr ? static_cast<int32>(Prim->GetCollisionEnabled()) : -1,
					Prim != nullptr ? static_cast<int32>(Prim->GetCollisionResponseToChannel(ECC_Pawn)) : -1);
			}
			if (const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(P->GetRootComponent()))
			{
				UE_LOG(LogHellwalkerRL, Display, TEXT("  explorer root %s profile=%s objtype=%d block-worlddynamic=%d"), *Root->GetName(), *Root->GetCollisionProfileName().ToString(),
					static_cast<int32>(Root->GetCollisionObjectType()), static_cast<int32>(Root->GetCollisionResponseToChannel(ECC_WorldDynamic)));
			}
			for (TArray<USceneComponent*>::TIterator It = Comps.CreateIterator(); false;)
			{
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWHelp(
		TEXT("hw.Help"),
		TEXT("hw.Help - toggle the controls panel (F1)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			(void)Args;
			const APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
			if (AHWHUD* H = PC != nullptr ? Cast<AHWHUD>(PC->GetHUD()) : nullptr) { H->ToggleHelp(); }
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWHold(
		TEXT("hw.Hold"),
		TEXT("hw.Hold <IA_Name> <seconds> [x y] - press an input action as the player (e.g. hw.Hold IA_Move 3 0 1, hw.Hold IA_Crouch 0.1)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1) { return; }
			const FString Name = Args[0];
			UInputAction* Action = LoadObject<UInputAction>(nullptr, *FString::Printf(TEXT("/Game/Input/%s.%s"), *Name, *Name));
			if (Action == nullptr) { UE_LOG(LogHellwalkerRL, Warning, TEXT("hw.Hold: no /Game/Input/%s"), *Name); return; }
			const float Seconds = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.1f;
			const bool bAxis = Args.Num() > 3;
			const FVector2D Axis = bAxis ? FVector2D(FCString::Atof(*Args[2]), FCString::Atof(*Args[3])) : FVector2D::ZeroVector;
			FHWAutomation::Hold(Action, Seconds, bAxis, Axis);
			if (UHWDuelSubsystem* D = DuelOf(World)) { D->MarkNotResearch(TEXT("hw.Hold pressed input")); }
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWWhere(
		TEXT("hw.Where"),
		TEXT("hw.Where - log the player pawn's position and speed."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			(void)Args;
			const APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
			const APawn* P = PC != nullptr ? PC->GetPawn() : nullptr;
			if (P == nullptr) { return; }
			UE_LOG(LogHellwalkerRL, Display, TEXT("Where: %s at (%.0f, %.0f, %.0f), speed %.0f cm/s (vertical %.0f), facing yaw %.0f, camera yaw %.0f"), *P->GetClass()->GetName(),
				P->GetActorLocation().X, P->GetActorLocation().Y, P->GetActorLocation().Z, P->GetVelocity().Size2D(), P->GetVelocity().Z, P->GetActorRotation().Yaw,
				PC->GetControlRotation().Yaw);
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWControls(
		TEXT("hw.Controls"),
		TEXT("hw.Controls - write every live key binding (ours + the explorer's) to Saved/Hellwalker/Controls.txt."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			(void)Args;
			const AHWPlayerController* PC = World != nullptr ? Cast<AHWPlayerController>(World->GetFirstPlayerController()) : nullptr;
			if (PC == nullptr) { return; }
			const FString Path = FPaths::ProjectSavedDir() / TEXT("HellwalkerRL") / TEXT("Controls.txt");
			FFileHelper::SaveStringToFile(PC->DescribeControls(), *Path);
			UE_LOG(LogHellwalkerRL, Display, TEXT("Controls -> %s"), *Path);
		}));

	FAutoConsoleCommandWithWorldAndArgs GHWShot(
		TEXT("hw.Shot"),
		TEXT("hw.Shot - screenshot with HUD (Saved/Screenshots)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			(void)Args;
			(void)World;
			FScreenshotRequest::RequestScreenshot(true);
		}));
}
