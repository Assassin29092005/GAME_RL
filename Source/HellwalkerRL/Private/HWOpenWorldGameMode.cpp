#include "HWOpenWorldGameMode.h"

#include "HellwalkerRL.h"
#include "HWBossCharacter.h"
#include "HWCharacterBase.h"
#include "HWDuelSubsystem.h"
#include "HWHUD.h"
#include "HWOpenWorld.h"
#include "HWPlayerCharacter.h"
#include "HWPlayerController.h"
#include "HWSaveGame.h"
#include "HWSessionSubsystem.h"
#include "HWSites.h"
#include "HWCore/HWSim.h"
#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "HWBuild.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	// The explorer: the Game Animation Sample's most complete character first. The Mover-based ones add the slide
	// (crouch while running) and, with _Ragdoll, the ragdoll and its get-ups; all of them run, sprint, crouch and
	// traverse (hurdle, vault, mantle, climb). -HWExplorer=ragdoll|mover|cmc picks one.
	struct FExplorerChoice { const TCHAR* Key; const TCHAR* Path; };
	const FExplorerChoice ExplorerChoices[] = {
		{ TEXT("ragdoll"), TEXT("/Game/Blueprints/SandboxCharacter_Mover_Ragdoll.SandboxCharacter_Mover_Ragdoll_C") },
		{ TEXT("mover"), TEXT("/Game/Blueprints/SandboxCharacter_Mover.SandboxCharacter_Mover_C") },
		{ TEXT("cmc"), TEXT("/Game/Blueprints/SandboxCharacter_CMC.SandboxCharacter_CMC_C") },
	};

	/** The pawn's body mesh: ACharacter's Mesh, else the Mover pawns' "SkeletalMesh", else the first one found. */
	USkeletalMeshComponent* BodyMeshOf(APawn* Pawn)
	{
		if (ACharacter* Char = Cast<ACharacter>(Pawn)) { return Char->GetMesh(); }
		TArray<USkeletalMeshComponent*> Meshes;
		Pawn->GetComponents(Meshes);
		for (USkeletalMeshComponent* M : Meshes) { if (M->GetFName() == TEXT("SkeletalMesh")) { return M; } }
		return Meshes.Num() > 0 ? Meshes[0] : nullptr;
	}
	constexpr float BellReach = 520.f;
	constexpr float GateReach = 650.f;
	constexpr float DuelOverSeconds = 4.5f;

	bool ParseBot(const FString& S, HW::EBotKind& Out)
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

	FString ModeName(EHWPlayMode M)
	{
		switch (M)
		{
		case EHWPlayMode::Pathbreaker: return TEXT("Pathbreaker");
		case EHWPlayMode::SixtySixDays: return TEXT("66 Days");
		default: return TEXT("Hellwalker");
		}
	}
}

AHWOpenWorldGameMode::AHWOpenWorldGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AHWPlayerController::StaticClass();
	HUDClass = AHWHUD::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

void AHWOpenWorldGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("HWWorldSeed="), WorldSeed);
	bNoSave = FParse::Param(Cmd, TEXT("HWNoSave"));
	bSpawnAtBell = FParse::Param(Cmd, TEXT("HWSpawnAtBell"));
	FString Want;
	FParse::Value(Cmd, TEXT("HWExplorer="), Want);
	for (const FExplorerChoice& Choice : ExplorerChoices)
	{
		if (!Want.IsEmpty() && !Want.Equals(Choice.Key, ESearchCase::IgnoreCase)) { continue; }
		ExplorerClass = LoadClass<APawn>(nullptr, Choice.Path);
		if (ExplorerClass != nullptr) { break; }
	}
	if (ExplorerClass == nullptr)
	{
		UE_LOG(LogHellwalkerRL, Warning, TEXT("Game Animation Sample character not found — exploring as Soul instead."));
		ExplorerClass = AHWPlayerCharacter::StaticClass();
	}
	UE_LOG(LogHellwalkerRL, Log, TEXT("Explorer: %s."), *GetNameSafe(ExplorerClass));
	Automation.Init();
	if (FParse::Param(Cmd, TEXT("HWMeshSurvey"))) { HWBuild::SurveyMeshes(); }
}

void AHWOpenWorldGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// No pawn at the title: the title camera looks over the valley until a game begins.
	if (TitleCamera != nullptr && NewPlayer != nullptr) { NewPlayer->SetViewTarget(TitleCamera); }
}

APlayerController* AHWOpenWorldGameMode::PC() const
{
	return GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
}

