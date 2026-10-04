// HellwalkerRL — the player controller's menus: what each page holds (built from the settings, the save and the duel),
// what its items do, navigation from every device, key capture for rebinding, pausing. The model is FHWMenu (HWMenu.h);
// AHWHUD draws it.

#include "HWPlayerController.h"

#include "HellwalkerRL.h"
#include "HWCharacterBase.h"
#include "HWDuelSubsystem.h"
#include "HWOpenWorldGameMode.h"
#include "HWSaveGame.h"
#include "HWSessionSubsystem.h"
#include "HWTelemetry.h"
#include "HWSettings.h"
#include "HWWorldGen.h"
#include "Engine/World.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	namespace Ids
	{
		const FName Resume(TEXT("Resume"));
		const FName Settings(TEXT("Settings"));
		const FName Tutorial(TEXT("Tutorial"));
		const FName Notebook(TEXT("Notebook"));
		const FName OpenStats(TEXT("OpenStats"));
		const FName Restart(TEXT("Restart"));
		const FName QuitTitle(TEXT("QuitTitle"));
		const FName QuitDesktop(TEXT("QuitDesktop"));
		const FName Back(TEXT("Back"));
		const FName Continue(TEXT("Continue"));
		const FName SkipTutorial(TEXT("SkipTutorial"));
		const FName Difficulty(TEXT("Difficulty"));
		const FName ParryAssist(TEXT("ParryAssist"));
		const FName Sensitivity(TEXT("Sensitivity"));
		const FName InvertY(TEXT("InvertY"));
		const FName ResetBindings(TEXT("ResetBindings"));
		const FName Quality(TEXT("Quality"));
		const FName Resolution(TEXT("Resolution"));
		const FName WindowMode(TEXT("WindowMode"));
		const FName VSync(TEXT("VSync"));
		const FName FrameLimit(TEXT("FrameLimit"));
		const FName ApplyGraphics(TEXT("ApplyGraphics"));
		const FName Master(TEXT("Master"));
		const FName Music(TEXT("Music"));
		const FName Effects(TEXT("Effects"));
		const FName HudScale(TEXT("HudScale"));
		const FName ReadHold(TEXT("ReadHold"));
		const FName CameraShake(TEXT("CameraShake"));
		const FName ShowTutorial(TEXT("ShowTutorial"));
	}

	const TCHAR* BindPrefix = TEXT("Bind_");

	FName BindId(EHWBind A) { return FName(*FString::Printf(TEXT("%s%d"), BindPrefix, static_cast<int32>(A))); }

	/** "Open my stats page": the website's page of what the keepers learned about this copy of the game. */
	FHWMenuItem StatsItem(const UGameInstance* GI)
	{
		const UHWTelemetrySubsystem* T = GI != nullptr ? GI->GetSubsystem<UHWTelemetrySubsystem>() : nullptr;
		const FString Why = T != nullptr ? T->StatsPageUnavailableReason() : FString(TEXT("Stats sharing is not set up in this build."));
		FHWMenuItem I = FHWMenuItem::Action(Ids::OpenStats, TEXT("Open my stats page"),
			Why.IsEmpty() ? FString(TEXT("Your skills and what the keepers learned about you, on the website (opens your browser).")) : Why);
		I.bEnabled = Why.IsEmpty();
		return I;
	}

	bool ParseBindId(FName Id, EHWBind& Out)
	{
		const FString S = Id.ToString();
		if (!S.StartsWith(BindPrefix)) { return false; }
		const int32 I = FCString::Atoi(*S.RightChop(FCString::Strlen(BindPrefix)));
		if (I < 0 || I >= FHWBindingTable::Num) { return false; }
		Out = static_cast<EHWBind>(I);
		return true;
	}

	FString WindowModeName(int32 M)
	{
		switch (M)
		{
		case 0:  return TEXT("Fullscreen");
		case 1:  return TEXT("Borderless window");
		default: return TEXT("Windowed");
		}
	}

	FString FrameLimitName(float L) { return L > 0.f ? FString::Printf(TEXT("%.0f fps"), L) : FString(TEXT("Unlimited")); }
}

// =================================================================================================
// Pages
// =================================================================================================

