#include "HWPlayerController.h"

#include "HellwalkerRL.h"
#include "HWDuelSubsystem.h"
#include "HWHUD.h"
#include "HWOpenWorldGameMode.h"
#include "HWPlayerCharacter.h"
#include "HWSessionSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"

namespace
{
	FString ShortClass(const UObject* O, const TCHAR* Prefix)
	{
		return O != nullptr ? O->GetClass()->GetName().Replace(Prefix, TEXT("")).Replace(TEXT("_C"), TEXT("")) : FString();
	}

	FString DescribeContext(const UInputMappingContext* C, const TCHAR* Role)
	{
		if (C == nullptr) { return FString::Printf(TEXT("\n== (missing) %s\n"), Role); }
		FString R = FString::Printf(TEXT("\n== %s  (%s)\n"), *C->GetPathName(), Role);
		for (const FEnhancedActionKeyMapping& M : C->GetMappings())
		{
			FString Extra;
			for (const UInputTrigger* T : M.Triggers) { if (T != nullptr) { Extra += TEXT("  +") + ShortClass(T, TEXT("InputTrigger")); } }
			for (const UInputModifier* Mod : M.Modifiers) { if (Mod != nullptr) { Extra += TEXT("  ~") + ShortClass(Mod, TEXT("InputModifier")); } }
			if (M.Action != nullptr)
			{
				for (const UInputTrigger* T : M.Action->Triggers) { if (T != nullptr) { Extra += TEXT("  +action:") + ShortClass(T, TEXT("InputTrigger")); } }
			}
			R += FString::Printf(TEXT("%-30s %-30s%s\n"), *GetNameSafe(M.Action), *M.Key.GetFName().ToString(), *Extra);
		}
		return R;
	}
}