void AHWOpenWorldGameMode::StartPlay()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	OpenWorld = World->SpawnActor<AHWOpenWorld>(FVector::ZeroVector, FRotator::ZeroRotator, P);
	OpenWorld->Build(WorldSeed);
	const FHWWorldGen& G = OpenWorld->Gen();

	// Sites.
	const TArray<FHWSite>& Sites = G.GetSites();
	for (int32 I = 0; I < Sites.Num(); ++I)
	{
		const FHWSite& S = Sites[I];
		if (S.Kind == EHWSiteKind::Bell)
		{
			AHWBell* B = World->SpawnActor<AHWBell>(FVector::ZeroVector, FRotator::ZeroRotator, P);
			B->Setup(S);
			Bells.SetNum(FMath::Max(Bells.Num(), S.Index + 1));
			Bells[S.Index] = B;
		}
		else if (S.Kind == EHWSiteKind::Shrine)
		{
			// The gate faces the site the path comes from.
			FVector2D GateDir(1.0, 0.0);
			for (const FIntPoint& L : G.GetLinks())
			{
				if (L.X == I || L.Y == I)
				{
					GateDir = (Sites[L.X == I ? L.Y : L.X].Pos - S.Pos).GetSafeNormal();
					break;
				}
			}
			AHWShrine* Sh = World->SpawnActor<AHWShrine>(FVector::ZeroVector, FRotator::ZeroRotator, P);
			Sh->Setup(S, FHWWorldGen::Shrines()[S.Index], GateDir);
			Shrines.SetNum(FMath::Max(Shrines.Num(), S.Index + 1));
			Shrines[S.Index] = Sh;
		}
	}

	// The title camera: over the first bell, looking up the valley toward the final shrine's mountains.
	if (const FHWSite* First = G.FindSite(TEXT("Bell_AshenGate")))
	{
		const FVector From = FVector(First->Pos.X, First->Pos.Y, First->Height) + FVector(-2600.f, -2200.f, 1800.f);
		const FVector To = FVector(0.f, 0.f, 0.f);
		TitleCamera = World->SpawnActor<ACameraActor>(From, (To - From).Rotation(), P);
	}

	if (UHWDuelSubsystem* Duel = World->GetSubsystem<UHWDuelSubsystem>())
	{
		Duel->bOpenWorld = true;
		Duel->OnEncounterEnded.AddUObject(this, &AHWOpenWorldGameMode::OnEncounterEnded);
		FString Value;
		const TCHAR* Cmd = FCommandLine::Get();
		if (FParse::Value(Cmd, TEXT("HWAutoplay="), Value))
		{
			HW::EBotKind Kind = HW::EBotKind::Varied;
			float Skill = 0.7f;
			FParse::Value(Cmd, TEXT("HWAutoplaySkill="), Skill);
			if (ParseBot(Value, Kind)) { Duel->SetAutoplay(true, Kind, Skill); }
		}
		Duel->bDebugDraw = FParse::Param(Cmd, TEXT("HWDebug"));
	}

	bSaveExists = UHWSaveGame::LoadOrNull() != nullptr;
	Super::StartPlay();

	// ---- command line: skip the title
	const TCHAR* Cmd = FCommandLine::Get();
	FString Value;
	if (FParse::Param(Cmd, TEXT("HWContinue")))
	{
		ContinueGame();
	}
	else if (FParse::Value(Cmd, TEXT("HWWorldMode="), Value))
	{
		const FString L = Value.ToLower();
		NewGame(L == TEXT("pathbreaker") ? EHWPlayMode::Pathbreaker : (L.StartsWith(TEXT("66")) ? EHWPlayMode::SixtySixDays : EHWPlayMode::Hellwalker));
	}
	if (FParse::Value(Cmd, TEXT("HWGoto="), Value)) { TeleportExplorer(FName(*Value)); }
	if (FParse::Param(Cmd, TEXT("HWTour")) && OpenWorld != nullptr)
	{
		// Along the path network: the first bell, the western watch, the ridge... and back through the ruins.
		const FHWWorldGen& Gw = OpenWorld->Gen();
		for (const TCHAR* Id : { TEXT("Ruin_Wall"), TEXT("Bell_Crossroads"), TEXT("Ruin_Court"), TEXT("Bell_Summit"), TEXT("Ruin_Stair"), TEXT("Ruin_Steps") })
		{
			const FHWSite* S = Gw.FindSite(Id);
			if (S != nullptr) { TourRoute.Add(static_cast<int32>(S - Gw.GetSites().GetData())); }
		}
		TourJumpAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/IA_Jump.IA_Jump"));
		TourMoveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/IA_Move.IA_Move"));
		bTour = TourRoute.Num() > 0;
	}
	int32 DuelIndex = -1;
	if (FParse::Value(Cmd, TEXT("HWDuel="), DuelIndex))
	{
		PendingDuel = DuelIndex;
		PendingDuelAt = 2.f;
	}
}