void AHWPlayerController::BuildMenuPage(EHWMenuPage Page, EHWSettingsTab Tab, int32 Slide, TArray<FHWMenuItem>& OutItems, FHWMenuPageInfo& OutInfo)
{
	UHWSettingsSubsystem* S = GetSettings();
	AHWOpenWorldGameMode* GM = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr;
	const int32 BuiltTab = LastBuiltTab;
	LastBuiltTab = Page == EHWMenuPage::Settings ? static_cast<int32>(Tab) : -1;

	switch (Page)
	{
	case EHWMenuPage::Title:
	{
		OutInfo.Title = TEXT("HELLWALKER");
		const bool bSave = GM != nullptr && GM->HasSavedGame();
		if (bSave)
		{
			FString Hint = TEXT("Pick up the saved walk.");
			if (const UHWSaveGame* Existing = UHWSaveGame::LoadOrNull())
			{
				int32 Cleared = 0;
				for (int32 I = 0; I < 3; ++I) { Cleared += Existing->IsShrineCleared(I) ? 1 : 0; }
				Hint = FString::Printf(TEXT("%s  -  %d seal%s broken,  %d death%s"), *PlayModeName(Existing->Mode), Cleared, Cleared == 1 ? TEXT("") : TEXT("s"),
					Existing->Deaths, Existing->Deaths == 1 ? TEXT("") : TEXT("s"));
			}
			FHWMenuItem C = FHWMenuItem::Action(Ids::Continue, TEXT("Continue"), Hint);
			C.Shortcut = TEXT("Enter");
			OutItems.Add(C);
		}
		const FString Erase = bSave ? TEXT("Begin a new walk?  The saved walk will be erased.") : FString();
		HWTitle::AddNewWalkItems(OutItems, Erase); // [1] Normal, [2] Adaptive AI
		OutItems.Add(FHWMenuItem::Action(Ids::Settings, TEXT("Settings"), TEXT("Difficulty, parry assist, controls, graphics, audio, accessibility.")));
		OutItems.Add(FHWMenuItem::Action(Ids::Tutorial, TEXT("How the keeper learns you"), TEXT("Six short pages: the two modes, what it sees, what it remembers, what READ means.")));
		OutItems.Add(StatsItem(GetGameInstance()));
		FHWMenuItem Q = FHWMenuItem::Action(Ids::QuitDesktop, TEXT("Quit to desktop"), TEXT("The keepers forget you when you quit."));
		Q.Confirm = TEXT("Quit to the desktop?  The keepers will forget you.");
		OutItems.Add(Q);
		return;
	}

	case EHWMenuPage::Pause:
	{
		OutInfo.Title = TEXT("PAUSED");
		OutItems.Add(FHWMenuItem::Action(Ids::Resume, TEXT("Resume"), TEXT("Back to it.  The world has been holding still.")));
		OutItems.Add(FHWMenuItem::Action(Ids::Settings, TEXT("Settings"), TEXT("Difficulty, parry assist, controls, graphics, audio, accessibility.")));
		OutItems.Add(FHWMenuItem::Action(Ids::Tutorial, TEXT("How the keeper learns you"), TEXT("Six short pages: the two modes, what it sees, what it remembers, what READ means.")));
		OutItems.Add(FHWMenuItem::Action(Ids::Notebook, TEXT("The keeper's notebook"), TEXT("What the keepers have written down about you this session.")));
		OutItems.Add(StatsItem(GetGameInstance()));
		if (GM == nullptr)
		{
			FHWMenuItem R = FHWMenuItem::Action(Ids::Restart, TEXT("Restart duel"), TEXT("A fresh fight.  The keeper keeps what it has learned about you."));
			R.Confirm = TEXT("Restart the duel?  This fight is abandoned; the keeper still remembers you.");
			R.bEnabled = CanRestartDuel();
			OutItems.Add(R);
		}
		else
		{
			FHWMenuItem T = FHWMenuItem::Action(Ids::QuitTitle, TEXT("Quit to title"), TEXT("Your walk is saved at bells and shrines."));
			T.Confirm = TEXT("Return to the title?  The walk resumes from its last save.");
			OutItems.Add(T);
		}
		FHWMenuItem Q = FHWMenuItem::Action(Ids::QuitDesktop, TEXT("Quit to desktop"), TEXT("The keepers forget you when you quit."));
		Q.Confirm = TEXT("Quit to the desktop?  The keepers will forget you.");
		OutItems.Add(Q);
		return;
	}

	case EHWMenuPage::Tutorial:
	{
		OutInfo.Title = TEXT("HOW THE KEEPER LEARNS YOU");
		OutInfo.NumSlides = HWTutorial::NumSlides();
		const bool bLast = Slide >= OutInfo.NumSlides - 1;
		FHWMenuItem Prev = FHWMenuItem::Action(HWMenuIds::SlidePrev, TEXT("Previous"));
		Prev.bEnabled = Slide > 0;
		OutItems.Add(Prev);
		FHWMenuItem Next = FHWMenuItem::Action(HWMenuIds::SlideNext, bLast ? TEXT("Done") : TEXT("Next"));
		Next.Tone = EHWMenuTone::Crimson;
		OutItems.Add(Next);
		OutItems.Add(FHWMenuItem::Action(Ids::SkipTutorial, bLast ? TEXT("Close") : TEXT("Skip")));
		return;
	}

	case EHWMenuPage::Notebook:
		OutInfo.Title = TEXT("THE KEEPER'S NOTEBOOK");
		OutItems.Add(FHWMenuItem::Action(Ids::Back, TEXT("Back")));
		return;

	case EHWMenuPage::Map:
	{
		// One item per keeper (shrine order): accept tracks it. A fallen keeper stays on the map but cannot be tracked.
		OutInfo.Title = TEXT("THE VALLEY");
		if (GM == nullptr) { return; }
		TArray<AHWOpenWorldGameMode::FKeeperView> Keepers;
		GM->GetKeepers(Keepers);
		const int32 Tracked = GM->GetTrackedKeeper();
		for (const AHWOpenWorldGameMode::FKeeperView& K : Keepers)
		{
			FString Hint;
			if (K.bCleared) { Hint = TEXT("Its seal is broken.  Nothing waits there now."); }
			else if (K.Index == Tracked) { Hint = TEXT("Tracked: its mark stays on your screen at any distance, and at the screen's edge when it is behind you."); }
			else if (K.bSealed) { Hint = TEXT("Sealed until the other keepers fall.  Accept to track it anyway."); }
			else { Hint = TEXT("Accept to track this keeper: its mark stays on your screen at any distance."); }
			FHWMenuItem I = FHWMenuItem::Action(TrackId(K.Index), K.Title, Hint);
			I.bEnabled = !K.bCleared;
			I.Tone = K.Index == Tracked ? EHWMenuTone::Gold : (K.bSealed ? EHWMenuTone::Dim : EHWMenuTone::Normal);
			OutItems.Add(I);
		}
		return;
	}

	case EHWMenuPage::Settings:
		break;

	default:
		return;
	}

	// ---- settings ---------------------------------------------------------------------------------
	OutInfo.Title = TEXT("SETTINGS");
	OutInfo.bTabs = true;
	if (S == nullptr) { return; }
	switch (Tab)
	{
	case EHWSettingsTab::Gameplay:
	{
		OutItems.Add(FHWMenuItem::Header(TEXT("THE KEEPER")));
		TArray<FString> Names;
		for (int32 D = 0; D < UHWSettingsSubsystem::NumMenuDifficulties; ++D) { Names.Add(UHWSettingsSubsystem::DifficultyName(static_cast<EHWDifficulty>(D))); }
		OutItems.Add(FHWMenuItem::Choice(Ids::Difficulty, TEXT("Difficulty"), Names,
			FMath::Clamp(static_cast<int32>(S->GetDifficulty()), 0, UHWSettingsSubsystem::NumMenuDifficulties - 1),
			UHWSettingsSubsystem::DifficultyBlurb(S->GetDifficulty()) + TEXT("  (From the next fight.)")));
		TArray<FString> Assists;
		for (EHWParryAssist A : { EHWParryAssist::RingSlow, EHWParryAssist::Ring, EHWParryAssist::Off }) { Assists.Add(UHWSettingsSubsystem::ParryAssistName(A)); }
		FString AssistHint = UHWSettingsSubsystem::ParryAssistBlurb(S->GetParryAssist()) + TEXT("  (From the next fight.)");
		if (S->HasParryAssistOverride()) { AssistHint += TEXT("  Set by -HWParryAssist for this session."); }
		OutItems.Add(FHWMenuItem::Choice(Ids::ParryAssist, TEXT("Parry assist"), Assists, static_cast<int32>(S->GetParryAssist()), AssistHint));
		OutItems.Add(FHWMenuItem::Header(TEXT("CAMERA")));
		FHWMenuItem Sens = FHWMenuItem::Slider(Ids::Sensitivity, TEXT("Mouse sensitivity"), S->GetMouseSensitivity(), FHWSettingsData::MinSensitivity,
			FHWSettingsData::MaxSensitivity, 0.05f, TEXT("How far the camera turns per inch of mouse (exploring, or with the lock-on off)."));
		Sens.Suffix = TEXT("x");
		OutItems.Add(Sens);
		OutItems.Add(FHWMenuItem::Toggle(Ids::InvertY, TEXT("Invert look up / down"), S->GetInvertY(), TEXT("Mouse and right stick alike.")));
		return;
	}

	case EHWSettingsTab::Controls:
	{
		const FHWBindingTable& B = S->GetBindings();
		auto Row = [&](EHWBind A)
		{
			const FString Pad = FHWBindingTable::GamepadLabel(A);
			OutItems.Add(FHWMenuItem::Binding(BindId(A), FHWBindingTable::ActionLabel(A), FHWBindingTable::KeyLabel(B.Get(A)), Pad, false,
				FString::Printf(TEXT("Accept, then press the new key or mouse button (Esc cancels).  Gamepad: %s (fixed)."), *Pad)));
		};
		OutItems.Add(FHWMenuItem::Header(TEXT("THE DUEL")));
		for (EHWBind A : { EHWBind::Light, EHWBind::Heavy, EHWBind::Parry, EHWBind::Guard, EHWBind::Step, EHWBind::Switch, EHWBind::LockOn, EHWBind::Restart }) { Row(A); }
		OutItems.Add(FHWMenuItem::Header(TEXT("EVERYWHERE")));
		for (EHWBind A : { EHWBind::Pause, EHWBind::Notebook, EHWBind::Help, EHWBind::Debug }) { Row(A); }
		OutItems.Add(FHWMenuItem::Header(TEXT("EXPLORING")));
		Row(EHWBind::Interact);
		Row(EHWBind::Map);
		FHWMenuItem Reset = FHWMenuItem::Action(Ids::ResetBindings, TEXT("Reset to defaults"), TEXT("Every keyboard and mouse key back to how it shipped."));
		Reset.Confirm = TEXT("Put every key back to its default?");
		Reset.Tone = EHWMenuTone::Gold;
		OutItems.Add(Reset);
		OutItems.Add(FHWMenuItem::Header(TEXT("FIXED")));
		auto Fixed = [&](const TCHAR* Label, const TCHAR* Key, const TCHAR* Pad, const TCHAR* Hint)
		{
			OutItems.Add(FHWMenuItem::Binding(NAME_None, Label, Key, Pad, true, Hint));
		};
		Fixed(TEXT("Move"), TEXT("W A S D"), TEXT("left stick"), TEXT("Movement stays on W A S D and the left stick."));
		Fixed(TEXT("Look"), TEXT("Mouse"), TEXT("right stick"), TEXT("The lock-on owns the camera in a duel."));
		Fixed(TEXT("Menu / back"), TEXT("Esc"), TEXT("Start / B"), TEXT("Esc and Start always open the menu; Esc and B go back."));
		Fixed(TEXT("Choose mode (title, arena)"), TEXT("1 / 2"), TEXT("D-pad left / right"), TEXT("1 / 2 pick the mode: a new walk at the title (named there), a fight on the arena's start screen (named there)."));
		return;
	}

	case EHWSettingsTab::Graphics:
	{
		if (BuiltTab != static_cast<int32>(EHWSettingsTab::Graphics)) { S->LoadGraphics(); } // entering the tab: start from what the engine has
		const FHWGraphicsChoice& G = S->PendingGraphics();
		const FString Pending = TEXT("Takes effect on Apply.");
		TArray<FString> Q;
		for (int32 L = 0; L <= 4; ++L) { Q.Add(UHWSettingsSubsystem::QualityName(L)); }
		if (G.Quality < 0) { Q.Add(UHWSettingsSubsystem::QualityName(-1)); }
		OutItems.Add(FHWMenuItem::Choice(Ids::Quality, TEXT("Overall quality"), Q, G.Quality < 0 ? 5 : G.Quality,
			TEXT("Scalability: shadows, effects, view distance, foliage, post-processing.  ") + Pending));
		TArray<FString> Res;
		int32 ResIndex = 0;
		const TArray<FIntPoint>& All = S->GetResolutions();
		for (int32 I = 0; I < All.Num(); ++I)
		{
			Res.Add(FString::Printf(TEXT("%d x %d"), All[I].X, All[I].Y));
			if (All[I] == G.Resolution) { ResIndex = I; }
		}
		if (Res.Num() == 0) { Res.Add(FString::Printf(TEXT("%d x %d"), G.Resolution.X, G.Resolution.Y)); }
		OutItems.Add(FHWMenuItem::Choice(Ids::Resolution, TEXT("Resolution"), Res, ResIndex, Pending));
		OutItems.Add(FHWMenuItem::Choice(Ids::WindowMode, TEXT("Window mode"), { WindowModeName(0), WindowModeName(1), WindowModeName(2) }, G.WindowMode, Pending));
		OutItems.Add(FHWMenuItem::Toggle(Ids::VSync, TEXT("VSync"), G.bVSync, TEXT("Locks to the display's refresh: no tearing, a little more latency.  ") + Pending));
		TArray<FString> Lim;
		for (float L : UHWSettingsSubsystem::FrameLimits()) { Lim.Add(FrameLimitName(L)); }
		OutItems.Add(FHWMenuItem::Choice(Ids::FrameLimit, TEXT("Frame rate limit"), Lim, G.FrameLimit,
			TEXT("The duel runs at a fixed 60 steps per second whatever the frame rate.  ") + Pending));
		const bool bDirty = S->HasUnappliedGraphics();
		FHWMenuItem Apply = FHWMenuItem::Action(Ids::ApplyGraphics, bDirty ? TEXT("Apply  (unapplied changes)") : TEXT("Apply"),
			TEXT("Apply and save the graphics settings."));
		Apply.Tone = bDirty ? EHWMenuTone::Gold : EHWMenuTone::Normal;
		OutItems.Add(Apply);
		return;
	}

	case EHWSettingsTab::Audio:
	{
		auto Vol = [&](FName Id, const TCHAR* Label, float V, const TCHAR* Hint)
		{
			FHWMenuItem I = FHWMenuItem::Slider(Id, Label, V, 0.f, 1.f, 0.05f, Hint);
			I.bPercent = true;
			OutItems.Add(I);
		};
		Vol(Ids::Master, TEXT("Master volume"), S->GetMasterVolume(), TEXT("Everything you hear."));
		Vol(Ids::Music, TEXT("Music"), S->GetMusicVolume(), TEXT("The score."));
		Vol(Ids::Effects, TEXT("Effects"), S->GetEffectsVolume(), TEXT("Blades, steps, bells, the keepers."));
		return;
	}

	case EHWSettingsTab::Accessibility:
	{
		FHWMenuItem Scale = FHWMenuItem::Slider(Ids::HudScale, TEXT("HUD scale"), S->GetHudScale(), FHWSettingsData::MinHudScale, FHWSettingsData::MaxHudScale, 0.05f,
			TEXT("The size of everything drawn over the game: bars, text, these menus."));
		Scale.bPercent = true;
		OutItems.Add(Scale);
		FHWMenuItem Hold = FHWMenuItem::Slider(Ids::ReadHold, TEXT("READ banner hold"), S->GetReadHoldSeconds(), FHWSettingsData::MinReadHold, FHWSettingsData::MaxReadHold,
			0.1f, TEXT("How long READ stays up after a counter lands on the answer it predicted."));
		Hold.Decimals = 1;
		Hold.Suffix = TEXT(" s");
		OutItems.Add(Hold);
		OutItems.Add(FHWMenuItem::Toggle(Ids::CameraShake, TEXT("Camera shake"), S->GetCameraShake(), TEXT("Shake on heavy hits and parries.")));
		OutItems.Add(FHWMenuItem::Action(Ids::ShowTutorial, TEXT("Show the tutorial again"), TEXT("Opens \"How the keeper learns you\" now (it is also in the pause menu).")));
		return;
	}

	default:
		return;
	}
}