FString AHWPlayerController::DescribeControls() const
{
	FString R = TEXT("Hellwalker controls, read back from the live mapping contexts.\n");
	R += DescribeContext(Context, TEXT("ours: the duel (and the greybox Soul explorer when the sample is missing)"));
	R += DescribeContext(ExploreContext, TEXT("ours: exploring, priority 10 - wins a shared key"));
	R += DescribeContext(LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Sandbox.IMC_Sandbox")), TEXT("the explorer pawn's own (Game Animation Sample)"));
	return R;
}

AHWPlayerController::AHWPlayerController()
{
	bShowMouseCursor = false;
}

void AHWPlayerController::BeginPlay()
{
	Super::BeginPlay();
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
}

UHWDuelSubsystem* AHWPlayerController::GetDuel() const
{
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UHWDuelSubsystem>() : nullptr;
}

AHWPlayerCharacter* AHWPlayerController::GetFighter() const
{
	return Cast<AHWPlayerCharacter>(GetPawn());
}

void AHWPlayerController::BuildInput()
{
	if (Context != nullptr) { return; }

	auto MakeAction = [this](const TCHAR* ActionName, EInputActionValueType Type)
	{
		UInputAction* A = NewObject<UInputAction>(this, ActionName);
		A->ValueType = Type;
		return A;
	};
	IA_Move = MakeAction(TEXT("IA_Move"), EInputActionValueType::Axis2D);
	IA_Look = MakeAction(TEXT("IA_Look"), EInputActionValueType::Axis2D);
	IA_Light = MakeAction(TEXT("IA_Light"), EInputActionValueType::Boolean);
	IA_Heavy = MakeAction(TEXT("IA_Heavy"), EInputActionValueType::Boolean);
	IA_Parry = MakeAction(TEXT("IA_Parry"), EInputActionValueType::Boolean);
	IA_Step = MakeAction(TEXT("IA_Step"), EInputActionValueType::Boolean);
	IA_Switch = MakeAction(TEXT("IA_Switch"), EInputActionValueType::Boolean);
	IA_Guard = MakeAction(TEXT("IA_Guard"), EInputActionValueType::Boolean);
	IA_Lock = MakeAction(TEXT("IA_Lock"), EInputActionValueType::Boolean);
	IA_Restart = MakeAction(TEXT("IA_Restart"), EInputActionValueType::Boolean);
	IA_Tier1 = MakeAction(TEXT("IA_Tier1"), EInputActionValueType::Boolean);
	IA_Tier2 = MakeAction(TEXT("IA_Tier2"), EInputActionValueType::Boolean);
	IA_Debug = MakeAction(TEXT("IA_Debug"), EInputActionValueType::Boolean);
	IA_Help = MakeAction(TEXT("IA_Help"), EInputActionValueType::Boolean);
	IA_Quit = MakeAction(TEXT("IA_Quit"), EInputActionValueType::Boolean);

	Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Hellwalker"));

	// Movement: x = right, y = forward. (Configure each mapping before the next MapKey — the returned
	// reference points into an array that MapKey may reallocate.)
	{
		FEnhancedActionKeyMapping& M = Context->MapKey(IA_Move, EKeys::W);
		M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
	}
	{
		FEnhancedActionKeyMapping& M = Context->MapKey(IA_Move, EKeys::S);
		M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
		M.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}
	Context->MapKey(IA_Move, EKeys::D);
	{
		FEnhancedActionKeyMapping& M = Context->MapKey(IA_Move, EKeys::A);
		M.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}
	Context->MapKey(IA_Move, EKeys::Gamepad_Left2D);
	Context->MapKey(IA_Look, EKeys::Mouse2D);
	Context->MapKey(IA_Look, EKeys::Gamepad_Right2D);

	Context->MapKey(IA_Light, EKeys::LeftMouseButton);
	Context->MapKey(IA_Light, EKeys::Gamepad_FaceButton_Left);
	Context->MapKey(IA_Heavy, EKeys::E);
	Context->MapKey(IA_Heavy, EKeys::Gamepad_FaceButton_Top);
	Context->MapKey(IA_Parry, EKeys::Q);
	Context->MapKey(IA_Parry, EKeys::Gamepad_RightShoulder);
	Context->MapKey(IA_Guard, EKeys::RightMouseButton);
	Context->MapKey(IA_Guard, EKeys::Gamepad_LeftShoulder);
	Context->MapKey(IA_Step, EKeys::SpaceBar);
	Context->MapKey(IA_Step, EKeys::Gamepad_FaceButton_Bottom);
	Context->MapKey(IA_Switch, EKeys::F);
	Context->MapKey(IA_Switch, EKeys::Gamepad_FaceButton_Right);
	Context->MapKey(IA_Lock, EKeys::Tab);
	Context->MapKey(IA_Lock, EKeys::MiddleMouseButton);
	Context->MapKey(IA_Lock, EKeys::Gamepad_RightThumbstick);
	Context->MapKey(IA_Restart, EKeys::R);
	Context->MapKey(IA_Restart, EKeys::Gamepad_Special_Right);
	Context->MapKey(IA_Tier1, EKeys::One);
	Context->MapKey(IA_Tier2, EKeys::Two);
	Context->MapKey(IA_Debug, EKeys::F3);
	Context->MapKey(IA_Help, EKeys::F1);
	Context->MapKey(IA_Help, EKeys::Gamepad_Special_Left);
	Context->MapKey(IA_Quit, EKeys::Escape);

	// Explore / title: only keys the explorer pawn does not use.
	IA_Interact = MakeAction(TEXT("IA_HWInteract"), EInputActionValueType::Boolean);
	IA_Choice1 = MakeAction(TEXT("IA_HWChoice1"), EInputActionValueType::Boolean);
	IA_Choice2 = MakeAction(TEXT("IA_HWChoice2"), EInputActionValueType::Boolean);
	IA_Choice3 = MakeAction(TEXT("IA_HWChoice3"), EInputActionValueType::Boolean);
	IA_Continue = MakeAction(TEXT("IA_HWContinue"), EInputActionValueType::Boolean);
	IA_ExploreQuit = MakeAction(TEXT("IA_HWExploreQuit"), EInputActionValueType::Boolean);
	ExploreContext = NewObject<UInputMappingContext>(this, TEXT("IMC_HellwalkerExplore"));
	ExploreContext->MapKey(IA_Interact, EKeys::E);
	ExploreContext->MapKey(IA_Interact, EKeys::Gamepad_FaceButton_Top);   // = E; GASP's own interact, which needs another sandbox character
	ExploreContext->MapKey(IA_Choice1, EKeys::One);
	ExploreContext->MapKey(IA_Choice2, EKeys::Two);
	ExploreContext->MapKey(IA_Choice3, EKeys::Three);
	ExploreContext->MapKey(IA_Continue, EKeys::Enter);                    // not C: C is the explorer's crouch (and slide)
	ExploreContext->MapKey(IA_Continue, EKeys::Gamepad_Special_Right);
	ExploreContext->MapKey(IA_ExploreQuit, EKeys::Escape);
	ExploreContext->MapKey(IA_Debug, EKeys::F3);
	ExploreContext->MapKey(IA_Help, EKeys::F1);
	ExploreContext->MapKey(IA_Help, EKeys::Gamepad_Special_Left);
}

void AHWPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	BuildInput();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (EIC == nullptr)
	{
		UE_LOG(LogHellwalkerRL, Error, TEXT("Enhanced Input is not the input component class — check Config/DefaultInput.ini."));
		return;
	}
	// One-shots bind Started on trigger-less actions (PLAN §7: never Triggered on a trigger-less action —
	// that is the "one click = full combo" bug). Held / axis inputs bind Triggered + Completed.
	EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AHWPlayerController::OnMove);
	EIC->BindAction(IA_Move, ETriggerEvent::Completed, this, &AHWPlayerController::OnMoveStop);
	EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AHWPlayerController::OnLook);
	EIC->BindAction(IA_Light, ETriggerEvent::Started, this, &AHWPlayerController::OnLight);
	EIC->BindAction(IA_Heavy, ETriggerEvent::Started, this, &AHWPlayerController::OnHeavy);
	EIC->BindAction(IA_Parry, ETriggerEvent::Started, this, &AHWPlayerController::OnParry);
	EIC->BindAction(IA_Step, ETriggerEvent::Started, this, &AHWPlayerController::OnStep);
	EIC->BindAction(IA_Switch, ETriggerEvent::Started, this, &AHWPlayerController::OnSwitch);
	EIC->BindAction(IA_Guard, ETriggerEvent::Started, this, &AHWPlayerController::OnGuardStart);
	EIC->BindAction(IA_Guard, ETriggerEvent::Completed, this, &AHWPlayerController::OnGuardEnd);
	EIC->BindAction(IA_Lock, ETriggerEvent::Started, this, &AHWPlayerController::OnLockToggle);
	EIC->BindAction(IA_Restart, ETriggerEvent::Started, this, &AHWPlayerController::OnRestart);
	EIC->BindAction(IA_Tier1, ETriggerEvent::Started, this, &AHWPlayerController::OnTierPathbreaker);
	EIC->BindAction(IA_Tier2, ETriggerEvent::Started, this, &AHWPlayerController::OnTierHellwalker);
	EIC->BindAction(IA_Debug, ETriggerEvent::Started, this, &AHWPlayerController::OnDebugToggle);
	EIC->BindAction(IA_Help, ETriggerEvent::Started, this, &AHWPlayerController::OnHelpToggle);
	EIC->BindAction(IA_Quit, ETriggerEvent::Started, this, &AHWPlayerController::OnQuit);
	EIC->BindAction(IA_Interact, ETriggerEvent::Started, this, &AHWPlayerController::OnInteract);
	EIC->BindAction(IA_Choice1, ETriggerEvent::Started, this, &AHWPlayerController::OnChoice, 1);
	EIC->BindAction(IA_Choice2, ETriggerEvent::Started, this, &AHWPlayerController::OnChoice, 2);
	EIC->BindAction(IA_Choice3, ETriggerEvent::Started, this, &AHWPlayerController::OnChoice, 3);
	EIC->BindAction(IA_Continue, ETriggerEvent::Started, this, &AHWPlayerController::OnContinue);
	EIC->BindAction(IA_ExploreQuit, ETriggerEvent::Started, this, &AHWPlayerController::OnQuit);
	ApplyInputMode();
}

