// HellwalkerRL — menu and settings automation tests (the menu model and the settings data are plain C++: no world).
//
//   Project.HellwalkerRL.Menu.Navigation      selection skips headers / disabled items and wraps; hover / click
//   Project.HellwalkerRL.Menu.Values          sliders and choices clamp, toggles flip, Accept cycles a choice
//   Project.HellwalkerRL.Menu.BackStack       Push / Back keep the page below and its selection; tabs wrap; Back closes
//   Project.HellwalkerRL.Menu.Confirm         a guarded action asks first (cancel is the default); Back cancels
//   Project.HellwalkerRL.Menu.Capture         a binding waits for a key; Back cancels; read-only bindings refuse
//   Project.HellwalkerRL.Menu.Tutorial        the slide deck opens on Next, turns pages, and ends with SlidesDone
//   Project.HellwalkerRL.Settings.Bindings    defaults are valid; bind, swap on a clash, refuse reserved / pad keys
//   Project.HellwalkerRL.Settings.Migration   a save from before the map key loads: Map gets M, or a free key if M was taken
//   Project.HellwalkerRL.Menu.TitleModes      the title offers exactly two new walks: [1] Normal, [2] Adaptive AI
//   Project.HellwalkerRL.Settings.Data        sanitising clamps (the retired Adaptive -> Normal); default Normal; the presets table
//   Project.HellwalkerRL.Settings.ParryAssist the assist per setting x difficulty, names, parsing, sanitising
//   Project.HellwalkerRL.Settings.SaveRoundTrip  a settings save written and read back is the same

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HWMenu.h"
#include "HWSettings.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr EAutomationTestFlags HWMenuTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	/** A menu whose pages are built from fixed item lists (the tests' "settings"). */
	struct FTestMenu
	{
		FHWMenu Menu;
		TArray<FHWMenuItem> Root;
		TArray<FHWMenuItem> Sub;
		bool bSubTabs = false;
		int32 Slides = 0;

		FTestMenu()
		{
			Menu.Builder = [this](EHWMenuPage Page, EHWSettingsTab Tab, int32 Slide, TArray<FHWMenuItem>& Out, FHWMenuPageInfo& Info)
			{
				(void)Tab;
				(void)Slide;
				if (Page == EHWMenuPage::Pause) { Out = Root; Info.Title = TEXT("Paused"); }
				else if (Page == EHWMenuPage::Settings) { Out = Sub; Info.Title = TEXT("Settings"); Info.bTabs = bSubTabs; }
				else if (Page == EHWMenuPage::Tutorial)
				{
					Info.NumSlides = Slides;
					Out.Add(FHWMenuItem::Action(HWMenuIds::SlidePrev, TEXT("Back")));
					Out.Add(FHWMenuItem::Action(HWMenuIds::SlideNext, TEXT("Next")));
				}
			};
		}
	};

	FHWMenuItem Disabled(FHWMenuItem I)
	{
		I.bEnabled = false;
		return I;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWMenuNavigationTest, "Project.HellwalkerRL.Menu.Navigation", HWMenuTestFlags)

bool FHWMenuNavigationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestMenu T;
	T.Root = { FHWMenuItem::Header(TEXT("H1")), FHWMenuItem::Action(TEXT("A"), TEXT("A")), Disabled(FHWMenuItem::Action(TEXT("B"), TEXT("B"))),
		FHWMenuItem::Action(TEXT("C"), TEXT("C")), FHWMenuItem::Header(TEXT("H2")), FHWMenuItem::Action(TEXT("D"), TEXT("D")) };
	T.Menu.Open(EHWMenuPage::Pause);
	TestTrue(TEXT("open"), T.Menu.IsOpen());
	TestEqual(TEXT("opens on the first selectable item (not the header)"), T.Menu.GetSelected(), 1);
	T.Menu.Move(+1);
	TestEqual(TEXT("down skips the disabled item"), T.Menu.GetSelected(), 3);
	T.Menu.Move(+1);
	TestEqual(TEXT("down skips the header"), T.Menu.GetSelected(), 5);
	T.Menu.Move(+1);
	TestEqual(TEXT("down wraps to the top"), T.Menu.GetSelected(), 1);
	T.Menu.Move(-1);
	TestEqual(TEXT("up wraps to the bottom"), T.Menu.GetSelected(), 5);
	T.Menu.Hover(2);
	TestEqual(TEXT("hovering a disabled item does not select it"), T.Menu.GetSelected(), 5);
	T.Menu.Hover(0);
	TestEqual(TEXT("hovering a header does not select it"), T.Menu.GetSelected(), 5);
	const FHWMenuEvent E = T.Menu.Click(3);
	TestTrue(TEXT("clicking an action selects and activates it"), E.Type == EHWMenuEventType::Activated && E.Id == FName(TEXT("C")) && T.Menu.GetSelected() == 3);
	const FHWMenuEvent None = T.Menu.Click(2);
	TestTrue(TEXT("clicking a disabled item does nothing"), None.Type == EHWMenuEventType::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWMenuValuesTest, "Project.HellwalkerRL.Menu.Values", HWMenuTestFlags)

