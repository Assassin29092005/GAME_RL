// Hellwalker — player controller. All Enhanced Input is created in C++ at runtime (no .uasset):
// actions, the mapping contexts and their modifiers. Presses become timestamped duel input; the camera
// lock-on runs in PlayerTick (PLAN A0).
//
// Three contexts. COMBAT (possessing Soul in a duel): the duel context. EXPLORE (the open world): a small
// context — interact, the map, title shortcuts, pause, help — on top of whatever the explorer pawn adds for itself (the Game
// Animation Sample's character adds its own mapping context when possessed). MENU (priority 100, only while a menu is
// open): navigation, accept / back, tabs — it consumes its keys so nothing reaches the duel. Every possession clears
// the mappings first, so the sets never fight over a key.
//
// Keyboard / mouse keys come from the player's settings (UHWSettingsSubsystem: rebindable, persisted); the contexts are
// rebuilt whenever they change. Gamepad buttons and WASD / Esc are fixed.
//
// Menus (the model: FHWMenu, HWMenu.h; the pages and their actions: HWPlayerControllerMenu.cpp; drawn by AHWHUD).
// Esc / Start open and close the pause menu in both modes; pausing really pauses the world (the duel's frame cursor
// stops and does not catch up). The open world's title is a menu too, and so is the valley map (M / D-pad left while
// exploring: a root page that pauses like the pause menu; its items are the keepers — accept tracks one).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HWCore/HWTypes.h"
#include "HWMap.h"
#include "HWMenu.h"
#include "HWPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UHWDuelSubsystem;
class UHWSettingsSubsystem;
class AHWPlayerCharacter;
struct FInputActionValue;

UCLASS()
class HELLWALKERRL_API AHWPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AHWPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	/** Runs while paused too: menu key repeat, messages, the title menu, the first-duel tutorial. */
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	/** Key capture for rebinding (the next key or mouse button), and which device was used last. */
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	/** Look input scaled by the mouse sensitivity (and Y inverted) — the duel's and the explorer pawn's alike. */
	virtual void AddYawInput(float Val) override;
	virtual void AddPitchInput(float Val) override;

	bool IsLockedOn() const { return bLockedOn; }

	/**
	 * PLAN A0 done-test: strafe around the boss for Seconds while locked on and track the worst angle
	 * between the CHARACTER's forward vector and the direction to the boss (asserting the camera would pass
	 * while the character faces away — which silently voids the target-space requirement).
	 */
	void StartStrafeTest(float Seconds, float ToleranceDegrees);

	/**
	 * Every live key binding, read back from the mapping contexts themselves: ours (duel, explore, menu) and the
	 * explorer pawn's own (the Game Animation Sample's IMC_Sandbox), with triggers and modifiers. hw.Controls
	 * writes it to Saved/HellwalkerRL/Controls.txt — the controls map is generated, never hand-copied.
	 */
	FString DescribeControls() const;

	// ---- menus ------------------------------------------------------------------------------------------
	const FHWMenu& GetMenu() const { return Menu; }
	bool IsMenuOpen() const { return Menu.IsOpen(); }
	/** Pause and open the pause menu (the title menu at the open world's title). */
	void OpenPauseMenu();
	/** Close every page and resume. */
	void CloseMenu();
	/** hw.Menu: pause | settings | gameplay | controls | graphics | audio | accessibility | tutorial | notebook | title | close. */
	bool OpenMenuPage(const FString& Name);
	/** hw.MenuNav and every menu key: up | down | left | right | accept | back | tabnext | tabprev | toggle. */
	void MenuCommand(const FString& Command);
	/** The HUD's hit boxes ("M:I:3" item, "M:L:3" / "M:R:3" value arrows, "M:T:1" tab, "M:C:0" confirm button). */
	void MenuPointer(FName Box, bool bClick);
	/** A line under the menu for a few seconds (rebinding results, "applied"). */
	const FString& GetMenuMessage() const { return MenuMessage; }
	float GetMenuMessageAlpha() const { return FMath::Clamp(MenuMessageLeft / 0.5f, 0.f, 1.f); }
	/** The HUD, each frame: how many list rows fit on screen (keeps the selection scrolled into view). */
	void SetMenuVisibleRows(int32 Rows) { Menu.EnsureVisible(Rows); }
	/** The last button pressed was a gamepad's: the HUD shows gamepad hints. */
	bool IsGamepadActive() const { return bGamepadLast; }
	UHWSettingsSubsystem* GetSettings() const;

	// ---- the valley map (open world, exploring) -----------------------------------------------------------
	/** Pause and open the map, centred on the player, the tracked keeper selected. False when it cannot open now. */
	bool OpenMap();
	bool IsMapOpen() const { return Menu.GetPage() == EHWMenuPage::Map; }
	/** hw.Map [open | close | track <0|1|2|auto> | zoom <x>]; no argument toggles. */
	void MapConsole(const TArray<FString>& Args);
	/** The map's view: zoom and pan (the controller's), placed where the HUD drew it last frame. */
	HWMap::FView GetMapView() const;
	/** The HUD, each frame the map is drawn: where its square is (drag-to-pan and zoom-at-the-cursor need it). */
	void SetMapScreen(const FVector2D& Origin, double Side) { MapOrigin = Origin; MapSide = Side; }
	/** The map page's item for a keeper ("Track_1"), and back (-1: not a map item). */
	static FName TrackId(int32 Keeper);
	static int32 ParseTrackId(FName Id);