bool AHWPlayerController::IsOpenWorld() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && Cast<AHWOpenWorldGameMode>(World->GetAuthGameMode()) != nullptr;
}

void AHWPlayerController::ApplyInputMode()
{
	ULocalPlayer* LP = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* Sub = LP != nullptr ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (Sub == nullptr || Context == nullptr) { return; }
	Sub->RemoveMappingContext(Context);
	Sub->RemoveMappingContext(ExploreContext);
	const bool bFighter = GetFighter() != nullptr;
	if (!IsOpenWorld())
	{
		Sub->AddMappingContext(Context, 0);           // the arena game: always the duel
		return;
	}
	if (bFighter && GetFighter() != nullptr)
	{
		const UHWDuelSubsystem* D = GetDuel();
		const bool bInDuel = D != nullptr && D->GetFighterActor(HW::ESide::Player) == GetFighter();
		Sub->AddMappingContext(Context, 0);           // a duel, or the fallback explorer (Soul walks the world)
		if (!bInDuel) { Sub->AddMappingContext(ExploreContext, 10); }
		return;
	}
	Sub->AddMappingContext(ExploreContext, 10);       // above the explorer's own context: E is ours
}

void AHWPlayerController::OnPossess(APawn* InPawn)
{
	// Clear first: the explorer pawn adds its own mapping context during Super::OnPossess.
	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) { Sub->ClearAllMappings(); }
	}
	Super::OnPossess(InPawn);
	if (AHWPlayerCharacter* P = GetFighter())
	{
		const UHWDuelSubsystem* D = GetDuel();
		const bool bInDuel = D != nullptr && D->GetFighterActor(HW::ESide::Player) == P;
		bLockedOn = !IsOpenWorld() || bInDuel;
		P->SetLockedOn(bLockedOn);
	}
	MoveInput = FVector2D::ZeroVector;
	ApplyInputMode();
}