void AHWOpenWorldGameMode::SetPhase(EHWWorldPhase NewPhase)
{
	if (NewPhase != Phase)
	{
		UE_LOG(LogHellwalkerRL, Log, TEXT("World phase: %s (shrines cleared %d/%d)."), *UEnum::GetValueAsString(NewPhase),
			Shrines.FilterByPredicate([](const AHWShrine* S) { return S != nullptr && S->bCleared; }).Num(), Shrines.Num());
	}
	Phase = NewPhase;
	PhaseSeconds = 0.f;
}

void AHWOpenWorldGameMode::Banner(const FString& Title, const FString& Sub, float Seconds)
{
	BannerTitle = Title;
	BannerSub = Sub;
	BannerLeft = Seconds;
	BannerTotal = Seconds;
}

bool AHWOpenWorldGameMode::GetBanner(FString& OutTitle, FString& OutSub, float& OutAlpha) const
{
	if (BannerLeft <= 0.f) { return false; }
	OutTitle = BannerTitle;
	OutSub = BannerSub;
	const float T = BannerTotal - BannerLeft;
	OutAlpha = FMath::Min(FMath::Clamp(T / 0.6f, 0.f, 1.f), FMath::Clamp(BannerLeft / 1.2f, 0.f, 1.f));
	return true;
}

void AHWOpenWorldGameMode::WriteSave()
{
	if (Save != nullptr && !bNoSave) { Save->Write(); }
}

bool AHWOpenWorldGameMode::AllOthersCleared(int32 ShrineIndex) const
{
	for (int32 I = 0; I < Shrines.Num(); ++I)
	{
		if (I != ShrineIndex && (Save == nullptr || !Save->IsShrineCleared(I))) { return false; }
	}
	return true;
}

void AHWOpenWorldGameMode::RefreshSites()
{
	for (int32 I = 0; I < Bells.Num(); ++I)
	{
		if (Bells[I] != nullptr) { Bells[I]->SetLit(Save != nullptr && Save->IsBellLit(I)); }
	}
	for (int32 I = 0; I < Shrines.Num(); ++I)
	{
		if (Shrines[I] == nullptr) { continue; }
		const bool bCleared = Save != nullptr && Save->IsShrineCleared(I);
		Shrines[I]->SetState(bCleared, Shrines[I]->Spec.bFinal && !AllOthersCleared(I));
	}
}

APawn* AHWOpenWorldGameMode::SpawnExplorer(const FVector& Where, float Yaw)
{
	APlayerController* Controller = PC();
	if (Controller == nullptr || OpenWorld == nullptr) { return nullptr; }
	RetireExplorer();
	const FVector Ground = OpenWorld->GroundAt(FVector2D(Where.X, Where.Y));
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* Pawn = GetWorld()->SpawnActor<APawn>(ExplorerClass, FVector(Where.X, Where.Y, FMath::Max(Where.Z, Ground.Z) + 110.f), FRotator(0.f, Yaw, 0.f), P);
	if (Pawn == nullptr)
	{
		UE_LOG(LogHellwalkerRL, Error, TEXT("Could not spawn the explorer (%s)."), *GetNameSafe(ExplorerClass));
		return nullptr;
	}
	Controller->Possess(Pawn);
	Controller->SetControlRotation(FRotator(-12.f, Yaw, 0.f));
	DressExplorerAsSoul(Pawn);
	return Pawn;
}

