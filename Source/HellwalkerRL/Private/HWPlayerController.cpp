#include "HWPlayerController.h"

#include "HellwalkerRL.h"
#include "HWDuelSubsystem.h"
#include "HWHUD.h"
#include "HWOpenWorldGameMode.h"
#include "HWPlayerCharacter.h"
#include "HWSessionSubsystem.h"
#include "HWSettings.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
				if (M.Action->bTriggerWhenPaused) { Extra += TEXT("  (when paused)"); }
			}
			R += FString::Printf(TEXT("%-30s %-30s%s\n"), *GetNameSafe(M.Action), *M.Key.GetFName().ToString(), *Extra);
		}
		return R;
	}
}

FString AHWPlayerController::DescribeControls() const
{
	FString R = TEXT("Hellwalker controls, read back from the live mapping contexts (keyboard keys from the player's settings).\n");
	R += DescribeContext(Context, TEXT("ours: the duel (and the greybox Soul explorer when the sample is missing)"));
	R += DescribeContext(ExploreContext, TEXT("ours: exploring, priority 10 - wins a shared key"));
	R += DescribeContext(MenuContext, TEXT("ours: menus, priority 100, only while a menu is open"));
	R += DescribeContext(LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Sandbox.IMC_Sandbox")), TEXT("the explorer pawn's own (Game Animation Sample)"));
	return R;
}

AHWPlayerController::AHWPlayerController()
{
	bShowMouseCursor = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

UHWSettingsSubsystem* AHWPlayerController::GetSettings() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI != nullptr ? GI->GetSubsystem<UHWSettingsSubsystem>() : nullptr;
}

void AHWPlayerController::BeginPlay()
{
	Super::BeginPlay();
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
	if (UHWSettingsSubsystem* S = GetSettings())
	{
		S->ApplyAll();
		BindingsChangedHandle = S->OnBindingsChanged.AddUObject(this, &AHWPlayerController::OnBindingsChanged);
	}
	// Scripted runs (-unattended, a bot at the controls) never stop for the tutorial; -HWTutorial forces it.
	const TCHAR* Cmd = FCommandLine::Get();
	bAutoTutorial = FParse::Param(Cmd, TEXT("HWTutorial")) || !(FApp::IsUnattended() || FParse::Param(Cmd, TEXT("HWNoTutorial")) || FParse::Param(Cmd, TEXT("HWAutoplay")));
	Menu.Builder = [this](EHWMenuPage Page, EHWSettingsTab Tab, int32 Slide, TArray<FHWMenuItem>& OutItems, FHWMenuPageInfo& OutInfo)
	{
		BuildMenuPage(Page, Tab, Slide, OutItems, OutInfo);
	};
}

void AHWPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UHWSettingsSubsystem* S = GetSettings()) { S->OnBindingsChanged.Remove(BindingsChangedHandle); }
	Super::EndPlay(EndPlayReason);
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

	auto MakeAction = [this](const TCHAR* ActionName, EInputActionValueType Type, bool bWhenPaused = false)
	{
		UInputAction* A = NewObject<UInputAction>(this, ActionName);
		A->ValueType = Type;
		A->bTriggerWhenPaused = bWhenPaused;
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
	IA_Pause = MakeAction(TEXT("IA_Pause"), EInputActionValueType::Boolean);
	IA_Notebook = MakeAction(TEXT("IA_Notebook"), EInputActionValueType::Boolean);
	Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Hellwalker"));

	IA_Interact = MakeAction(TEXT("IA_HWInteract"), EInputActionValueType::Boolean);
	IA_Map = MakeAction(TEXT("IA_HWMap"), EInputActionValueType::Boolean);
	IA_Choice1 = MakeAction(TEXT("IA_HWChoice1"), EInputActionValueType::Boolean);
	IA_Choice2 = MakeAction(TEXT("IA_HWChoice2"), EInputActionValueType::Boolean);
	ExploreContext = NewObject<UInputMappingContext>(this, TEXT("IMC_HellwalkerExplore"));

	// The menu's actions trigger while the game is paused (that is when the menu is open).
	IA_MenuUp = MakeAction(TEXT("IA_MenuUp"), EInputActionValueType::Boolean, true);
	IA_MenuDown = MakeAction(TEXT("IA_MenuDown"), EInputActionValueType::Boolean, true);
	IA_MenuLeft = MakeAction(TEXT("IA_MenuLeft"), EInputActionValueType::Boolean, true);
	IA_MenuRight = MakeAction(TEXT("IA_MenuRight"), EInputActionValueType::Boolean, true);
	IA_MenuAccept = MakeAction(TEXT("IA_MenuAccept"), EInputActionValueType::Boolean, true);
	IA_MenuBack = MakeAction(TEXT("IA_MenuBack"), EInputActionValueType::Boolean, true);
	IA_MenuToggle = MakeAction(TEXT("IA_MenuToggle"), EInputActionValueType::Boolean, true);
	IA_MenuTabPrev = MakeAction(TEXT("IA_MenuTabPrev"), EInputActionValueType::Boolean, true);
	IA_MenuTabNext = MakeAction(TEXT("IA_MenuTabNext"), EInputActionValueType::Boolean, true);
	IA_MenuScroll = MakeAction(TEXT("IA_MenuScroll"), EInputActionValueType::Axis1D, true);
	IA_MenuPointer = MakeAction(TEXT("IA_MenuPointer"), EInputActionValueType::Boolean, true);
	IA_MenuMap = MakeAction(TEXT("IA_MenuMap"), EInputActionValueType::Boolean, true);
	MenuContext = NewObject<UInputMappingContext>(this, TEXT("IMC_HellwalkerMenu"));

	MapContexts();
}