void AHWPlayerController::OnUnPossess()
{
	Super::OnUnPossess();
	MoveInput = FVector2D::ZeroVector;
	ApplyInputMode();
}

void AHWPlayerController::OnInteract()
{
	if (AHWOpenWorldGameMode* GM = GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>()) { GM->Interact(); }
}

void AHWPlayerController::OnChoice(int32 Choice)
{
	AHWOpenWorldGameMode* GM = GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>();
	if (GM == nullptr) { return; }
	const EHWWorldPhase Phase = GM->GetPhase();
	if (Phase != EHWWorldPhase::Title && Phase != EHWWorldPhase::Ending && Phase != EHWWorldPhase::Regret) { return; }
	GM->NewGame(Choice == 1 ? EHWPlayMode::Pathbreaker : (Choice == 3 ? EHWPlayMode::SixtySixDays : EHWPlayMode::Hellwalker));
}

void AHWPlayerController::OnContinue()
{
	AHWOpenWorldGameMode* GM = GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>();
	if (GM != nullptr && GM->GetPhase() == EHWWorldPhase::Title) { GM->ContinueGame(); }
}

// ---- handlers ------------------------------------------------------------------------------------

void AHWPlayerController::OnMove(const FInputActionValue& Value) { MoveInput = Value.Get<FVector2D>(); }
void AHWPlayerController::OnMoveStop(const FInputActionValue& Value) { (void)Value; MoveInput = FVector2D::ZeroVector; }

void AHWPlayerController::OnLook(const FInputActionValue& Value)
{
	if (bLockedOn) { return; } // the lock-on owns the camera
	const FVector2D V = Value.Get<FVector2D>();
	AddYawInput(V.X);
	AddPitchInput(-V.Y);
}