bool FHWMenuValuesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestMenu T;
	T.Root = { FHWMenuItem::Slider(TEXT("S"), TEXT("Slider"), 0.5f, 0.f, 1.f, 0.25f),
		FHWMenuItem::Choice(TEXT("C"), TEXT("Choice"), { TEXT("x"), TEXT("y"), TEXT("z") }, 0),
		FHWMenuItem::Toggle(TEXT("T"), TEXT("Toggle"), false) };
	T.Menu.Open(EHWMenuPage::Pause);
	// Slider: 0.5 -> 0.75 -> 1.0, then clamped (no event).
	FHWMenuEvent E = T.Menu.Adjust(+1);
	TestTrue(TEXT("slider steps up"), E.Type == EHWMenuEventType::Changed && FMath::IsNearlyEqual(E.Value, 0.75f));
	T.Menu.Adjust(+1);
	E = T.Menu.Adjust(+1);
	TestTrue(TEXT("slider clamps at its max (no event)"), E.Type == EHWMenuEventType::None && FMath::IsNearlyEqual(T.Menu.GetItems()[0].Value, 1.f));
	for (int32 I = 0; I < 6; ++I) { T.Menu.Adjust(-1); }
	TestTrue(TEXT("slider clamps at its min"), FMath::IsNearlyEqual(T.Menu.GetItems()[0].Value, 0.f));
	TestEqual(TEXT("slider text"), T.Menu.GetItems()[0].ValueText(), FString(TEXT("0.00")));
	// Choice: clamped at 0 going left; Accept steps forward and wraps.
	T.Menu.Move(+1);
	E = T.Menu.Adjust(-1);
	TestTrue(TEXT("choice clamps at its first option"), E.Type == EHWMenuEventType::None && T.Menu.GetItems()[1].Index == 0);
	T.Menu.Adjust(+1);
	T.Menu.Adjust(+1);
	E = T.Menu.Adjust(+1);
	TestTrue(TEXT("choice clamps at its last option"), E.Type == EHWMenuEventType::None && T.Menu.GetItems()[1].Index == 2);
	E = T.Menu.Accept();
	TestTrue(TEXT("Accept cycles a choice (wrapping)"), E.Type == EHWMenuEventType::Changed && E.Index == 0);
	// Toggle: Accept and Left / Right flip it.
	T.Menu.Move(+1);
	E = T.Menu.Accept();
	TestTrue(TEXT("Accept flips a toggle"), E.Type == EHWMenuEventType::Changed && E.bOn);
	E = T.Menu.Adjust(-1);
	TestTrue(TEXT("left flips it back"), E.Type == EHWMenuEventType::Changed && !E.bOn && T.Menu.GetItems()[2].ValueText() == TEXT("Off"));
	// Refresh keeps the selection by id even if the items move.
	T.Root.Insert(FHWMenuItem::Header(TEXT("new")), 0);
	T.Menu.Refresh();
	TestEqual(TEXT("Refresh keeps the selected item by id"), T.Menu.GetSelectedItem() != nullptr ? T.Menu.GetSelectedItem()->Id : NAME_None, FName(TEXT("T")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWMenuBackStackTest, "Project.HellwalkerRL.Menu.BackStack", HWMenuTestFlags)

bool FHWMenuBackStackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestMenu T;
	T.Root = { FHWMenuItem::Action(TEXT("R0"), TEXT("R0")), FHWMenuItem::Action(TEXT("R1"), TEXT("R1")), FHWMenuItem::Action(TEXT("R2"), TEXT("R2")) };
	T.Sub = { FHWMenuItem::Toggle(TEXT("X"), TEXT("X"), true) };
	T.bSubTabs = true;
	T.Menu.Open(EHWMenuPage::Pause);
	T.Menu.Move(+2);
	TestEqual(TEXT("R2 selected"), T.Menu.GetSelected(), 2);
	T.Menu.Push(EHWMenuPage::Settings, EHWSettingsTab::Gameplay);
	TestTrue(TEXT("settings on top of pause"), T.Menu.GetPage() == EHWMenuPage::Settings && T.Menu.GetRootPage() == EHWMenuPage::Pause && T.Menu.GetDepth() == 2);
	T.Menu.SwitchTab(-1);
	TestTrue(TEXT("tabs wrap backwards (Gameplay -> Accessibility)"), T.Menu.GetTab() == EHWSettingsTab::Accessibility);
	T.Menu.SwitchTab(+1);
	T.Menu.SwitchTab(+1);
	TestTrue(TEXT("and forwards (-> Controls)"), T.Menu.GetTab() == EHWSettingsTab::Controls);
	TestTrue(TEXT("Back returns to pause"), T.Menu.Back() && T.Menu.GetPage() == EHWMenuPage::Pause);
	TestEqual(TEXT("... with its selection intact"), T.Menu.GetSelected(), 2);
	TestFalse(TEXT("Back on the root closes the menu"), T.Menu.Back());
	TestFalse(TEXT("closed"), T.Menu.IsOpen());
	T.Menu.Open(EHWMenuPage::Pause);
	T.Menu.Push(EHWMenuPage::Settings);
	T.Menu.Open(EHWMenuPage::Pause);
	TestEqual(TEXT("Open resets the stack"), T.Menu.GetDepth(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWMenuConfirmTest, "Project.HellwalkerRL.Menu.Confirm", HWMenuTestFlags)

bool FHWMenuConfirmTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestMenu T;
	FHWMenuItem Quit = FHWMenuItem::Action(TEXT("Quit"), TEXT("Quit"));
	Quit.Confirm = TEXT("Quit to the desktop?");
	T.Root = { Quit };
	T.Menu.Open(EHWMenuPage::Pause);
	FHWMenuEvent E = T.Menu.Accept();
	TestTrue(TEXT("a guarded action asks first"), E.Type == EHWMenuEventType::None && T.Menu.IsConfirming() && T.Menu.GetConfirmChoice() == 0);
	TestEqual(TEXT("the question"), T.Menu.GetConfirmText(), FString(TEXT("Quit to the desktop?")));
	E = T.Menu.Accept();
	TestTrue(TEXT("Accept on the default (cancel) does nothing"), E.Type == EHWMenuEventType::None && !T.Menu.IsConfirming());
	T.Menu.Accept();
	T.Menu.Adjust(+1);
	TestEqual(TEXT("right moves to Confirm"), T.Menu.GetConfirmChoice(), 1);
	E = T.Menu.Accept();
	TestTrue(TEXT("Confirm says yes"), E.Type == EHWMenuEventType::Confirmed && E.Id == FName(TEXT("Quit")));
	T.Menu.Accept();
	TestTrue(TEXT("Back cancels the dialog and keeps the menu"), T.Menu.Back() && !T.Menu.IsConfirming() && T.Menu.IsOpen());
	T.Menu.Accept();
	E = T.Menu.ClickConfirm(1);
	TestTrue(TEXT("clicking Confirm says yes"), E.Type == EHWMenuEventType::Confirmed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWMenuCaptureTest, "Project.HellwalkerRL.Menu.Capture", HWMenuTestFlags)

bool FHWMenuCaptureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestMenu T;
	T.Root = { FHWMenuItem::Binding(TEXT("Bind.Light"), TEXT("Light"), TEXT("LMB"), TEXT("X")),
		FHWMenuItem::Binding(TEXT("Bind.Pad"), TEXT("Pad"), TEXT("-"), TEXT("A"), true) };
	T.Menu.Open(EHWMenuPage::Pause);
	FHWMenuEvent E = T.Menu.Accept();
	TestTrue(TEXT("a binding waits for a key"), E.Type == EHWMenuEventType::Capture && T.Menu.IsCapturing() && T.Menu.GetCaptureId() == FName(TEXT("Bind.Light")));
	T.Menu.Move(+1);
	TestEqual(TEXT("navigation is frozen while capturing"), T.Menu.GetSelected(), 0);
	TestTrue(TEXT("Back cancels the capture, the menu stays"), T.Menu.Back() && !T.Menu.IsCapturing() && T.Menu.IsOpen());
	T.Menu.Accept();
	T.Menu.EndCapture();
	TestFalse(TEXT("EndCapture ends it"), T.Menu.IsCapturing());
	T.Menu.Move(+1);
	E = T.Menu.Accept();
	TestTrue(TEXT("a read-only binding does not capture"), E.Type == EHWMenuEventType::None && !T.Menu.IsCapturing());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWMenuTutorialTest, "Project.HellwalkerRL.Menu.Tutorial", HWMenuTestFlags)

bool FHWMenuTutorialTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestMenu T;
	T.Slides = HWTutorial::NumSlides();
	TestTrue(TEXT("the tutorial has 5-6 slides with text"), T.Slides >= 5 && T.Slides <= 6 && FCString::Strlen(HWTutorial::Slide(0).Body) > 40);
	FString All;
	for (int32 I = 0; I < T.Slides; ++I) { All += FString(HWTutorial::Slide(I).Title) + TEXT(" ") + HWTutorial::Slide(I).Body + TEXT(" "); }
	TestTrue(TEXT("it names both modes"), All.Contains(TEXT("Normal")) && All.Contains(TEXT("Adaptive AI")));
	TestTrue(TEXT("it says repeating makes it surer and changing makes it learn you again"), All.Contains(TEXT("surer")) && All.Contains(TEXT("learn you again")));
	TestFalse(TEXT("nothing about 66 Days or the retired Adaptive difficulty"), All.Contains(TEXT("66")) || All.Contains(TEXT("Adaptive changes")));
	T.Menu.Open(EHWMenuPage::Tutorial);
	TestTrue(TEXT("the deck opens on Next"), T.Menu.GetSelectedItem() != nullptr && T.Menu.GetSelectedItem()->Id == HWMenuIds::SlideNext && T.Menu.GetSlide() == 0);
	T.Menu.Adjust(-1);
	TestEqual(TEXT("left on the first slide stays"), T.Menu.GetSlide(), 0);
	FHWMenuEvent E;
	for (int32 I = 0; I < T.Slides - 1; ++I) { E = T.Menu.Accept(); }
	TestTrue(TEXT("Next walks to the last slide"), T.Menu.GetSlide() == T.Slides - 1 && E.Type == EHWMenuEventType::None);
	E = T.Menu.Accept();
	TestTrue(TEXT("Next on the last slide is done"), E.Type == EHWMenuEventType::Activated && E.Id == HWMenuIds::SlidesDone);
	T.Menu.Adjust(-1);
	TestEqual(TEXT("left turns back a page"), T.Menu.GetSlide(), T.Slides - 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWMenuTitleModesTest, "Project.HellwalkerRL.Menu.TitleModes", HWMenuTestFlags)

bool FHWMenuTitleModesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<FHWMenuItem> Items;
	HWTitle::AddNewWalkItems(Items, FString());
	TestEqual(TEXT("the title offers exactly two new walks"), Items.Num(), 2);
	if (Items.Num() != 2) { return false; }
	TestTrue(TEXT("[1] Normal"), Items[0].Id == HWMenuIds::NewNormal && Items[0].Shortcut == TEXT("1") && Items[0].Label.Contains(TEXT("Normal")));
	TestTrue(TEXT("[2] Adaptive AI"), Items[1].Id == HWMenuIds::NewAdaptive && Items[1].Shortcut == TEXT("2") && Items[1].Label.Contains(TEXT("Adaptive AI")));
	TestTrue(TEXT("no save: nothing to confirm"), Items[0].Confirm.IsEmpty() && Items[1].Confirm.IsEmpty());
	for (const FHWMenuItem& I : Items)
	{
		TestFalse(TEXT("no 66 Days, no research names"), I.Label.Contains(TEXT("66")) || I.Hint.Contains(TEXT("66")) || I.Label.Contains(TEXT("Pathbreaker"))
			|| I.Label.Contains(TEXT("Hellwalker")));
		TestFalse(TEXT("each explains itself"), I.Hint.IsEmpty());
	}
	TArray<FHWMenuItem> WithSave;
	HWTitle::AddNewWalkItems(WithSave, TEXT("Erase?"));
	TestTrue(TEXT("a saved walk: both ask first"), WithSave.Num() == 2 && WithSave[0].Confirm == TEXT("Erase?") && WithSave[1].Confirm == TEXT("Erase?"));

	// Through the menu model: the shortcuts' items are found by id, and accepting one asks first when a save would go.
	FHWMenu Menu;
	Menu.Builder = [](EHWMenuPage Page, EHWSettingsTab Tab, int32 Slide, TArray<FHWMenuItem>& Out, FHWMenuPageInfo& Info)
	{
		(void)Page;
		(void)Tab;
		(void)Slide;
		Info.Title = TEXT("HELLWALKER");
		HWTitle::AddNewWalkItems(Out, TEXT("Erase?"));
	};
	Menu.Open(EHWMenuPage::Title);
	const int32 Adaptive = Menu.FindItem(HWMenuIds::NewAdaptive);
	TestTrue(TEXT("[2] is on the page"), Adaptive != INDEX_NONE && Menu.FindItem(HWMenuIds::NewNormal) != INDEX_NONE);
	const FHWMenuEvent Asked = Menu.Click(Adaptive);
	TestTrue(TEXT("a new walk over a save asks first"), Asked.Type == EHWMenuEventType::None && Menu.IsConfirming());
	const FHWMenuEvent Yes = Menu.ClickConfirm(1);
	TestTrue(TEXT("... and goes on yes"), Yes.Type == EHWMenuEventType::Confirmed && Yes.Id == HWMenuIds::NewAdaptive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWSettingsBindingsTest, "Project.HellwalkerRL.Settings.Bindings", HWMenuTestFlags)

bool FHWSettingsBindingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FHWBindingTable B;
	TestTrue(TEXT("the defaults are valid"), B.IsValid());
	FString Msg;
	TestTrue(TEXT("Light -> K binds"), B.Rebind(EHWBind::Light, EKeys::K, Msg) == FHWBindingTable::EResult::Bound && B.Get(EHWBind::Light) == EKeys::K);
	TestTrue(TEXT("rebinding to the same key is a no-op"), B.Rebind(EHWBind::Light, EKeys::K, Msg) == FHWBindingTable::EResult::Unchanged);
	const FKey HeavyKey = B.Get(EHWBind::Heavy);
	const FKey ParryKey = B.Get(EHWBind::Parry);
	TestTrue(TEXT("Parry -> Heavy's key swaps the two"), B.Rebind(EHWBind::Parry, HeavyKey, Msg) == FHWBindingTable::EResult::Swapped
		&& B.Get(EHWBind::Parry) == HeavyKey && B.Get(EHWBind::Heavy) == ParryKey && B.IsValid());
	TestTrue(TEXT("Esc is refused"), B.Rebind(EHWBind::Heavy, EKeys::Escape, Msg) == FHWBindingTable::EResult::Refused);
	TestTrue(TEXT("W (movement) is refused"), B.Rebind(EHWBind::Heavy, EKeys::W, Msg) == FHWBindingTable::EResult::Refused);
	TestTrue(TEXT("a gamepad button is refused"), B.Rebind(EHWBind::Heavy, EKeys::Gamepad_FaceButton_Bottom, Msg) == FHWBindingTable::EResult::Refused);
	TestTrue(TEXT("the mouse wheel is refused"), B.Rebind(EHWBind::Heavy, EKeys::MouseScrollUp, Msg) == FHWBindingTable::EResult::Refused);
	TestTrue(TEXT("still valid after the refusals"), B.IsValid());
	FHWBindingTable C;
	C.FromArray(B.ToArray());
	TestTrue(TEXT("ToArray / FromArray round trip"), C == B);
	TArray<FHWKeyBinding> Clash = FHWBindingTable().ToArray();
	Clash[static_cast<int32>(EHWBind::Light)].Key = Clash[static_cast<int32>(EHWBind::Heavy)].Key;
	FHWBindingTable D;
	D.Rebind(EHWBind::Light, EKeys::K, Msg);
	D.FromArray(Clash);
	TestTrue(TEXT("a clashing saved table falls back to the defaults"), D == FHWBindingTable());
	TestTrue(TEXT("every action has a gamepad label and an action label"), [&]()
	{
		for (int32 I = 0; I < FHWBindingTable::Num; ++I)
		{
			if (FHWBindingTable::ActionLabel(static_cast<EHWBind>(I)).IsEmpty() || FHWBindingTable::GamepadLabel(static_cast<EHWBind>(I)).IsEmpty()) { return false; }
		}
		return true;
	}());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWSettingsMigrationTest, "Project.HellwalkerRL.Settings.Migration", HWMenuTestFlags)