void AHWPlayerController::MapContexts()
{
	if (Context == nullptr) { return; }
	const UHWSettingsSubsystem* S = GetSettings();
	const FHWBindingTable Defaults;
	const FHWBindingTable& B = S != nullptr ? S->GetBindings() : Defaults;
	auto Key = [&B](EHWBind A) { return B.Get(A); };
	auto Pad = [](EHWBind A) { return FHWBindingTable::GamepadKey(A); };

	// ---- the duel
	Context->UnmapAll();
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

	const TPair<UInputAction*, EHWBind> Duel[] = {
		{ IA_Light, EHWBind::Light }, { IA_Heavy, EHWBind::Heavy }, { IA_Parry, EHWBind::Parry }, { IA_Guard, EHWBind::Guard },
		{ IA_Step, EHWBind::Step }, { IA_Switch, EHWBind::Switch }, { IA_Lock, EHWBind::LockOn }, { IA_Restart, EHWBind::Restart },
		{ IA_Debug, EHWBind::Debug }, { IA_Help, EHWBind::Help }, { IA_Pause, EHWBind::Pause }, { IA_Notebook, EHWBind::Notebook },
	};
	for (const TPair<UInputAction*, EHWBind>& P : Duel)
	{
		Context->MapKey(P.Key, Key(P.Value));
		if (Pad(P.Value).IsValid()) { Context->MapKey(P.Key, Pad(P.Value)); }
	}
	Context->MapKey(IA_Pause, EKeys::Escape);                 // Esc always opens the menu
	Context->MapKey(IA_Tier1, EKeys::One);                    // the arena's tier keys (the open world ignores them)
	Context->MapKey(IA_Tier1, EKeys::Gamepad_DPad_Left);
	Context->MapKey(IA_Tier2, EKeys::Two);
	Context->MapKey(IA_Tier2, EKeys::Gamepad_DPad_Right);

	// ---- exploring / the title: only keys the explorer pawn does not use (FHWBindingTable refuses its keys)
	ExploreContext->UnmapAll();
	ExploreContext->MapKey(IA_Interact, Key(EHWBind::Interact));
	ExploreContext->MapKey(IA_Interact, Pad(EHWBind::Interact)); // = E; GASP's own interact, which needs another sandbox character
	// The map: M and D-pad left shadow the sample's "next visual override" and "next pawn" (they would undress Soul).
	ExploreContext->MapKey(IA_Map, Key(EHWBind::Map));
	ExploreContext->MapKey(IA_Map, Pad(EHWBind::Map));
	ExploreContext->MapKey(IA_Choice1, EKeys::One);
	ExploreContext->MapKey(IA_Choice2, EKeys::Two);
	for (const TPair<UInputAction*, EHWBind>& P : { TPair<UInputAction*, EHWBind>(IA_Pause, EHWBind::Pause), TPair<UInputAction*, EHWBind>(IA_Debug, EHWBind::Debug),
		TPair<UInputAction*, EHWBind>(IA_Help, EHWBind::Help), TPair<UInputAction*, EHWBind>(IA_Notebook, EHWBind::Notebook) })
	{
		ExploreContext->MapKey(P.Key, Key(P.Value));
		ExploreContext->MapKey(P.Key, Pad(P.Value));
	}
	ExploreContext->MapKey(IA_Pause, EKeys::Escape);

	// ---- menus: arrows / WASD / D-pad / left stick, Enter / Space / A, Esc / Backspace / B, Start, LB / RB, the wheel.
	// The pointer action holds both mouse buttons so a click on "Resume" never becomes a light attack.
	MenuContext->UnmapAll();
	for (const FKey& K : { EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_LeftStick_Up }) { MenuContext->MapKey(IA_MenuUp, K); }
	for (const FKey& K : { EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_LeftStick_Down }) { MenuContext->MapKey(IA_MenuDown, K); }
	for (const FKey& K : { EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftStick_Left }) { MenuContext->MapKey(IA_MenuLeft, K); }
	for (const FKey& K : { EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftStick_Right }) { MenuContext->MapKey(IA_MenuRight, K); }
	for (const FKey& K : { EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom }) { MenuContext->MapKey(IA_MenuAccept, K); }
	for (const FKey& K : { EKeys::Escape, EKeys::BackSpace, EKeys::Gamepad_FaceButton_Right }) { MenuContext->MapKey(IA_MenuBack, K); }
	for (const FKey& K : { EKeys::Gamepad_LeftShoulder, EKeys::Q, EKeys::PageUp }) { MenuContext->MapKey(IA_MenuTabPrev, K); }
	for (const FKey& K : { EKeys::Gamepad_RightShoulder, EKeys::E, EKeys::PageDown }) { MenuContext->MapKey(IA_MenuTabNext, K); }
	MenuContext->MapKey(IA_MenuToggle, EKeys::Gamepad_Special_Right);
	{
		// The pause key closes the menu too, unless the player put it on one of the menu's own keys.
		const FKey PauseKey = Key(EHWBind::Pause);
		const bool bTaken = MenuContext->GetMappings().ContainsByPredicate([&PauseKey](const FEnhancedActionKeyMapping& M) { return M.Key == PauseKey; });
		if (!bTaken) { MenuContext->MapKey(IA_MenuToggle, PauseKey); }
		// So does the map key on the map page (D-pad left as well: on that page left / right do nothing else).
		const FKey MapKey = Key(EHWBind::Map);
		const bool bMapTaken = MenuContext->GetMappings().ContainsByPredicate([&MapKey](const FEnhancedActionKeyMapping& M) { return M.Key == MapKey; });
		if (!bMapTaken) { MenuContext->MapKey(IA_MenuMap, MapKey); }
		MenuContext->MapKey(IA_MenuMap, Pad(EHWBind::Map));
	}
	MenuContext->MapKey(IA_MenuScroll, EKeys::MouseWheelAxis);
	MenuContext->MapKey(IA_MenuPointer, EKeys::LeftMouseButton);
	MenuContext->MapKey(IA_MenuPointer, EKeys::RightMouseButton);

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) { Sub->RequestRebuildControlMappings(); }
	}
}