void AHWPlayerController::OnLight()  { if (UHWDuelSubsystem* D = GetDuel()) { D->QueuePlayerInput(EHWPlayerAction::Light); } }
void AHWPlayerController::OnHeavy()  { if (UHWDuelSubsystem* D = GetDuel()) { D->QueuePlayerInput(EHWPlayerAction::Heavy); } }
void AHWPlayerController::OnParry()  { if (UHWDuelSubsystem* D = GetDuel()) { D->QueuePlayerInput(EHWPlayerAction::Parry); } }
void AHWPlayerController::OnSwitch() { if (UHWDuelSubsystem* D = GetDuel()) { D->QueuePlayerInput(EHWPlayerAction::Switch); } }

void AHWPlayerController::OnStep()
{
	if (UHWDuelSubsystem* D = GetDuel()) { D->QueuePlayerInput(EHWPlayerAction::Step, StepDirectionFromInput()); }
}

void AHWPlayerController::OnGuardStart()
{
	bGuardDown = true;
	if (UHWDuelSubsystem* D = GetDuel()) { D->QueuePlayerInput(EHWPlayerAction::GuardDown); }
}

void AHWPlayerController::OnGuardEnd()
{
	bGuardDown = false;
	if (UHWDuelSubsystem* D = GetDuel()) { D->QueuePlayerInput(EHWPlayerAction::GuardUp); }
}

void AHWPlayerController::OnLockToggle()
{
	bLockedOn = !bLockedOn;
	if (AHWPlayerCharacter* P = GetFighter()) { P->SetLockedOn(bLockedOn); }
}

void AHWPlayerController::OnRestart()
{
	if (IsOpenWorld()) { return; } // the open world owns the flow: you wake at your bell
	UHWDuelSubsystem* D = GetDuel();
	if (D != nullptr && D->GetState() != EHWEncounterState::WaitingToStart)
	{
		D->ResetEncounter(-1);
	}
}

void AHWPlayerController::OnTierPathbreaker()
{
	if (IsOpenWorld()) { return; }
	UHWDuelSubsystem* D = GetDuel();
	if (D == nullptr) { return; }
	const UHWSessionSubsystem* S = D->GetSession();
	// Blind mode: key 1 is "Variant A", whichever tier that is this session.
	const EHWTier Tier = (S != nullptr && S->bBlind) ? S->BlindVariantA : EHWTier::Pathbreaker;
	D->StartEncounter(Tier);
}

void AHWPlayerController::OnTierHellwalker()
{
	if (IsOpenWorld()) { return; }
	UHWDuelSubsystem* D = GetDuel();
	if (D == nullptr) { return; }
	const UHWSessionSubsystem* S = D->GetSession();
	EHWTier Tier = EHWTier::Hellwalker;
	if (S != nullptr && S->bBlind)
	{
		Tier = S->BlindVariantA == EHWTier::Pathbreaker ? EHWTier::Hellwalker : EHWTier::Pathbreaker;
	}
	D->StartEncounter(Tier);
}

void AHWPlayerController::OnDebugToggle()
{
	if (UHWDuelSubsystem* D = GetDuel()) { D->bDebugDraw = !D->bDebugDraw; }
}

void AHWPlayerController::OnHelpToggle()
{
	if (AHWHUD* H = Cast<AHWHUD>(GetHUD())) { H->ToggleHelp(); }
}

void AHWPlayerController::OnQuit()
{
	ConsoleCommand(TEXT("quit"));
}