bool FHWSettingsMigrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FHWBindingTable Defaults;
	TestTrue(TEXT("the map is on M / D-pad left, explore-only"), Defaults.Get(EHWBind::Map) == EKeys::M && FHWBindingTable::GamepadKey(EHWBind::Map) == EKeys::Gamepad_DPad_Left
		&& FHWBindingTable::Scope(EHWBind::Map) == EHWBindScope::Explore);
	FString Why;
	TestTrue(TEXT("M is bindable while exploring"), FHWBindingTable::IsBindable(EKeys::M, EHWBindScope::Explore, Why));

	// A save written before the map key existed: every action but Map.
	auto OldSave = [](const FHWBindingTable& From)
	{
		TArray<FHWKeyBinding> A = From.ToArray();
		A.RemoveAll([](const FHWKeyBinding& B) { return B.Action == EHWBind::Map; });
		return A;
	};
	FHWBindingTable Loaded;
	Loaded.FromArray(OldSave(Defaults));
	TestTrue(TEXT("an old default save loads as today's defaults (Map on M)"), Loaded == Defaults);

	// The player had put the notebook on M (free back then): the notebook keeps M, the map takes a free key.
	FHWBindingTable Old;
	FString Msg;
	TestTrue(TEXT("light -> K"), Old.Rebind(EHWBind::Light, EKeys::K, Msg) == FHWBindingTable::EResult::Bound);
	TArray<FHWKeyBinding> Saved = OldSave(Old);
	for (FHWKeyBinding& B : Saved) { if (B.Action == EHWBind::Notebook) { B.Key = EKeys::M; } }
	FHWBindingTable Migrated;
	Migrated.FromArray(Saved);
	TestTrue(TEXT("the saved keys win: the notebook stays on M, light stays on K"), Migrated.Get(EHWBind::Notebook) == EKeys::M && Migrated.Get(EHWBind::Light) == EKeys::K);
	TestTrue(TEXT("the map moved to a free key"), Migrated.Get(EHWBind::Map).IsValid() && Migrated.Get(EHWBind::Map) != EKeys::M && Migrated.IsValid());
	TestTrue(TEXT("nothing else changed"), Migrated.Get(EHWBind::Heavy) == Defaults.Get(EHWBind::Heavy) && Migrated.Get(EHWBind::Interact) == Defaults.Get(EHWBind::Interact)
		&& Migrated.Get(EHWBind::Pause) == Defaults.Get(EHWBind::Pause));
	FHWBindingTable Again;
	Again.FromArray(Migrated.ToArray());
	TestTrue(TEXT("the migrated table round-trips"), Again == Migrated);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWSettingsDataTest, "Project.HellwalkerRL.Settings.Data", HWMenuTestFlags)