private:
	/** The actions (once) and the contexts (again whenever the bindings change). */
	void BuildInput();
	void MapContexts();
	void OnBindingsChanged();
	UHWDuelSubsystem* GetDuel() const;
	AHWPlayerCharacter* GetFighter() const;
	/** Map the current movement input onto a TARGET-space ghoststep direction (PLAN §2.1). */
	HW::EDir StepDirectionFromInput() const;

	void OnMove(const FInputActionValue& Value);
	void OnMoveStop(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnLight();
	void OnHeavy();
	void OnParry();
	void OnStep();
	void OnSwitch();
	void OnGuardStart();
	void OnGuardEnd();
	void OnLockToggle();
	void OnRestart();
	void OnTierPathbreaker();
	void OnTierHellwalker();
	void OnDebugToggle();
	void OnHelpToggle();
	void OnPause();
	void OnNotebook();
	void OnMap();
	void OnInteract();
	/** The title's / an ending's mode keys: 1 = a new walk in Normal, 2 = in Adaptive AI. */
	void OnChoice(int32 Choice);
	/** COMBAT or EXPLORE mappings for the current pawn (+ MENU while a menu is open). */
	void ApplyInputMode();
	bool IsOpenWorld() const;

	// ---- menus (HWPlayerControllerMenu.cpp) -----------------------------------------------------------
	void BuildMenuPage(EHWMenuPage Page, EHWSettingsTab Tab, int32 Slide, TArray<FHWMenuItem>& OutItems, FHWMenuPageInfo& OutInfo);
	void HandleMenuEvent(const FHWMenuEvent& Event);
	void HandleSettingChanged(const FHWMenuEvent& Event);
	/** Open a root page: pause when it is not the title, take the menu keys and show the cursor. */
	void OpenMenuRoot(EHWMenuPage Page, bool bPause);
	void PushSettings(EHWSettingsTab Tab);
	void MenuBack();
	void SetMenuInputActive(bool bActive);
	void ShowMenuMessage(const FString& Message, float Seconds = 3.5f);
	/** Leaving the tutorial (done, skipped, backed out of, or closed) marks it seen. */
	void NoteLeavingPage();
	void FinishCapture(const FKey& Key);
	void TickMenu(float DeltaSeconds);
	void OnMenuNavStarted(int32 Dir);
	void OnMenuNavCompleted(int32 Dir);
	void OnMenuScroll(const FInputActionValue& Value);
	void DoMenuNav(int32 Dir);
	void SelectMenuItem(FName Id);
	/** The arena's duel can be restarted (an encounter has begun). */
	bool CanRestartDuel() const;
	/** The map page, each frame: right stick / drag pan, triggers zoom; closes itself if exploring ends. */
	void TickMap(float DeltaSeconds);
	/** Zoom by Factor, keeping the map point under Anchor (screen pixels) where it is. */
	void ZoomMap(double Factor, const FVector2D& Anchor);

	FHWMenu Menu;
	bool bPausedByMenu = false;
	bool bMenuInput = false;
	bool bAutoTutorial = true;
	int32 NavHeld = 0;          // 1 up, 2 down, 3 left, 4 right
	float NavRepeatIn = 0.f;
	FString MenuMessage;
	float MenuMessageLeft = 0.f;
	bool bGamepadLast = false;
	/** The settings tab built last (-1: not on the settings page): entering Graphics re-reads the engine's settings. */
	int32 LastBuiltTab = -1;
	FDelegateHandle BindingsChangedHandle;
	// The map's view (UV centre, zoom) and where the HUD put it on screen.
	FVector2D MapCenter = FVector2D(0.5, 0.5);
	double MapZoom = 1.0;
	FVector2D MapOrigin = FVector2D::ZeroVector;
	double MapSide = 0.0;
	bool bMapDragging = false;
	FVector2D MapDragLast = FVector2D::ZeroVector;

	UPROPERTY() TObjectPtr<UInputMappingContext> Context;
	UPROPERTY() TObjectPtr<UInputAction> IA_Move;
	UPROPERTY() TObjectPtr<UInputAction> IA_Look;
	UPROPERTY() TObjectPtr<UInputAction> IA_Light;
	UPROPERTY() TObjectPtr<UInputAction> IA_Heavy;
	UPROPERTY() TObjectPtr<UInputAction> IA_Parry;
	UPROPERTY() TObjectPtr<UInputAction> IA_Step;
	UPROPERTY() TObjectPtr<UInputAction> IA_Switch;
	UPROPERTY() TObjectPtr<UInputAction> IA_Guard;
	UPROPERTY() TObjectPtr<UInputAction> IA_Lock;
	UPROPERTY() TObjectPtr<UInputAction> IA_Restart;
	UPROPERTY() TObjectPtr<UInputAction> IA_Tier1;
	UPROPERTY() TObjectPtr<UInputAction> IA_Tier2;
	UPROPERTY() TObjectPtr<UInputAction> IA_Debug;
	UPROPERTY() TObjectPtr<UInputAction> IA_Help;
	UPROPERTY() TObjectPtr<UInputAction> IA_Pause;
	UPROPERTY() TObjectPtr<UInputAction> IA_Notebook;
	UPROPERTY() TObjectPtr<UInputMappingContext> ExploreContext;
	UPROPERTY() TObjectPtr<UInputAction> IA_Interact;
	UPROPERTY() TObjectPtr<UInputAction> IA_Map;
	UPROPERTY() TObjectPtr<UInputAction> IA_Choice1;
	UPROPERTY() TObjectPtr<UInputAction> IA_Choice2;
	UPROPERTY() TObjectPtr<UInputMappingContext> MenuContext;
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuUp;
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuDown;
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuLeft;
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuRight;
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuAccept;
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuBack;
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuToggle;
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuTabPrev;
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuTabNext;
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuScroll;
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuPointer;
	/** The map key (and D-pad left) while a menu is open: closes the map page; ignored on the others. */
	UPROPERTY() TObjectPtr<UInputAction> IA_MenuMap;

	FVector2D MoveInput = FVector2D::ZeroVector;
	bool bLockedOn = true;
	float StrafeTestLeft = 0.f;
	float StrafeTestDuration = 0.f;
	float StrafeTolerance = 10.f;
	float StrafeWorstDegrees = 0.f;
	float StrafeYawTravelled = 0.f;
	float StrafeLastYaw = 0.f;
	bool bGuardDown = false;
};