namespace
{
	/**
	 * The twin blades, sheathed and crossed on the back (hilts over the shoulders) — placed in world terms (actor
	 * axes), then attached to the spine. Dark lacquer scabbards: the pale blades are for the duel.
	 */
	void AttachBackBlades(USkeletalMeshComponent* Look)
	{
		AActor* Char = Look->GetOwner();
		if (Char == nullptr) { return; }
		const FVector F = Char->GetActorForwardVector();
		const FVector R = Char->GetActorRightVector();
		const FVector U = FVector::UpVector;
		const FVector Spine = Look->DoesSocketExist(TEXT("spine_05")) ? Look->GetSocketLocation(TEXT("spine_05")) : Char->GetActorLocation() + U * 50.f;
		auto Piece = [&](UStaticMesh* Mesh, const FVector& At, const FVector& Along, const FVector& Scale, const FLinearColor& Color)
		{
			UStaticMeshComponent* C = HWBuild::Part(Char, Look, Mesh, FVector::ZeroVector, FVector(1.f), FRotator::ZeroRotator, Color, false, 0.35f);
			C->SetWorldLocationAndRotation(At, FRotationMatrix::MakeFromZX(Along, F).Rotator());
			C->SetWorldScale3D(Scale);
			C->AttachToComponent(Look, FAttachmentTransformRules::KeepWorldTransform, TEXT("spine_05"));
		};
		UStaticMesh* Sword = HWBuild::OptionalMesh(TEXT("/Game/Sword/Sword/SM_Sword.SM_Sword"));
		for (int32 I = 0; I < 2; ++I)
		{
			const FVector Along = (U + R * (I == 0 ? 0.32f : -0.32f)).GetSafeNormal();
			const FVector Mid = Spine - F * (15.f + 2.f * I) - U * 14.f;
			if (Sword != nullptr)
			{
				// Pommel over the shoulder, blade down across the back, flat against it.
				const float Length = 95.f;
				const float S = Length / FMath::Max(static_cast<float>(Sword->GetBoundingBox().GetSize().Z), 1.f);
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Char);
				C->SetStaticMesh(Sword);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetupAttachment(Look);
				C->RegisterComponent();
				C->SetWorldLocationAndRotation(Mid + Along * (Length * 0.5f), FRotationMatrix::MakeFromZX(-Along, R).Rotator());
				C->SetWorldScale3D(FVector(S));
				C->AttachToComponent(Look, FAttachmentTransformRules::KeepWorldTransform, TEXT("spine_05"));
				continue;
			}
			Piece(HWBuild::Cylinder(), Mid, Along, FVector(0.035f, 0.05f, 0.64f), FLinearColor(0.025f, 0.02f, 0.025f));                   // scabbard
			Piece(HWBuild::Cube(), Mid + Along * 32.f, Along, FVector(0.08f, 0.1f, 0.015f), FLinearColor(0.3f, 0.27f, 0.22f));              // guard
			Piece(HWBuild::Cylinder(), Mid + Along * 42.f, Along, FVector(0.028f, 0.028f, 0.2f), FLinearColor(0.09f, 0.015f, 0.015f));      // hilt
		}
	}
}

void AHWOpenWorldGameMode::DressExplorerAsSoul(APawn* Pawn)
{
	APawn* Char = Pawn;
	if (Char == nullptr || Char->IsA<AHWCharacterBase>()) { return; }
	USkeletalMesh* Manny = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Fighter_Animations/Demo/Mannequins/Meshes/SKM_Manny.SKM_Manny"));
	UClass* Retarget = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Blueprints/RetargetedCharacters/ABP_GenericRetarget.ABP_GenericRetarget_C"));
	USkeletalMeshComponent* Base = BodyMeshOf(Char);
	if (Manny == nullptr || Retarget == nullptr || Base == nullptr) { return; }

	// The sample's own recipe (BP_Manny): a mesh on the character's mesh, posed by ABP_GenericRetarget from its
	// attach parent. The mannequin underneath keeps animating (motion matching) but is not drawn.
	USkeletalMeshComponent* Look = NewObject<USkeletalMeshComponent>(Char, TEXT("SoulLook"));
	Look->SetSkeletalMeshAsset(Manny);
	Look->ComponentTags.Add(TEXT("RTG_UEFN_to_UE5_Mannequin")); // ABP_GenericRetarget picks its retargeter from the first tag
	Look->SetupAttachment(Base);
	Look->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Look->SetAnimInstanceClass(Retarget);
	Look->RegisterComponent();
	Look->PrimaryComponentTick.AddPrerequisite(Base, Base->PrimaryComponentTick);
	Base->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Base->SetVisibility(false);
	ExplorerBody = Base;
	ExplorerLook = Look;
	HWBuild::MeshEffect(HWFX::TeleportIn, Look, FLinearColor(0.35f, 0.6f, 1.f)); // Soul wakes

	// The blades go on once the retargeted pose is live: fitted to a spine still in its reference pose, they tilt.
	FTimerHandle Blades;
	GetWorldTimerManager().SetTimer(Blades, FTimerDelegate::CreateWeakLambda(Look, [Look]() { AttachBackBlades(Look); }), 0.3f, false);
}

void AHWOpenWorldGameMode::RetireExplorer()
{
	APlayerController* Controller = PC();
	if (Controller == nullptr) { return; }
	APawn* Old = Controller->GetPawn();
	if (Old != nullptr && Old != Soul)
	{
		Controller->UnPossess();
		Old->Destroy();
	}
}

void AHWOpenWorldGameMode::NewGame(EHWPlayMode Mode)
{
	UHWSaveGame::Erase();
	Save = UHWSaveGame::NewGame(Mode);
	if (UHWSessionSubsystem* S = GetGameInstance()->GetSubsystem<UHWSessionSubsystem>()) { S->ResetMemory(); }
	WriteSave();
	bSaveExists = true;
	RefreshSites();
	SetPhase(EHWWorldPhase::Exploring);
	SpawnAtStart(Mode == EHWPlayMode::SixtySixDays ? TEXT("66 days remain") : ModeName(Mode));
	UE_LOG(LogHellwalkerRL, Log, TEXT("New game: %s."), *ModeName(Mode));
}