void AHWPlayerController::OnBindingsChanged()
{
	MapContexts();
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
	// that is the "one click = full combo" bug). Held / axis inputs bind Triggered + Completed (+ Canceled: a menu
	// taking the key mid-hold must not leave the guard up or the feet walking).
	EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AHWPlayerController::OnMove);
	EIC->BindAction(IA_Move, ETriggerEvent::Completed, this, &AHWPlayerController::OnMoveStop);
	EIC->BindAction(IA_Move, ETriggerEvent::Canceled, this, &AHWPlayerController::OnMoveStop);
	EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AHWPlayerController::OnLook);
	EIC->BindAction(IA_Light, ETriggerEvent::Started, this, &AHWPlayerController::OnLight);
	EIC->BindAction(IA_Heavy, ETriggerEvent::Started, this, &AHWPlayerController::OnHeavy);
	EIC->BindAction(IA_Parry, ETriggerEvent::Started, this, &AHWPlayerController::OnParry);
	EIC->BindAction(IA_Step, ETriggerEvent::Started, this, &AHWPlayerController::OnStep);
	EIC->BindAction(IA_Switch, ETriggerEvent::Started, this, &AHWPlayerController::OnSwitch);
	EIC->BindAction(IA_Guard, ETriggerEvent::Started, this, &AHWPlayerController::OnGuardStart);
	EIC->BindAction(IA_Guard, ETriggerEvent::Completed, this, &AHWPlayerController::OnGuardEnd);
	EIC->BindAction(IA_Guard, ETriggerEvent::Canceled, this, &AHWPlayerController::OnGuardEnd);
	EIC->BindAction(IA_Lock, ETriggerEvent::Started, this, &AHWPlayerController::OnLockToggle);
	EIC->BindAction(IA_Restart, ETriggerEvent::Started, this, &AHWPlayerController::OnRestart);
	EIC->BindAction(IA_Tier1, ETriggerEvent::Started, this, &AHWPlayerController::OnTierPathbreaker);
	EIC->BindAction(IA_Tier2, ETriggerEvent::Started, this, &AHWPlayerController::OnTierHellwalker);
	EIC->BindAction(IA_Debug, ETriggerEvent::Started, this, &AHWPlayerController::OnDebugToggle);
	EIC->BindAction(IA_Help, ETriggerEvent::Started, this, &AHWPlayerController::OnHelpToggle);
	EIC->BindAction(IA_Pause, ETriggerEvent::Started, this, &AHWPlayerController::OnPause);
	EIC->BindAction(IA_Notebook, ETriggerEvent::Started, this, &AHWPlayerController::OnNotebook);
	EIC->BindAction(IA_Interact, ETriggerEvent::Started, this, &AHWPlayerController::OnInteract);
	EIC->BindAction(IA_Map, ETriggerEvent::Started, this, &AHWPlayerController::OnMap);
	EIC->BindAction(IA_Choice1, ETriggerEvent::Started, this, &AHWPlayerController::OnChoice, 1);
	EIC->BindAction(IA_Choice2, ETriggerEvent::Started, this, &AHWPlayerController::OnChoice, 2);

	EIC->BindAction(IA_MenuUp, ETriggerEvent::Started, this, &AHWPlayerController::OnMenuNavStarted, 1);
	EIC->BindAction(IA_MenuDown, ETriggerEvent::Started, this, &AHWPlayerController::OnMenuNavStarted, 2);
	EIC->BindAction(IA_MenuLeft, ETriggerEvent::Started, this, &AHWPlayerController::OnMenuNavStarted, 3);
	EIC->BindAction(IA_MenuRight, ETriggerEvent::Started, this, &AHWPlayerController::OnMenuNavStarted, 4);
	EIC->BindAction(IA_MenuUp, ETriggerEvent::Completed, this, &AHWPlayerController::OnMenuNavCompleted, 1);
	EIC->BindAction(IA_MenuDown, ETriggerEvent::Completed, this, &AHWPlayerController::OnMenuNavCompleted, 2);
	EIC->BindAction(IA_MenuLeft, ETriggerEvent::Completed, this, &AHWPlayerController::OnMenuNavCompleted, 3);
	EIC->BindAction(IA_MenuRight, ETriggerEvent::Completed, this, &AHWPlayerController::OnMenuNavCompleted, 4);
	const TPair<UInputAction*, const TCHAR*> MenuKeys[] = {
		{ IA_MenuAccept, TEXT("accept") }, { IA_MenuBack, TEXT("back") }, { IA_MenuToggle, TEXT("toggle") },
		{ IA_MenuTabPrev, TEXT("tabprev") }, { IA_MenuTabNext, TEXT("tabnext") }, { IA_MenuMap, TEXT("map") },
	};
	for (const TPair<UInputAction*, const TCHAR*>& K : MenuKeys)
	{
		const FString Command = K.Value;
		EIC->BindActionValueLambda(K.Key, ETriggerEvent::Started, [this, Command](const FInputActionValue&) { MenuCommand(Command); });
	}
	EIC->BindAction(IA_MenuScroll, ETriggerEvent::Triggered, this, &AHWPlayerController::OnMenuScroll); // every notch
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
	Sub->RemoveMappingContext(MenuContext);
	if (bMenuInput) { Sub->AddMappingContext(MenuContext, 100); }
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
	if (Phase == EHWWorldPhase::Title && Menu.GetPage() == EHWMenuPage::Title)
	{
		// The title's shortcuts go through its menu (which asks before a new walk erases the saved one).
		const int32 I = Menu.FindItem(Choice == 1 ? HWMenuIds::NewNormal : HWMenuIds::NewAdaptive);
		if (I != INDEX_NONE) { HandleMenuEvent(Menu.Click(I)); }
		return;
	}
	if (Phase != EHWWorldPhase::Title && Phase != EHWWorldPhase::Ending && Phase != EHWWorldPhase::Regret) { return; }
	if (Menu.IsOpen()) { return; }
	GM->NewGame(Choice == 1 ? EHWPlayMode::Pathbreaker : EHWPlayMode::Hellwalker);
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