bool FHWSettingsDataTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FHWSettingsData D;
	D.MouseSensitivity = 50.f;
	D.MasterVolume = -1.f;
	D.MusicVolume = NAN;
	D.HudScale = 0.1f;
	D.ReadHoldSeconds = 9.f;
	D.Difficulty = static_cast<EHWDifficulty>(42);
	D.Sanitize();
	TestTrue(TEXT("sanitize clamps (an unknown difficulty plays Normal)"), D.MouseSensitivity == FHWSettingsData::MaxSensitivity && D.MasterVolume == 0.f
		&& D.MusicVolume == 1.f && D.HudScale == FHWSettingsData::MinHudScale && D.ReadHoldSeconds == FHWSettingsData::MaxReadHold
		&& D.Difficulty == EHWDifficulty::Normal);
	TestTrue(TEXT("the default difficulty is Normal, the default assist ring + slow motion"), FHWSettingsData().Difficulty == EHWDifficulty::Normal
		&& FHWSettingsData().ParryAssist == EHWParryAssist::RingSlow);
	FHWSettingsData Old;
	Old.Difficulty = EHWDifficulty::Adaptive;
	Old.Sanitize();
	TestTrue(TEXT("the retired Adaptive difficulty sanitises to Normal"), Old.Difficulty == EHWDifficulty::Normal);
	for (EHWDifficulty Kept : { EHWDifficulty::Easy, EHWDifficulty::Normal, EHWDifficulty::Hard, EHWDifficulty::Hellwalker })
	{
		FHWSettingsData K;
		K.Difficulty = Kept;
		K.Sanitize();
		TestTrue(FString::Printf(TEXT("%s survives sanitising"), *UHWSettingsSubsystem::DifficultyName(Kept)), K.Difficulty == Kept);
	}

	// The presets (DESIGN 2): skill range (Adaptive AI), the keeper's damage scale and the assist (both modes).
	struct FWant { EHWDifficulty D; float Lo; float Hi; float Damage; float Slow; bool bGlow; };
	const FWant Table[] = {
		{ EHWDifficulty::Easy,       0.00f, 0.40f, 0.60f, 0.40f, true },
		{ EHWDifficulty::Normal,     0.00f, 0.70f, 0.75f, 0.60f, false },
		{ EHWDifficulty::Hard,       0.15f, 0.85f, 0.85f, 0.75f, false },
		{ EHWDifficulty::Hellwalker, 0.30f, 1.00f, 0.90f, 0.85f, false },
	};
	const int32 Rows = static_cast<int32>(UE_ARRAY_COUNT(Table));
	TestEqual(TEXT("the menu offers Easy .. Hellwalker"), UHWSettingsSubsystem::NumMenuDifficulties, Rows);
	for (int32 I = 0; I < Rows; ++I)
	{
		const FWant& W = Table[I];
		const FHWDifficultyPreset P = UHWSettingsSubsystem::PresetFor(W.D);
		TestTrue(FString::Printf(TEXT("%s -> skill %.2f..%.2f, damage x%.2f, slow motion %.2f, glow %d"), *UHWSettingsSubsystem::DifficultyName(W.D), W.Lo, W.Hi,
			W.Damage, W.Slow, W.bGlow ? 1 : 0), FMath::IsNearlyEqual(P.SkillLo, W.Lo) && FMath::IsNearlyEqual(P.SkillHi, W.Hi)
			&& FMath::IsNearlyEqual(P.KeeperDamageScale, W.Damage) && FMath::IsNearlyEqual(P.AssistSlowScale, W.Slow) && P.bIncomingGlow == W.bGlow);
		TestTrue(TEXT("every difficulty is softer than the trained keeper"), P.KeeperDamageScale < 1.f && P.SkillLo < P.SkillHi && P.SkillHi <= 1.f);
		TestEqual(TEXT("the menu's order is the enum's"), static_cast<int32>(W.D), I);
		const FString Blurb = UHWSettingsSubsystem::DifficultyBlurb(W.D);
		TestTrue(TEXT("each difficulty explains itself, with its damage"), !Blurb.IsEmpty()
			&& Blurb.Contains(FString::Printf(TEXT("%d%%"), FMath::RoundToInt32((1.f - W.Damage) * 100.f))));
		if (I > 0)
		{
			const FHWDifficultyPreset Prev = UHWSettingsSubsystem::PresetFor(Table[I - 1].D);
			TestTrue(TEXT("each step up hits harder, grows stronger and slows less"), P.KeeperDamageScale > Prev.KeeperDamageScale && P.SkillHi > Prev.SkillHi
				&& P.AssistSlowScale > Prev.AssistSlowScale);
		}
	}
	const FHWDifficultyPreset Retired = UHWSettingsSubsystem::PresetFor(EHWDifficulty::Adaptive);
	const FHWDifficultyPreset Normal = UHWSettingsSubsystem::PresetFor(EHWDifficulty::Normal);
	TestTrue(TEXT("the retired Adaptive gets Normal's numbers"), Retired.SkillLo == Normal.SkillLo && Retired.SkillHi == Normal.SkillHi
		&& Retired.KeeperDamageScale == Normal.KeeperDamageScale && Retired.AssistSlowScale == Normal.AssistSlowScale);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWSettingsParryAssistTest, "Project.HellwalkerRL.Settings.ParryAssist", HWMenuTestFlags)

bool FHWSettingsParryAssistTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using S = UHWSettingsSubsystem;
	for (EHWDifficulty D : { EHWDifficulty::Easy, EHWDifficulty::Normal, EHWDifficulty::Hard, EHWDifficulty::Hellwalker })
	{
		const FHWDifficultyPreset P = S::PresetFor(D);
		const FString Name = S::DifficultyName(D);
		const FHWAssistParams Slow = S::AssistFor(EHWParryAssist::RingSlow, D);
		TestTrue(FString::Printf(TEXT("%s, ring + slow motion: the ring, the difficulty's slow motion and glow"), *Name),
			Slow.bRing && FMath::IsNearlyEqual(Slow.SlowScale, P.AssistSlowScale) && Slow.SlowScale > 0.f && Slow.SlowScale < 1.f && Slow.bIncomingGlow == P.bIncomingGlow);
		const FHWAssistParams Ring = S::AssistFor(EHWParryAssist::Ring, D);
		TestTrue(FString::Printf(TEXT("%s, ring only: no slow motion"), *Name), Ring.bRing && Ring.SlowScale == 1.f && Ring.bIncomingGlow == P.bIncomingGlow);
		const FHWAssistParams Off = S::AssistFor(EHWParryAssist::Off, D);
		TestTrue(FString::Printf(TEXT("%s, off: no help at all"), *Name), !Off.bRing && !Off.bIncomingGlow && Off.SlowScale == 1.f);
		TestEqual(TEXT("telemetry: ring+slowmo"), S::AssistTelemetryName(Slow), FString(TEXT("ring+slowmo")));
		TestEqual(TEXT("telemetry: ring"), S::AssistTelemetryName(Ring), FString(TEXT("ring")));
		TestEqual(TEXT("telemetry: off"), S::AssistTelemetryName(Off), FString(TEXT("off")));
	}
	TestTrue(TEXT("names and blurbs"), [&]()
	{
		for (EHWParryAssist A : { EHWParryAssist::RingSlow, EHWParryAssist::Ring, EHWParryAssist::Off })
		{
			if (S::ParryAssistName(A).IsEmpty() || S::ParryAssistBlurb(A).IsEmpty()) { return false; }
		}
		return S::ParryAssistName(EHWParryAssist::RingSlow) != S::ParryAssistName(EHWParryAssist::Ring);
	}());
	EHWParryAssist A = EHWParryAssist::Off;
	TestTrue(TEXT("-HWParryAssist=RingSlow"), S::ParseParryAssist(TEXT("RingSlow"), A) && A == EHWParryAssist::RingSlow);
	TestTrue(TEXT("-HWParryAssist=ring (any case)"), S::ParseParryAssist(TEXT("ring"), A) && A == EHWParryAssist::Ring);
	TestTrue(TEXT("-HWParryAssist=OFF"), S::ParseParryAssist(TEXT("OFF"), A) && A == EHWParryAssist::Off);
	A = EHWParryAssist::Ring;
	TestTrue(TEXT("an unknown value is refused and changes nothing"), !S::ParseParryAssist(TEXT("sometimes"), A) && A == EHWParryAssist::Ring);
	FHWSettingsData D;
	D.ParryAssist = static_cast<EHWParryAssist>(9);
	D.Sanitize();
	TestTrue(TEXT("an unknown assist sanitises to the default"), D.ParryAssist == EHWParryAssist::RingSlow);
	D.ParryAssist = EHWParryAssist::Off;
	D.Sanitize();
	TestTrue(TEXT("Off survives sanitising"), D.ParryAssist == EHWParryAssist::Off);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWSettingsSaveRoundTripTest, "Project.HellwalkerRL.Settings.SaveRoundTrip", HWMenuTestFlags)