bool AHWOpenWorldGameMode::ContinueGame()
{
	UHWSaveGame* Loaded = UHWSaveGame::LoadOrNull();
	if (Loaded == nullptr) { return false; }
	Save = Loaded;
	RefreshSites();
	SetPhase(Save->bFinished ? EHWWorldPhase::Ending : EHWWorldPhase::Exploring);
	if (Phase == EHWWorldPhase::Exploring)
	{
		SpawnAtStart(Save->Mode == EHWPlayMode::SixtySixDays ? FString::Printf(TEXT("%d days remain"), Save->DaysLeft) : ModeName(Save->Mode));
	}
	return true;
}

void AHWOpenWorldGameMode::TeleportExplorer(FName SiteId)
{
	if (OpenWorld == nullptr) { return; }
	const FHWSite* S = OpenWorld->Gen().FindSite(SiteId);
	APlayerController* Controller = PC();
	if (S == nullptr || Controller == nullptr || Controller->GetPawn() == nullptr || Phase != EHWWorldPhase::Exploring) { return; }
	const FVector2D Off = S->Pos.GetSafeNormal() * -(S->Radius + 900.0);
	const FVector At = OpenWorld->GroundAt(S->Pos + Off) + FVector(0.f, 0.f, 120.f);
	const FRotator Face = (FVector(S->Pos.X, S->Pos.Y, At.Z) - At).Rotation();
	HWBuild::TeleportPawn(Controller->GetPawn(), At, static_cast<float>(Face.Yaw));
	Controller->SetControlRotation(FRotator(-12.f, Face.Yaw, 0.f));
}

void AHWOpenWorldGameMode::Interact()
{
	if (Phase != EHWWorldPhase::Exploring || Save == nullptr) { return; }
	APlayerController* Controller = PC();
	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Pawn == nullptr) { return; }
	const FVector At = Pawn->GetActorLocation();

	for (AHWBell* B : Bells)
	{
		if (B != nullptr && FVector::Dist2D(At, B->InteractPoint()) < BellReach)
		{
			B->Ring();
			Save->BellsLit |= (1 << B->BellIndex);
			Save->CheckpointBell = B->BellIndex;
			WriteSave();
			Banner(B->Title, TEXT("You will wake here"), 3.5f);
			return;
		}
	}
	for (AHWShrine* S : Shrines)
	{
		if (S == nullptr || FVector::Dist2D(At, S->GatePoint()) > GateReach) { continue; }
		if (S->bCleared) { Banner(S->Spec.Title, TEXT("The seal is broken. Nothing waits here now."), 3.f); return; }
		if (S->bSealed) { Banner(TEXT("SEALED"), TEXT("Break the other seals first."), 3.f); return; }
		BeginDuel(S->ShrineIndex);
		return;
	}
}

void AHWOpenWorldGameMode::BeginDuel(int32 ShrineIndex)
{
	if (!Shrines.IsValidIndex(ShrineIndex) || Shrines[ShrineIndex] == nullptr || Save == nullptr) { return; }
	UHWDuelSubsystem* Duel = GetWorld()->GetSubsystem<UHWDuelSubsystem>();
	APlayerController* Controller = PC();
	if (Duel == nullptr || Controller == nullptr) { return; }
	AHWShrine* Shrine = Shrines[ShrineIndex];
	const FHWShrineSpec& Spec = Shrine->Spec;

	RetireExplorer();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const float Yaw = Shrine->ArenaYaw();
	Soul = GetWorld()->SpawnActor<AHWPlayerCharacter>(Shrine->PlayerSpawn(), FRotator(0.f, Yaw, 0.f), P);
	const FTransform BossAt(FRotator(0.f, Yaw + 180.f, 0.f), Shrine->BossSpawn());
	Boss = GetWorld()->SpawnActorDeferred<AHWBossCharacter>(AHWBossCharacter::StaticClass(), BossAt, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Soul == nullptr || Boss == nullptr) { return; }
	Boss->CastOverride = Spec.Cast;
	UGameplayStatics::FinishSpawningActor(Boss, BossAt);

	// The final shrine always reads you — that is the point of it. Pathbreaker's others keep their script.
	// Register the duel BEFORE possessing: the controller picks combat input and lock-on from it.
	const EHWTier Tier = (Save->Mode == EHWPlayMode::Pathbreaker && !Spec.bFinal) ? EHWTier::Pathbreaker : EHWTier::Hellwalker;
	Duel->BossTitle = Spec.Title;
	Duel->ConfigureBoss(Spec.Script, Spec.HealthScale);
	Duel->SetArenaSpawns(Shrine->PlayerSpawn(), Shrine->BossSpawn(), Yaw);
	Duel->RegisterFighters(Soul, Boss);
	Duel->StartEncounter(Tier);
	Controller->Possess(Soul);
	Controller->SetControlRotation(FRotator(-12.f, Yaw, 0.f));
	HWBuild::MeshEffect(HWFX::TeleportIn, Soul->GetDrawnMesh(), FLinearColor(0.35f, 0.6f, 1.f));
	HWBuild::MeshEffect(HWFX::TeleportIn, Boss->GetDrawnMesh(), FLinearColor(1.f, 0.25f, 0.08f));
	ActiveShrine = ShrineIndex;
	SetPhase(EHWWorldPhase::Duel);
	Prompt.Reset();
	Banner(Spec.Title.ToUpper(), Spec.Epithet, 3.5f);
	UE_LOG(LogHellwalkerRL, Log, TEXT("Duel at %s: %s (%s)."), *Spec.Id.ToString(), *Spec.Title, Tier == EHWTier::Hellwalker ? TEXT("reads you") : TEXT("scripted"));
}

