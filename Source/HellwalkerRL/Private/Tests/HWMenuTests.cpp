// HellwalkerRL — menu and settings automation tests (the menu model and the settings data are plain C++: no world).
//
//   Project.HellwalkerRL.Menu.Navigation      selection skips headers / disabled items and wraps; hover / click
//   Project.HellwalkerRL.Menu.Values          sliders and choices clamp, toggles flip, Accept cycles a choice
//   Project.HellwalkerRL.Menu.BackStack       Push / Back keep the page below and its selection; tabs wrap; Back closes
//   Project.HellwalkerRL.Menu.Confirm         a guarded action asks first (cancel is the default); Back cancels
//   Project.HellwalkerRL.Menu.Capture         a binding waits for a key; Back cancels; read-only bindings refuse
//   Project.HellwalkerRL.Menu.Tutorial        the slide deck opens on Next, turns pages, and ends with SlidesDone
//   Project.HellwalkerRL.Settings.Bindings    defaults are valid; bind, swap on a clash, refuse reserved / pad keys
//   Project.HellwalkerRL.Settings.Data        sanitising clamps; the difficulty -> keeper skill table
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
	TestTrue(TEXT("sanitize clamps"), D.MouseSensitivity == FHWSettingsData::MaxSensitivity && D.MasterVolume == 0.f && D.MusicVolume == 1.f
		&& D.HudScale == FHWSettingsData::MinHudScale && D.ReadHoldSeconds == FHWSettingsData::MaxReadHold && D.Difficulty == EHWDifficulty::Hellwalker);
	struct FWant { EHWDifficulty D; float Skill; float Temp; bool bAdaptive; };
	const FWant Table[] = {
		{ EHWDifficulty::Easy, 0.f, 1.f, false }, { EHWDifficulty::Normal, 0.4f, 0.6f, false }, { EHWDifficulty::Hard, 0.75f, 0.f, false },
		{ EHWDifficulty::Hellwalker, 1.f, 0.f, false }, { EHWDifficulty::Adaptive, 0.6f, 0.f, true },
	};
	for (const FWant& W : Table)
	{
		float S = -1.f, T = -1.f;
		bool bA = false;
		UHWSettingsSubsystem::KeeperSkillFor(W.D, S, T, bA);
		TestTrue(FString::Printf(TEXT("%s -> skill %.2f, temperature %.1f"), *UHWSettingsSubsystem::DifficultyName(W.D), W.Skill, W.Temp),
			FMath::IsNearlyEqual(S, W.Skill) && FMath::IsNearlyEqual(T, W.Temp) && bA == W.bAdaptive);
		TestFalse(TEXT("each difficulty explains itself"), UHWSettingsSubsystem::DifficultyBlurb(W.D).IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWSettingsSaveRoundTripTest, "Project.HellwalkerRL.Settings.SaveRoundTrip", HWMenuTestFlags)

bool FHWSettingsSaveRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Slot = TEXT("HellwalkerRL_Settings_Test");
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	UHWSettingsSave* Out = Cast<UHWSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHWSettingsSave::StaticClass()));
	Out->Data.Difficulty = EHWDifficulty::Adaptive;
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
		TestTrue(TEXT("every field survives"), D.Difficulty == EHWDifficulty::Adaptive && FMath::IsNearlyEqual(D.MouseSensitivity, 1.75f) && D.bInvertY
			&& FMath::IsNearlyEqual(D.MusicVolume, 0.3f) && FMath::IsNearlyEqual(D.HudScale, 1.25f) && FMath::IsNearlyEqual(D.ReadHoldSeconds, 1.2f)
			&& D.bTutorialSeen && C == B);
	}
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	TestNull(TEXT("an empty slot loads as nothing"), UHWSettingsSave::LoadOrNull(Slot));
	return true;
}

#endif