bool FHWSettingsSaveRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Slot = TEXT("HellwalkerRL_Settings_Test");
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	UHWSettingsSave* Out = Cast<UHWSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHWSettingsSave::StaticClass()));
	Out->Data.Difficulty = EHWDifficulty::Hard;
	Out->Data.ParryAssist = EHWParryAssist::Ring;
	Out->Data.MouseSensitivity = 1.75f;
	Out->Data.bInvertY = true;
	Out->Data.MusicVolume = 0.3f;
	Out->Data.HudScale = 1.25f;
	Out->Data.ReadHoldSeconds = 1.2f;
	Out->Data.bTutorialSeen = true;
	FHWBindingTable B;
	FString Msg;
	B.Rebind(EHWBind::Light, EKeys::K, Msg);
	Out->Data.Bindings = B.ToArray();
	TestTrue(TEXT("written"), Out->Write(Slot));
	UHWSettingsSave* In = UHWSettingsSave::LoadOrNull(Slot);
	TestNotNull(TEXT("read back"), In);
	if (In != nullptr)
	{
		const FHWSettingsData& D = In->Data;
		FHWBindingTable C;
		C.FromArray(D.Bindings);
		TestTrue(TEXT("every field survives"), D.Difficulty == EHWDifficulty::Hard && D.ParryAssist == EHWParryAssist::Ring
			&& FMath::IsNearlyEqual(D.MouseSensitivity, 1.75f) && D.bInvertY && FMath::IsNearlyEqual(D.MusicVolume, 0.3f) && FMath::IsNearlyEqual(D.HudScale, 1.25f)
			&& FMath::IsNearlyEqual(D.ReadHoldSeconds, 1.2f) && D.bTutorialSeen && C == B);
	}

	// A save from before 1.4: the retired Adaptive difficulty loads as Normal.
	UHWSettingsSave* Old = Cast<UHWSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHWSettingsSave::StaticClass()));
	Old->Data.Difficulty = EHWDifficulty::Adaptive;
	Old->Data.ParryAssist = EHWParryAssist::Off;
	TestTrue(TEXT("an old save written"), Old->Write(Slot));
	UHWSettingsSave* Migrated = UHWSettingsSave::LoadOrNull(Slot);
	TestTrue(TEXT("Adaptive loads as Normal; the assist survives"), Migrated != nullptr && Migrated->Data.Difficulty == EHWDifficulty::Normal
		&& Migrated->Data.ParryAssist == EHWParryAssist::Off);

	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	TestNull(TEXT("an empty slot loads as nothing"), UHWSettingsSave::LoadOrNull(Slot));
	return true;
}

#endif