void AHWOpenWorldGameMode::OnEncounterEnded(bool bPlayerWon)
{
	if (Phase != EHWWorldPhase::Duel) { return; }
	bLastWon = bPlayerWon;
	// The loser comes apart: the keeper into embers, or Soul into ash.
	AHWCharacterBase* Fallen = bPlayerWon ? static_cast<AHWCharacterBase*>(Boss.Get()) : static_cast<AHWCharacterBase*>(Soul.Get());
	if (Fallen != nullptr)
	{
		HWBuild::MeshEffect(HWFX::MeshBurst, Fallen->GetDrawnMesh(), bPlayerWon ? FLinearColor(1.f, 0.45f, 0.1f) : FLinearColor(0.4f, 0.45f, 0.6f));
	}
	SetPhase(EHWWorldPhase::DuelOver);
}

void AHWOpenWorldGameMode::FinishDuel()
{
	UHWDuelSubsystem* Duel = GetWorld()->GetSubsystem<UHWDuelSubsystem>();
	APlayerController* Controller = PC();
	AHWShrine* Shrine = Shrines.IsValidIndex(ActiveShrine) ? Shrines[ActiveShrine].Get() : nullptr;
	if (Duel != nullptr) { Duel->ClearFighters(); }
	if (Controller != nullptr) { Controller->UnPossess(); }
	if (Soul != nullptr) { Soul->Destroy(); Soul = nullptr; }
	if (Boss != nullptr) { Boss->Destroy(); Boss = nullptr; }

	if (bLastWon && Shrine != nullptr)
	{
		Save->ShrinesCleared |= (1 << ActiveShrine);
		RefreshSites();
		bool bAll = true;
		for (int32 I = 0; I < Shrines.Num(); ++I) { bAll = bAll && Save->IsShrineCleared(I); }
		if (bAll)
		{
			Save->bFinished = true;
			WriteSave();
			SetPhase(EHWWorldPhase::Ending);
			if (TitleCamera != nullptr && Controller != nullptr) { Controller->SetViewTarget(TitleCamera); }
			return;
		}
		WriteSave();
		SetPhase(EHWWorldPhase::Exploring);
		const bool bFinalOpens = Shrines.ContainsByPredicate([](const AHWShrine* S) { return S != nullptr && S->Spec.bFinal && !S->bSealed && !S->bCleared; });
		if (bSpawnAtBell)
		{
			SpawnExplorer(Shrine->ExitPoint(), static_cast<float>((-Shrine->Inward()).Rotation().Yaw));
			Banner(TEXT("SEAL BROKEN"), bFinalOpens ? TEXT("Something stirs at the Hell Gate.") : Shrine->Spec.Title, 4.5f);
		}
		else
		{
			SpawnAtStart(bFinalOpens ? TEXT("Seal broken. Something stirs at the Hell Gate.") : TEXT("Seal broken. The next keeper waits."));
		}
		return;
	}

	// Death: you wake at your bell. In 66 Days, a day passes; when the last one does, it is over.
	++Save->Deaths;
	if (Save->Mode == EHWPlayMode::SixtySixDays)
	{
		--Save->DaysLeft;
		if (Save->DaysLeft <= 0)
		{
			if (!bNoSave) { UHWSaveGame::Erase(); }
			bSaveExists = false;
			SetPhase(EHWWorldPhase::Regret);
			if (TitleCamera != nullptr && Controller != nullptr) { Controller->SetViewTarget(TitleCamera); }
			return;
		}
	}
	WriteSave();
	SetPhase(EHWWorldPhase::Exploring);
	SpawnAtStart(Save->Mode == EHWPlayMode::SixtySixDays ? FString::Printf(TEXT("A day has passed. %d remain."), Save->DaysLeft)
		: TEXT("You wake again."));
}