// =================================================================================================
// Opening, closing, pausing
// =================================================================================================

bool AHWPlayerController::CanRestartDuel() const
{
	const UHWDuelSubsystem* D = GetDuel();
	return D != nullptr && D->GetState() != EHWEncounterState::WaitingToStart && !IsOpenWorld();
}

void AHWPlayerController::OpenMenuRoot(EHWMenuPage Page, bool bPause)
{
	LastBuiltTab = -1;
	Menu.Open(Page);
	if (bPause && !UGameplayStatics::IsGamePaused(this))
	{
		bPausedByMenu = UGameplayStatics::SetGamePaused(this, true);
	}
	SetMenuInputActive(true);
	UE_LOG(LogHellwalkerRL, Log, TEXT("Menu: %s%s."), Page == EHWMenuPage::Title ? TEXT("title")
		: (Page == EHWMenuPage::Tutorial ? TEXT("tutorial") : (Page == EHWMenuPage::Map ? TEXT("the valley map") : TEXT("pause"))),
		bPausedByMenu ? TEXT(" (paused)") : TEXT(""));
}

void AHWPlayerController::OpenPauseMenu()
{
	const AHWOpenWorldGameMode* GM = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr;
	if (GM != nullptr && GM->GetPhase() == EHWWorldPhase::Title)
	{
		OpenMenuRoot(EHWMenuPage::Title, false);
		return;
	}
	OpenMenuRoot(EHWMenuPage::Pause, true);
}

