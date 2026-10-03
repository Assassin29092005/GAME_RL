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
#include "HWSettings.h"
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
		const FName Restart(TEXT("Restart"));
		const FName QuitTitle(TEXT("QuitTitle"));
		const FName QuitDesktop(TEXT("QuitDesktop"));
		const FName Back(TEXT("Back"));
		const FName Continue(TEXT("Continue"));
		const FName NewPathbreaker(TEXT("NewPathbreaker"));
		const FName NewHellwalker(TEXT("NewHellwalker"));
		const FName New66(TEXT("New66"));
		const FName SkipTutorial(TEXT("SkipTutorial"));
		const FName Difficulty(TEXT("Difficulty"));
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
				const FString Mode = Existing->Mode == EHWPlayMode::Pathbreaker ? TEXT("Pathbreaker")
					: (Existing->Mode == EHWPlayMode::SixtySixDays ? TEXT("66 Days") : TEXT("Hellwalker"));
				Hint = FString::Printf(TEXT("%s  -  %d seal%s broken,  %d death%s"), *Mode, Cleared, Cleared == 1 ? TEXT("") : TEXT("s"),
					Existing->Deaths, Existing->Deaths == 1 ? TEXT("") : TEXT("s"));
				if (Existing->Mode == EHWPlayMode::SixtySixDays) { Hint += FString::Printf(TEXT(",  %d days left"), Existing->DaysLeft); }
			}
			FHWMenuItem C = FHWMenuItem::Action(Ids::Continue, TEXT("Continue"), Hint);
			C.Shortcut = TEXT("Enter");
			OutItems.Add(C);
		}
		const FString Erase = bSave ? TEXT("Begin a new walk?  The saved walk will be erased.") : FString();
		struct FNew { FName Id; const TCHAR* Label; const TCHAR* Hint; EHWMenuTone Tone; const TCHAR* Key; };
		const FNew News[] = {
			{ Ids::NewPathbreaker, TEXT("Pathbreaker"), TEXT("The keepers follow their patterns.  Learn them.  (The final keeper still reads you.)"), EHWMenuTone::Dim, TEXT("1") },
			{ Ids::NewHellwalker, TEXT("Hellwalker"), TEXT("They learn you.  Stay unpredictable."), EHWMenuTone::Crimson, TEXT("2") },
			{ Ids::New66, TEXT("66 Days"), TEXT("Hellwalker, and you have 66 lives.  Then the save is gone."), EHWMenuTone::Gold, TEXT("3") },
		};
		for (const FNew& N : News)
		{
			FHWMenuItem I = FHWMenuItem::Action(N.Id, FString::Printf(TEXT("New walk:  %s"), N.Label), N.Hint);
			I.Tone = N.Tone;
			I.Shortcut = N.Key;
			I.Confirm = Erase;
			OutItems.Add(I);
		}
		OutItems.Add(FHWMenuItem::Action(Ids::Settings, TEXT("Settings"), TEXT("Difficulty, controls, graphics, audio, accessibility.")));
		OutItems.Add(FHWMenuItem::Action(Ids::Tutorial, TEXT("How the keeper learns you"), TEXT("Six short pages: what it sees, what it remembers, what READ means.")));
		FHWMenuItem Q = FHWMenuItem::Action(Ids::QuitDesktop, TEXT("Quit to desktop"), TEXT("The keepers forget you when you quit."));
		Q.Confirm = TEXT("Quit to the desktop?  The keepers will forget you.");
		OutItems.Add(Q);
		return;
	}

	case EHWMenuPage::Pause:
	{
		OutInfo.Title = TEXT("PAUSED");
		OutItems.Add(FHWMenuItem::Action(Ids::Resume, TEXT("Resume"), TEXT("Back to it.  The world has been holding still.")));
		OutItems.Add(FHWMenuItem::Action(Ids::Settings, TEXT("Settings"), TEXT("Difficulty, controls, graphics, audio, accessibility.")));
		OutItems.Add(FHWMenuItem::Action(Ids::Tutorial, TEXT("How the keeper learns you"), TEXT("Six short pages: what it sees, what it remembers, what READ means.")));
		OutItems.Add(FHWMenuItem::Action(Ids::Notebook, TEXT("The keeper's notebook"), TEXT("What the keepers have written down about you this session.")));
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
		for (int32 D = 0; D <= static_cast<int32>(EHWDifficulty::Adaptive); ++D) { Names.Add(UHWSettingsSubsystem::DifficultyName(static_cast<EHWDifficulty>(D))); }
		OutItems.Add(FHWMenuItem::Choice(Ids::Difficulty, TEXT("Difficulty"), Names, static_cast<int32>(S->GetDifficulty()),
			UHWSettingsSubsystem::DifficultyBlurb(S->GetDifficulty()) + TEXT("  (From the next fight.)")));
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
		Fixed(TEXT("Choose tier (arena)"), TEXT("1 / 2"), TEXT("D-pad left / right"), TEXT("On the arena's start screen."));
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
	UE_LOG(LogHellwalkerRL, Log, TEXT("Menu: %s%s."), Page == EHWMenuPage::Title ? TEXT("title") : (Page == EHWMenuPage::Tutorial ? TEXT("tutorial") : TEXT("pause")),
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
	UE_LOG(LogHellwalkerRL, Warning, TEXT("hw.Menu: unknown page '%s' (pause | settings | controls | graphics | audio | accessibility | tutorial | notebook | close)."), *Name);
	return false;
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
	if (Id == Ids::Resume) { CloseMenu(); }
	else if (Id == Ids::Settings) { PushSettings(EHWSettingsTab::Gameplay); }
	else if (Id == Ids::Tutorial || Id == Ids::ShowTutorial) { Menu.Push(EHWMenuPage::Tutorial); }
	else if (Id == Ids::Notebook) { Menu.Push(EHWMenuPage::Notebook); }
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
	else if (Id == Ids::NewPathbreaker || Id == Ids::NewHellwalker || Id == Ids::New66)
	{
		if (GM != nullptr)
		{
			GM->NewGame(Id == Ids::NewPathbreaker ? EHWPlayMode::Pathbreaker : (Id == Ids::New66 ? EHWPlayMode::SixtySixDays : EHWPlayMode::Hellwalker));
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
		const EHWDifficulty D = static_cast<EHWDifficulty>(FMath::Clamp(Event.Index, 0, static_cast<int32>(EHWDifficulty::Adaptive)));
		S->SetDifficulty(D);
		ShowMenuMessage(FString::Printf(TEXT("Difficulty: %s  -  from the next fight."), *UHWSettingsSubsystem::DifficultyName(D)));
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
	else { UE_LOG(LogHellwalkerRL, Warning, TEXT("hw.MenuNav: unknown '%s' (up | down | left | right | accept | back | tabnext | tabprev | toggle)."), *Command); }
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

	// The first Hellwalker duel of a fresh install stops for "How the keeper learns you" (seen once, then never again).
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