HW::EDir AHWPlayerController::StepDirectionFromInput() const
{
	FVector2D In = MoveInput;
	const UHWDuelSubsystem* D = GetDuel();
	if (!bLockedOn && D != nullptr && !In.IsNearlyZero())
	{
		// Unlocked: turn the camera-relative input into the target-space axes.
		const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
		const FVector World = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * In.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * In.X;
		const FVector Fwd = D->TargetDirection(HW::ESide::Player);
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
		In = FVector2D(FVector::DotProduct(World, Right), FVector::DotProduct(World, Fwd));
	}
	if (In.IsNearlyZero(0.3f)) { return HW::EDir::Back; } // neutral ghoststep: out of range
	if (FMath::Abs(In.Y) >= FMath::Abs(In.X)) { return In.Y > 0.f ? HW::EDir::Forward : HW::EDir::Back; }
	return In.X > 0.f ? HW::EDir::Right : HW::EDir::Left;
}

void AHWPlayerController::StartStrafeTest(float Seconds, float ToleranceDegrees)
{
	StrafeTestDuration = Seconds;
	StrafeTestLeft = Seconds;
	StrafeTolerance = ToleranceDegrees;
	StrafeWorstDegrees = 0.f;
	StrafeYawTravelled = 0.f;
	StrafeLastYaw = 0.f;
	bLockedOn = true;
	if (AHWPlayerCharacter* P = GetFighter()) { P->SetLockedOn(true); }
	UE_LOG(LogHellwalkerRL, Log, TEXT("A0 strafe test: %.1f s, tolerance %.1f deg."), Seconds, ToleranceDegrees);
}

void AHWPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	AHWPlayerCharacter* P = GetFighter();
	UHWDuelSubsystem* D = GetDuel();
	if (P == nullptr || D == nullptr) { return; }

	if (StrafeTestLeft > 0.f)
	{
		MoveInput = FVector2D(1.f, 0.f); // pure strafe right around the lock-on target
		const FVector To = D->TargetDirection(HW::ESide::Player);
		const float Err = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(P->GetActorForwardVector().GetSafeNormal2D(), To)), -1.f, 1.f)));
		const float Elapsed = StrafeTestDuration - StrafeTestLeft;
		if (Elapsed > 0.5f) { StrafeWorstDegrees = FMath::Max(StrafeWorstDegrees, Err); } // ignore the first settle
		const float Yaw = static_cast<float>(To.Rotation().Yaw);
		if (Elapsed > 0.f) { StrafeYawTravelled += FMath::Abs(FMath::FindDeltaAngleDegrees(StrafeLastYaw, Yaw)); }
		StrafeLastYaw = Yaw;
		StrafeTestLeft -= DeltaTime;
		if (StrafeTestLeft <= 0.f)
		{
			MoveInput = FVector2D::ZeroVector;
			const bool bPass = StrafeWorstDegrees <= StrafeTolerance && StrafeYawTravelled >= 360.f;
			UE_LOG(LogHellwalkerRL, Log, TEXT("A0 strafe test %s: circled %.0f deg around the boss, worst character-facing error %.2f deg (tolerance %.1f)."),
				bPass ? TEXT("PASS") : TEXT("FAIL"), StrafeYawTravelled, StrafeWorstDegrees, StrafeTolerance);
		}
	}

	// Lock-on (PLAN A0): interp the control yaw toward the boss; the character follows the control yaw.
	if (bLockedOn && D->GetFighterActor(HW::ESide::Boss) != nullptr)
	{
		const FVector To = D->TargetDirection(HW::ESide::Player);
		const FRotator Want(-16.f, To.Rotation().Yaw, 0.f);
		SetControlRotation(FMath::RInterpTo(GetControlRotation(), Want, DeltaTime, 10.f));
	}

	const FVector2D In = D->IsAutoplay() ? D->GetAutoplayWalk() : MoveInput;
	const float Scale = D->PlayerMoveScale();
	if (Scale > 0.f && !In.IsNearlyZero())
	{
		FVector Fwd;
		FVector Right;
		if (bLockedOn)
		{
			Fwd = D->TargetDirection(HW::ESide::Player);
			Right = FVector::CrossProduct(FVector::UpVector, Fwd);
		}
		else
		{
			const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
			Fwd = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X);
			Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
		}
		P->AddMovementInput(Fwd, In.Y * Scale);
		P->AddMovementInput(Right, In.X * Scale);
	}
}