void AHWPlayerController::CloseMenu()
{
	NoteLeavingPage();
	const AHWOpenWorldGameMode* GM = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr;
	if (GM != nullptr && GM->GetPhase() == EHWWorldPhase::Title)
	{
		// The title is a menu: "closing" it goes back to its root.
		if (Menu.GetRootPage() != EHWMenuPage::Title || Menu.GetDepth() > 1) { OpenMenuRoot(EHWMenuPage::Title, false); }
		return;
	}
	Menu.Close();
	LastBuiltTab = -1;
	if (bPausedByMenu)
	{
		UGameplayStatics::SetGamePaused(this, false);
		bPausedByMenu = false;
	}
	SetMenuInputActive(false);
}

void AHWPlayerController::SetMenuInputActive(bool bActive)
{
	bMenuInput = bActive;
	NavHeld = 0;
	if (bActive)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		SetShowMouseCursor(true);
		bEnableClickEvents = true;
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
		bEnableClickEvents = false;
	}
	// Whatever was held when the menu took the keys is let go: no guard left up, no feet left walking.
	MoveInput = FVector2D::ZeroVector;
	if (bGuardDown) { OnGuardEnd(); }
	ApplyInputMode();
}

void AHWPlayerController::NoteLeavingPage()
{
	if (!Menu.IsOpen()) { return; }
	if (Menu.GetPage() == EHWMenuPage::Tutorial || Menu.GetRootPage() == EHWMenuPage::Tutorial)
	{
		if (UHWSettingsSubsystem* S = GetSettings())
		{
			if (!S->IsTutorialSeen()) { S->SetTutorialSeen(true); }
		}
	}
	if (Menu.GetPage() == EHWMenuPage::Settings)
	{
		const UHWSettingsSubsystem* S = GetSettings();
		if (S != nullptr && Menu.GetTab() == EHWSettingsTab::Graphics && S->HasUnappliedGraphics()) { ShowMenuMessage(TEXT("Graphics changes were not applied.")); }
		LastBuiltTab = -1;
	}
}