int32 AHWOpenWorldGameMode::NextShrine() const
{
	for (int32 I = 0; I < Shrines.Num(); ++I)
	{
		if (Shrines[I] != nullptr && !Shrines[I]->bCleared && !Shrines[I]->bSealed) { return I; }
	}
	return -1;
}

void AHWOpenWorldGameMode::SpawnAtStart(const FString& Subtitle)
{
	const int32 Next = bSpawnAtBell ? -1 : NextShrine();
	if (Shrines.IsValidIndex(Next))
	{
		// Build phase: just outside the next keeper's gate, facing in.
		const AHWShrine* S = Shrines[Next];
		const FVector At = S->Center() - S->Inward() * (AHWShrine::WallRadius + 400.f);
		SpawnExplorer(At, S->ArenaYaw());
		Banner(S->Spec.Title, Subtitle, 4.f);
		return;
	}
	const int32 B = (Save != nullptr && Bells.IsValidIndex(Save->CheckpointBell)) ? Save->CheckpointBell : 0;
	if (Bells.IsValidIndex(B) && Bells[B] != nullptr)
	{
		SpawnExplorer(Bells[B]->WakePoint(), static_cast<float>((-Bells[B]->GetActorLocation()).Rotation().Yaw));
		Banner(Bells[B]->Title, Subtitle, 4.f);
	}
}

void AHWOpenWorldGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Automation.Tick(GetWorld(), DeltaSeconds);
	PhaseSeconds += DeltaSeconds;
	if (ExplorerBody.IsValid() && ExplorerLook.IsValid() && (ExplorerBody->IsVisible() || !ExplorerLook->IsVisible()))
	{
		if (!bLoggedLookFix)
		{
			UE_LOG(LogHellwalkerRL, Log, TEXT("Explorer look restored (body visible %d, look visible %d)."), ExplorerBody->IsVisible() ? 1 : 0, ExplorerLook->IsVisible() ? 1 : 0);
			bLoggedLookFix = true;
		}
		ExplorerBody->SetVisibility(false);
		ExplorerLook->SetVisibility(true, true);
	}
	BannerLeft = FMath::Max(0.f, BannerLeft - DeltaSeconds);
	if (Save != nullptr && (Phase == EHWWorldPhase::Exploring || Phase == EHWWorldPhase::Duel)) { Save->PlaySeconds += DeltaSeconds; }

	if (Phase == EHWWorldPhase::DuelOver && PhaseSeconds > DuelOverSeconds)
	{
		FinishDuel();
		return;
	}
	if (Phase != EHWWorldPhase::Exploring) { return; }

	if (bTour) { TickTour(DeltaSeconds); }
	if (PendingDuel >= 0 && PhaseSeconds >= PendingDuelAt)
	{
		const int32 D = PendingDuel;
		PendingDuel = -1;
		BeginDuel(D);
		return;
	}

	// The prompt under the nearest thing you can use; a place's name the first time you walk into it.
	Prompt.Reset();
	APlayerController* Controller = PC();
	APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Pawn == nullptr || OpenWorld == nullptr) { return; }
	const FVector At = Pawn->GetActorLocation();
	for (const AHWBell* B : Bells)
	{
		if (B != nullptr && FVector::Dist2D(At, B->InteractPoint()) < BellReach)
		{
			Prompt = (Save != nullptr && Save->CheckpointBell == B->BellIndex) ? FString::Printf(TEXT("[E]  Rest at the %s"), *B->Title)
				: FString::Printf(TEXT("[E]  Ring the %s"), *B->Title);
		}
	}
	for (const AHWShrine* S : Shrines)
	{
		if (S == nullptr || FVector::Dist2D(At, S->GatePoint()) > GateReach) { continue; }
		Prompt = S->bCleared ? FString::Printf(TEXT("%s  -  the seal is broken"), *S->Spec.Title)
			: (S->bSealed ? TEXT("SEALED  -  break the other seals first") : FString::Printf(TEXT("[E]  Challenge %s"), *S->Spec.Title));
	}

	// Fell off the world somehow: back to the bell.
	if (At.Z < OpenWorld->GroundAt(FVector2D(At.X, At.Y)).Z - 3000.f && Save != nullptr && Bells.IsValidIndex(Save->CheckpointBell))
	{
		HWBuild::TeleportPawn(Pawn, Bells[Save->CheckpointBell]->WakePoint() + FVector(0.f, 0.f, 150.f), static_cast<float>(Pawn->GetActorRotation().Yaw));
	}
}