void AHWPlayerController::AddYawInput(float Val)
{
	const UHWSettingsSubsystem* S = GetSettings();
	Super::AddYawInput(Val * ((S != nullptr && !bGamepadLast) ? S->GetMouseSensitivity() : 1.f));
}

void AHWPlayerController::AddPitchInput(float Val)
{
	const UHWSettingsSubsystem* S = GetSettings();
	const float Sens = (S != nullptr && !bGamepadLast) ? S->GetMouseSensitivity() : 1.f;
	Super::AddPitchInput(Val * Sens * ((S != nullptr && S->GetInvertY()) ? -1.f : 1.f));
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
	if (!bGuardDown) { return; }
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

void AHWPlayerController::OnPause()
{
	if (Menu.IsOpen()) { CloseMenu(); return; }
	OpenPauseMenu();
}

void AHWPlayerController::OnMap()
{
	// Exploring only (the action is in the explore context, which a duel does not have); a menu already open keeps it.
	if (Menu.IsOpen()) { return; }
	OpenMap();
}

void AHWPlayerController::OnNotebook()
{
	if (Menu.IsOpen()) { return; }
	const AHWOpenWorldGameMode* GM = GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>();
	if (GM != nullptr && GM->GetPhase() == EHWWorldPhase::Title) { return; }
	OpenMenuRoot(EHWMenuPage::Pause, true);
	Menu.Push(EHWMenuPage::Notebook);
}

bool AHWPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// Which device spoke last (the HUD's hints and the look sensitivity follow it). Analog noise does not count.
	if (Params.Event == IE_Pressed || (Params.Event == IE_Axis && FMath::Abs(Params.AmountDepressed) > 0.35f))
	{
		bGamepadLast = Params.Key.IsGamepadKey();
	}
	if (Menu.IsCapturing() && Params.Event == IE_Pressed)
	{
		FinishCapture(Params.Key);
		return true; // the captured key never reaches the menu or the game
	}
	return Super::InputKey(Params);
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

void AHWPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickMenu(DeltaSeconds);
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