void AHWPlayerController::PushSettings(EHWSettingsTab Tab)
{
	LastBuiltTab = -1;
	Menu.Push(EHWMenuPage::Settings, Tab);
}

void AHWPlayerController::MenuBack()
{
	if (!Menu.IsOpen()) { return; }
	if (Menu.IsCapturing() || Menu.IsConfirming())
	{
		Menu.Back();
		return;
	}
	if (Menu.GetDepth() <= 1)
	{
		if (Menu.GetPage() == EHWMenuPage::Title)
		{
			SelectMenuItem(Ids::QuitDesktop); // Esc at the title: offer the way out
			return;
		}
		CloseMenu();
		return;
	}
	NoteLeavingPage();
	Menu.Back();
}

void AHWPlayerController::SelectMenuItem(FName Id)
{
	const int32 I = Menu.FindItem(Id);
	if (I != INDEX_NONE) { Menu.Hover(I); }
}

void AHWPlayerController::ShowMenuMessage(const FString& Message, float Seconds)
{
	MenuMessage = Message;
	MenuMessageLeft = Seconds;
}

bool AHWPlayerController::OpenMenuPage(const FString& Name)
{
	const FString N = Name.ToLower();
	if (N == TEXT("close")) { CloseMenu(); return true; }
	struct FTab { const TCHAR* Name; EHWSettingsTab Tab; };
	const FTab Tabs[] = {
		{ TEXT("settings"), EHWSettingsTab::Gameplay }, { TEXT("gameplay"), EHWSettingsTab::Gameplay }, { TEXT("controls"), EHWSettingsTab::Controls },
		{ TEXT("graphics"), EHWSettingsTab::Graphics }, { TEXT("audio"), EHWSettingsTab::Audio }, { TEXT("accessibility"), EHWSettingsTab::Accessibility },
	};
	if (N == TEXT("pause") || N == TEXT("title"))
	{
		OpenPauseMenu(); // the title's menu at the open world's title
		return true;
	}
	for (const FTab& T : Tabs)
	{
		if (N == T.Name)
		{
			OpenPauseMenu();
			PushSettings(T.Tab);
			return true;
		}
	}
	if (N == TEXT("tutorial") || N == TEXT("notebook"))
	{
		OpenPauseMenu();
		Menu.Push(N == TEXT("tutorial") ? EHWMenuPage::Tutorial : EHWMenuPage::Notebook);
		return true;
	}
	if (N == TEXT("map")) { return OpenMap(); }
	UE_LOG(LogHellwalkerRL, Warning, TEXT("hw.Menu: unknown page '%s' (pause | settings | controls | graphics | audio | accessibility | tutorial | notebook | map | close)."), *Name);
	return false;
}

// =================================================================================================
// The valley map
// =================================================================================================

FName AHWPlayerController::TrackId(int32 Keeper)
{
	return FName(*FString::Printf(TEXT("Track_%d"), Keeper));
}

int32 AHWPlayerController::ParseTrackId(FName Id)
{
	const FString S = Id.ToString();
	return S.StartsWith(TEXT("Track_")) ? FCString::Atoi(*S.RightChop(6)) : -1;
}

bool AHWPlayerController::OpenMap()
{
	AHWOpenWorldGameMode* GM = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr;
	if (GM == nullptr || !GM->CanOpenMap())
	{
		UE_LOG(LogHellwalkerRL, Log, TEXT("Map: only while exploring the open world."));
		return false;
	}
	// Centred on the player, at the zoom it was left at.
	FVector At = FVector::ZeroVector;
	float Yaw = 0.f;
	if (GM->GetPlayerSpot(At, Yaw)) { MapCenter = HWMap::WorldToUV(FVector2D(At.X, At.Y), FHWWorldGen::HalfExtent); }
	HWMap::ClampView(MapCenter, MapZoom);
	bMapDragging = false;
	if (!IsMapOpen()) { OpenMenuRoot(EHWMenuPage::Map, true); }
	SelectMenuItem(TrackId(GM->GetTrackedKeeper()));
	return true;
}

HWMap::FView AHWPlayerController::GetMapView() const
{
	HWMap::FView V;
	V.Origin = MapOrigin;
	V.Side = FMath::Max(MapSide, 1.0);
	V.Center = MapCenter;
	V.Zoom = MapZoom;
	return V;
}

void AHWPlayerController::ZoomMap(double Factor, const FVector2D& Anchor)
{
	if (MapSide <= 1.0)
	{
		// Not drawn yet: nothing on screen to keep in place.
		MapZoom *= Factor;
		HWMap::ClampView(MapCenter, MapZoom);
		return;
	}
	const HWMap::FView Before = GetMapView();
	const FVector2D Middle = MapOrigin + FVector2D(MapSide * 0.5, MapSide * 0.5);
	FVector2D At = Anchor;
	if (!Before.Contains(At))
	{
		// No cursor on the map (the triggers, the console): zoom on the player while the map shows them, else on its middle.
		At = Middle;
		const AHWOpenWorldGameMode* GM = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr;
		FVector Spot = FVector::ZeroVector;
		float Yaw = 0.f;
		if (GM != nullptr && GM->GetPlayerSpot(Spot, Yaw))
		{
			const FVector2D OnMap = Before.UVToScreen(HWMap::WorldToUV(FVector2D(Spot.X, Spot.Y), FHWWorldGen::HalfExtent));
			if (Before.Contains(OnMap)) { At = OnMap; }
		}
	}
	// Keep the point under the anchor under the anchor.
	const FVector2D Under = Before.ScreenToUV(At);
	MapZoom *= Factor;
	HWMap::ClampView(MapCenter, MapZoom);
	MapCenter = Under - (At - Middle) / (MapSide * MapZoom);
	HWMap::ClampView(MapCenter, MapZoom);
}