void AHWOpenWorldGameMode::TickTour(float DeltaSeconds)
{
	APlayerController* Controller = PC();
	APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Pawn == nullptr || OpenWorld == nullptr || !TourRoute.IsValidIndex(TourStep)) { return; }
	const FHWSite& S = OpenWorld->Gen().GetSites()[TourRoute[TourStep]];
	const FVector At = Pawn->GetActorLocation();
	FVector To = FVector(S.Pos.X, S.Pos.Y, At.Z) - At;
	To.Z = 0.0;
	if (To.Size() < 350.0)
	{
		UE_LOG(LogHellwalkerRL, Log, TEXT("Tour: reached %s."), *S.Title);
		TourStep = (TourStep + 1) % TourRoute.Num();
		return;
	}
	const FVector Dir = To.GetSafeNormal();
	Controller->SetControlRotation(FMath::RInterpTo(Controller->GetControlRotation(), FRotator(-14.f, Dir.Rotation().Yaw, 0.f), DeltaSeconds, 3.f));
	// Forward on the explorer's own move action (camera-relative: the camera now faces Dir); the direct input
	// is for pawns that read AddMovementInput (the greybox Soul, the CMC character).
	UEnhancedInputLocalPlayerSubsystem* Input = Controller->GetLocalPlayer() != nullptr ? Controller->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (Input != nullptr && TourMoveAction != nullptr) { Input->InjectInputForAction(TourMoveAction, FInputActionValue(FVector2D(0.0, 1.0)), {}, {}); }
	Pawn->AddMovementInput(Dir, 1.f);

	// Something in the way (a ruin's wall, a boulder): press the explorer's own jump — the Game Animation
	// Sample turns it into a vault or a mantle when the obstacle is traversable.
	TourJumpCooldown -= DeltaSeconds;
	TourStuckTime = FVector::Dist2D(At, TourLastPos) < 60.f * DeltaSeconds ? TourStuckTime + DeltaSeconds : 0.f;
	TourLastPos = At;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(HWTour), false, Pawn);
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, At, At + Dir * 160.f, ECC_Visibility, Q);
	if ((bBlocked || TourStuckTime > 0.4f) && TourJumpCooldown <= 0.f && TourJumpAction != nullptr)
	{
		if (ULocalPlayer* LP = Controller->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				Sub->InjectInputForAction(TourJumpAction, FInputActionValue(true), {}, {});
				TourJumpCooldown = 1.2f;
			}
		}
	}
	if (TourStuckTime > 6.f)
	{
		// Truly stuck: skip ahead (the tour is a demo, not a pathfinder).
		HWBuild::TeleportPawn(Pawn, FVector(S.Pos.X, S.Pos.Y, OpenWorld->GroundAt(S.Pos).Z + 150.0), static_cast<float>(Pawn->GetActorRotation().Yaw));
		TourStuckTime = 0.f;
	}
}

FString AHWOpenWorldGameMode::ObjectiveText() const
{
	if (Save == nullptr) { return FString(); }
	int32 Cleared = 0;
	for (int32 I = 0; I < Shrines.Num(); ++I) { Cleared += Save->IsShrineCleared(I) ? 1 : 0; }
	if (Cleared >= Shrines.Num() - 1 && Shrines.Num() > 0) { return TEXT("The Hell Gate is open. The Warden waits, remembering."); }
	return FString::Printf(TEXT("Break the seals  %d / %d"), Cleared, FMath::Max(0, Shrines.Num() - 1));
}

void AHWOpenWorldGameMode::GetMarkers(TArray<FMarker>& Out) const
{
	for (const AHWShrine* S : Shrines)
	{
		if (S == nullptr) { continue; }
		FMarker M;
		M.Location = S->Center() + FVector(0.f, 0.f, 900.f);
		M.Label = S->Spec.Title;
		M.Color = S->bCleared ? FLinearColor(0.4f, 0.6f, 1.f) : (S->bSealed ? FLinearColor(0.5f, 0.1f, 0.1f) : FLinearColor(0.95f, 0.15f, 0.08f));
		M.bPrimary = !S->bCleared && !S->bSealed;
		Out.Add(M);
	}
	for (const AHWBell* B : Bells)
	{
		if (B == nullptr) { continue; }
		FMarker M;
		M.Location = B->GetActorLocation() + FVector(0.f, 0.f, 600.f);
		M.Label = B->Title;
		M.Color = (Save != nullptr && Save->IsBellLit(B->BellIndex)) ? FLinearColor(1.f, 0.7f, 0.25f) : FLinearColor(0.55f, 0.5f, 0.45f);
		Out.Add(M);
	}
}