void AHWPlayerController::TickMap(float DeltaSeconds)
{
	const AHWOpenWorldGameMode* GM = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr;
	if (GM == nullptr || !GM->CanOpenMap())
	{
		CloseMenu(); // exploring ended under it (a scripted duel): the map has nothing to show
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	// Gamepad: the right stick pans (a window-width in ~1.4 s), the triggers zoom.
	const FVector2D Stick(GetInputAnalogKeyState(EKeys::Gamepad_RightX), GetInputAnalogKeyState(EKeys::Gamepad_RightY));
	if (Stick.SizeSquared() > 0.04)
	{
		MapCenter += FVector2D(Stick.X, -Stick.Y) * (0.7 / MapZoom) * Dt;
		HWMap::ClampView(MapCenter, MapZoom);
	}
	const float Triggers = GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) - GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis);
	if (FMath::Abs(Triggers) > 0.1f) { ZoomMap(FMath::Exp(1.6 * Triggers * Dt), FVector2D(-1.0, -1.0)); }
	// The mouse: drag (either button) to pan; a click on a keeper is the HUD's hit box.
	float MX = 0.f;
	float MY = 0.f;
	const bool bMouse = GetMousePosition(MX, MY);
	const FVector2D Mouse(MX, MY);
	const bool bHeld = IsInputKeyDown(EKeys::LeftMouseButton) || IsInputKeyDown(EKeys::RightMouseButton);
	if (bMouse && bHeld && MapSide > 1.0)
	{
		if (!bMapDragging)
		{
			bMapDragging = GetMapView().Contains(Mouse);
			MapDragLast = Mouse;
		}
		else
		{
			MapCenter -= (Mouse - MapDragLast) / (MapSide * MapZoom);
			HWMap::ClampView(MapCenter, MapZoom);
			MapDragLast = Mouse;
		}
	}
	else
	{
		bMapDragging = false;
	}
}

void AHWPlayerController::MapConsole(const TArray<FString>& Args)
{
	AHWOpenWorldGameMode* GM = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr;
	const FString Verb = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("toggle"));
	if (Verb == TEXT("open")) { OpenMap(); }
	else if (Verb == TEXT("close")) { if (IsMapOpen()) { CloseMenu(); } }
	else if (Verb == TEXT("toggle"))
	{
		if (IsMapOpen()) { CloseMenu(); }
		else if (!Menu.IsOpen()) { OpenMap(); }
	}
	else if (Verb == TEXT("track") && GM != nullptr)
	{
		const bool bAuto = Args.Num() < 2 || Args[1].Equals(TEXT("auto"), ESearchCase::IgnoreCase);
		GM->SetTrackedKeeper(bAuto ? -1 : FCString::Atoi(*Args[1]));
		if (IsMapOpen()) { Menu.Refresh(); SelectMenuItem(TrackId(GM->GetTrackedKeeper())); }
	}
	else if (Verb == TEXT("zoom") && Args.Num() > 1)
	{
		const double Want = FMath::Clamp(FCString::Atod(*Args[1]), HWMap::MinZoom, HWMap::MaxZoom);
		ZoomMap(Want / FMath::Max(MapZoom, HWMap::MinZoom), FVector2D(-1.0, -1.0)); // on the player
	}
	else
	{
		UE_LOG(LogHellwalkerRL, Warning, TEXT("hw.Map: unknown '%s' (open | close | track <0|1|2|auto> | zoom <1-4>)."), *Verb);
	}
}

// =================================================================================================
// Events
// =================================================================================================

void AHWPlayerController::HandleMenuEvent(const FHWMenuEvent& Event)
{
	AHWOpenWorldGameMode* GM = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr;
	UHWSettingsSubsystem* S = GetSettings();
	switch (Event.Type)
	{
	case EHWMenuEventType::Changed:
		HandleSettingChanged(Event);
		Menu.Refresh();
		return;

	case EHWMenuEventType::Capture:
	{
		EHWBind A = EHWBind::Light;
		if (ParseBindId(Event.Id, A)) { ShowMenuMessage(FString::Printf(TEXT("Press a key for %s  -  Esc cancels."), *FHWBindingTable::ActionLabel(A)), 30.f); }
		return;
	}

	case EHWMenuEventType::Activated:
	case EHWMenuEventType::Confirmed:
		break;

	default:
		return;
	}

	const FName Id = Event.Id;
	if (const int32 Keeper = ParseTrackId(Id); Keeper >= 0)
	{
		// The map: track this keeper (the map stays open; M / Esc closes it).
		if (GM != nullptr)
		{
			GM->SetTrackedKeeper(Keeper);
			TArray<AHWOpenWorldGameMode::FKeeperView> Keepers;
			GM->GetKeepers(Keepers);
			const AHWOpenWorldGameMode::FKeeperView* K = Keepers.FindByPredicate([Keeper](const AHWOpenWorldGameMode::FKeeperView& V) { return V.Index == Keeper; });
			if (K != nullptr) { ShowMenuMessage(FString::Printf(TEXT("Tracking: %s"), *K->Title)); }
		}
		Menu.Refresh();
		return;
	}
	if (Id == Ids::Resume) { CloseMenu(); }
	else if (Id == Ids::Settings) { PushSettings(EHWSettingsTab::Gameplay); }
	else if (Id == Ids::Tutorial || Id == Ids::ShowTutorial) { Menu.Push(EHWMenuPage::Tutorial); }
	else if (Id == Ids::Notebook) { Menu.Push(EHWMenuPage::Notebook); }
	else if (Id == Ids::OpenStats)
	{
		const UHWTelemetrySubsystem* T = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UHWTelemetrySubsystem>() : nullptr;
		if (T != nullptr) { T->OpenStatsPage(); }
	}
	else if (Id == Ids::Back) { MenuBack(); }
	else if (Id == HWMenuIds::SlidesDone || Id == Ids::SkipTutorial)
	{
		// Done or skipped: the tutorial is seen. Opened on its own (before the first duel) it closes and the fight begins.
		if (Menu.GetDepth() <= 1) { CloseMenu(); }
		else { MenuBack(); }
	}
	else if (Id == Ids::Continue)
	{
		if (GM != nullptr && GM->ContinueGame()) { CloseMenu(); }
		else { ShowMenuMessage(TEXT("There is no saved walk to continue.")); }
	}
	else if (Id == HWMenuIds::NewNormal || Id == HWMenuIds::NewAdaptive)
	{
		if (GM != nullptr)
		{
			GM->NewGame(Id == HWMenuIds::NewNormal ? EHWPlayMode::Pathbreaker : EHWPlayMode::Hellwalker);
			CloseMenu();
		}
	}
	else if (Id == Ids::Restart)
	{
		CloseMenu();
		if (UHWDuelSubsystem* D = GetDuel())
		{
			if (D->GetState() != EHWEncounterState::WaitingToStart) { D->ResetEncounter(-1); }
		}
	}
	else if (Id == Ids::QuitTitle)
	{
		CloseMenu();
		UGameplayStatics::SetGamePaused(this, false);
		UE_LOG(LogHellwalkerRL, Log, TEXT("Menu: back to the title."));
		UGameplayStatics::OpenLevel(this, FName(*GetWorld()->GetOutermost()->GetName()));
	}
	else if (Id == Ids::QuitDesktop)
	{
		UE_LOG(LogHellwalkerRL, Log, TEXT("Menu: quit to the desktop."));
		ConsoleCommand(TEXT("quit"));
	}
	else if (Id == Ids::ResetBindings)
	{
		if (S != nullptr) { S->ResetBindings(); }
		ShowMenuMessage(TEXT("Every key is back to its default."));
		Menu.Refresh();
	}
	else if (Id == Ids::ApplyGraphics)
	{
		if (S != nullptr)
		{
			const bool bDirty = S->HasUnappliedGraphics();
			S->ApplyGraphics();
			ShowMenuMessage(bDirty ? TEXT("Graphics applied and saved.") : TEXT("Nothing to apply."));
		}
		Menu.Refresh();
	}
}

void AHWPlayerController::HandleSettingChanged(const FHWMenuEvent& Event)
{
	UHWSettingsSubsystem* S = GetSettings();
	if (S == nullptr) { return; }
	const FName Id = Event.Id;
	if (Id == Ids::Difficulty)
	{
		const EHWDifficulty D = static_cast<EHWDifficulty>(FMath::Clamp(Event.Index, 0, UHWSettingsSubsystem::NumMenuDifficulties - 1));
		S->SetDifficulty(D);
		ShowMenuMessage(FString::Printf(TEXT("Difficulty: %s  -  from the next fight."), *UHWSettingsSubsystem::DifficultyName(D)));
	}
	else if (Id == Ids::ParryAssist)
	{
		const EHWParryAssist A = static_cast<EHWParryAssist>(FMath::Clamp(Event.Index, 0, static_cast<int32>(EHWParryAssist::Off)));
		S->SetParryAssist(A);
		ShowMenuMessage(FString::Printf(TEXT("Parry assist: %s  -  from the next fight."), *UHWSettingsSubsystem::ParryAssistName(A)));
	}
	else if (Id == Ids::Sensitivity) { S->SetMouseSensitivity(Event.Value); }
	else if (Id == Ids::InvertY) { S->SetInvertY(Event.bOn); }
	else if (Id == Ids::Quality) { S->PendingGraphics().Quality = Event.Index >= 5 ? -1 : Event.Index; }
	else if (Id == Ids::Resolution)
	{
		if (S->GetResolutions().IsValidIndex(Event.Index)) { S->PendingGraphics().Resolution = S->GetResolutions()[Event.Index]; }
	}
	else if (Id == Ids::WindowMode) { S->PendingGraphics().WindowMode = static_cast<uint8>(FMath::Clamp(Event.Index, 0, 2)); }
	else if (Id == Ids::VSync) { S->PendingGraphics().bVSync = Event.bOn; }
	else if (Id == Ids::FrameLimit) { S->PendingGraphics().FrameLimit = Event.Index; }
	else if (Id == Ids::Master) { S->SetMasterVolume(Event.Value); }
	else if (Id == Ids::Music) { S->SetMusicVolume(Event.Value); }
	else if (Id == Ids::Effects) { S->SetEffectsVolume(Event.Value); }
	else if (Id == Ids::HudScale) { S->SetHudScale(Event.Value); }
	else if (Id == Ids::ReadHold) { S->SetReadHoldSeconds(Event.Value); }
	else if (Id == Ids::CameraShake) { S->SetCameraShake(Event.bOn); }
}

void AHWPlayerController::FinishCapture(const FKey& Key)
{
	if (!Menu.IsCapturing()) { return; }
	EHWBind A = EHWBind::Light;
	const bool bAction = ParseBindId(Menu.GetCaptureId(), A);
	Menu.EndCapture();
	if (!bAction) { return; }
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
	{
		ShowMenuMessage(FString::Printf(TEXT("%s stays on %s."), *FHWBindingTable::ActionLabel(A), *FHWBindingTable::KeyLabel(GetSettings() != nullptr ? GetSettings()->GetBindings().Get(A) : FKey())));
		return;
	}
	FString Message;
	if (UHWSettingsSubsystem* S = GetSettings()) { S->Rebind(A, Key, Message); }
	ShowMenuMessage(Message, 5.f);
	Menu.Refresh();
}

// =================================================================================================
// Navigation
// =================================================================================================

void AHWPlayerController::MenuCommand(const FString& Command)
{
	const FString C = Command.ToLower();
	if (C == TEXT("open")) { if (!Menu.IsOpen()) { OpenPauseMenu(); } return; }
	if (!Menu.IsOpen()) { return; }
	if (C == TEXT("up")) { DoMenuNav(1); }
	else if (C == TEXT("down")) { DoMenuNav(2); }
	else if (C == TEXT("left")) { DoMenuNav(3); }
	else if (C == TEXT("right")) { DoMenuNav(4); }
	else if (C == TEXT("accept")) { HandleMenuEvent(Menu.Accept()); }
	else if (C == TEXT("back")) { MenuBack(); }
	else if (C == TEXT("toggle")) { CloseMenu(); } // Start / P: straight back to the game (the title stays)
	else if (C == TEXT("tabnext")) { Menu.SwitchTab(+1); }
	else if (C == TEXT("tabprev")) { Menu.SwitchTab(-1); }
	else if (C == TEXT("map")) { if (IsMapOpen()) { CloseMenu(); } } // the map key: closes the map, nothing on the other pages
	else { UE_LOG(LogHellwalkerRL, Warning, TEXT("hw.MenuNav: unknown '%s' (up | down | left | right | accept | back | tabnext | tabprev | toggle | map)."), *Command); }
}

void AHWPlayerController::DoMenuNav(int32 Dir)
{
	if (!Menu.IsOpen() || Menu.IsCapturing()) { return; }
	switch (Dir)
	{
	case 1: Menu.Move(-1); break;
	case 2: Menu.Move(+1); break;
	case 3: HandleMenuEvent(Menu.Adjust(-1)); break;
	case 4: HandleMenuEvent(Menu.Adjust(+1)); break;
	default: break;
	}
}

void AHWPlayerController::OnMenuNavStarted(int32 Dir)
{
	if (!Menu.IsOpen()) { return; }
	DoMenuNav(Dir);
	NavHeld = Dir;
	NavRepeatIn = 0.38f;
}

void AHWPlayerController::OnMenuNavCompleted(int32 Dir)
{
	if (NavHeld == Dir) { NavHeld = 0; }
}

void AHWPlayerController::OnMenuScroll(const FInputActionValue& Value)
{
	const float V = Value.Get<float>();
	if (!Menu.IsOpen() || Menu.IsConfirming() || FMath::IsNearlyZero(V)) { return; }
	if (IsMapOpen())
	{
		// The map: the wheel zooms, at the cursor.
		float MX = -1.f;
		float MY = -1.f;
		GetMousePosition(MX, MY);
		ZoomMap(V > 0.f ? 1.25 : 0.8, FVector2D(MX, MY));
		return;
	}
	Menu.Move(V > 0.f ? -1 : +1);
}

void AHWPlayerController::MenuPointer(FName Box, bool bClick)
{
	TArray<FString> P;
	Box.ToString().ParseIntoArray(P, TEXT(":"));
	if (P.Num() != 3 || P[0] != TEXT("M") || !Menu.IsOpen() || Menu.IsCapturing()) { return; }
	const int32 N = FCString::Atoi(*P[2]);
	const TCHAR What = P[1].Len() > 0 ? P[1][0] : TEXT('?');
	switch (What)
	{
	case TEXT('I'):
		if (bClick) { HandleMenuEvent(Menu.Click(N)); }
		else { Menu.Hover(N); }
		break;
	case TEXT('L'):
	case TEXT('R'):
		Menu.Hover(N);
		if (bClick && Menu.GetSelected() == N) { HandleMenuEvent(Menu.Adjust(What == TEXT('L') ? -1 : +1)); }
		break;
	case TEXT('T'):
		if (bClick && N >= 0 && N < static_cast<int32>(EHWSettingsTab::Count)) { Menu.SetTab(static_cast<EHWSettingsTab>(N)); }
		break;
	case TEXT('C'):
		if (bClick) { HandleMenuEvent(Menu.ClickConfirm(N)); }
		else { Menu.HoverConfirm(N); }
		break;
	case TEXT('S'):
		if (bClick) { Menu.SetSlide(N); }
		break;
	default:
		break;
	}
}

void AHWPlayerController::TickMenu(float DeltaSeconds)
{
	MenuMessageLeft = FMath::Max(0.f, MenuMessageLeft - DeltaSeconds);
	if (!Menu.IsCapturing() && MenuMessage.StartsWith(TEXT("Press a key")) && MenuMessageLeft > 0.f) { MenuMessageLeft = 0.f; }

	// Held direction: repeat (keyboard, D-pad, left stick).
	if (NavHeld != 0 && Menu.IsOpen() && !Menu.IsCapturing())
	{
		NavRepeatIn -= DeltaSeconds;
		if (NavRepeatIn <= 0.f)
		{
			DoMenuNav(NavHeld);
			NavRepeatIn = 0.085f;
		}
	}

	if (IsMapOpen()) { TickMap(DeltaSeconds); }

	// The open world's title is a menu, open whenever the title is.
	if (const AHWOpenWorldGameMode* GM = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr)
	{
		const bool bTitle = GM->GetPhase() == EHWWorldPhase::Title;
		if (bTitle && Menu.GetRootPage() != EHWMenuPage::Title)
		{
			OpenMenuRoot(EHWMenuPage::Title, false);
		}
		else if (!bTitle && Menu.GetRootPage() == EHWMenuPage::Title)
		{
			Menu.Close();
			SetMenuInputActive(false);
		}
	}

	// The first Adaptive AI duel of a fresh install stops for "How the keeper learns you" (seen once, then never again).
	if (bAutoTutorial && !Menu.IsOpen())
	{
		const UHWSettingsSubsystem* S = GetSettings();
		const UHWDuelSubsystem* D = GetDuel();
		if (S != nullptr && !S->IsTutorialSeen() && D != nullptr && D->GetState() == EHWEncounterState::Running && D->GetTier() == EHWTier::Hellwalker
			&& !D->IsAutoplay() && GetPawn() != nullptr && D->GetFighterActor(HW::ESide::Player) == GetPawn())
		{
			OpenMenuRoot(EHWMenuPage::Tutorial, true);
		}
	}
}
