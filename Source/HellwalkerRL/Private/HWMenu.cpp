#include "HWMenu.h"

namespace HWMenuIds
{
	const FName SlidePrev(TEXT("SlidePrev"));
	const FName SlideNext(TEXT("SlideNext"));
	const FName SlidesDone(TEXT("SlidesDone"));
	const FName NewNormal(TEXT("NewPathbreaker"));
	const FName NewAdaptive(TEXT("NewHellwalker"));
}

// =================================================================================================
// The title's new walks
// =================================================================================================

void HWTitle::AddNewWalkItems(TArray<FHWMenuItem>& Out, const FString& Confirm)
{
	struct FNew { const FName* Id; const TCHAR* Label; const TCHAR* Hint; EHWMenuTone Tone; const TCHAR* Key; };
	const FNew News[] = {
		{ &HWMenuIds::NewNormal, TEXT("Normal"), TEXT("The keepers fight in fixed patterns.  Learn them, and they stay learned."), EHWMenuTone::Normal, TEXT("1") },
		{ &HWMenuIds::NewAdaptive, TEXT("Adaptive AI"), TEXT("The keepers learn you.  A trick that works at first stops working."), EHWMenuTone::Crimson, TEXT("2") },
	};
	for (const FNew& N : News)
	{
		FHWMenuItem I = FHWMenuItem::Action(*N.Id, FString::Printf(TEXT("New walk:  %s"), N.Label), N.Hint);
		I.Tone = N.Tone;
		I.Shortcut = N.Key;
		I.Confirm = Confirm;
		Out.Add(I);
	}
}

// =================================================================================================
// Items
// =================================================================================================

FString FHWMenuItem::ValueText() const
{
	switch (Kind)
	{
	case EHWMenuItemKind::Choice:
		return Options.IsValidIndex(Index) ? Options[Index] : FString();
	case EHWMenuItemKind::Slider:
		if (bPercent) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt32(Value * 100.f)); }
		switch (Decimals)
		{
		case 0:  return FString::Printf(TEXT("%.0f%s"), Value, *Suffix);
		case 1:  return FString::Printf(TEXT("%.1f%s"), Value, *Suffix);
		default: return FString::Printf(TEXT("%.2f%s"), Value, *Suffix);
		}
	case EHWMenuItemKind::Toggle:
		return bOn ? TEXT("On") : TEXT("Off");
	case EHWMenuItemKind::Binding:
		return KeyText;
	default:
		return FString();
	}
}

FHWMenuItem FHWMenuItem::Action(FName Id, const FString& Label, const FString& Hint)
{
	FHWMenuItem I;
	I.Id = Id;
	I.Kind = EHWMenuItemKind::Action;
	I.Label = Label;
	I.Hint = Hint;
	return I;
}

FHWMenuItem FHWMenuItem::Choice(FName Id, const FString& Label, const TArray<FString>& Options, int32 Index, const FString& Hint)
{
	FHWMenuItem I = Action(Id, Label, Hint);
	I.Kind = EHWMenuItemKind::Choice;
	I.Options = Options;
	I.Index = Options.Num() > 0 ? FMath::Clamp(Index, 0, Options.Num() - 1) : 0;
	return I;
}

FHWMenuItem FHWMenuItem::Slider(FName Id, const FString& Label, float Value, float Min, float Max, float Step, const FString& Hint)
{
	FHWMenuItem I = Action(Id, Label, Hint);
	I.Kind = EHWMenuItemKind::Slider;
	I.Min = Min;
	I.Max = FMath::Max(Min, Max);
	I.Step = FMath::Max(Step, KINDA_SMALL_NUMBER);
	I.Value = FMath::Clamp(Value, I.Min, I.Max);
	return I;
}

FHWMenuItem FHWMenuItem::Toggle(FName Id, const FString& Label, bool bOn, const FString& Hint)
{
	FHWMenuItem I = Action(Id, Label, Hint);
	I.Kind = EHWMenuItemKind::Toggle;
	I.bOn = bOn;
	return I;
}

FHWMenuItem FHWMenuItem::Binding(FName Id, const FString& Label, const FString& KeyText, const FString& PadText, bool bReadOnly, const FString& Hint)
{
	FHWMenuItem I = Action(Id, Label, Hint);
	I.Kind = EHWMenuItemKind::Binding;
	I.KeyText = KeyText;
	I.PadText = PadText;
	I.bReadOnly = bReadOnly;
	return I;
}

FHWMenuItem FHWMenuItem::Header(const FString& Label)
{
	FHWMenuItem I = Action(NAME_None, Label);
	I.Kind = EHWMenuItemKind::Header;
	return I;
}

// =================================================================================================
// Pages and the back stack
// =================================================================================================

int32 FHWMenu::FindItem(FName Id) const
{
	if (Id.IsNone()) { return INDEX_NONE; }
	return Items.IndexOfByPredicate([Id](const FHWMenuItem& I) { return I.Id == Id; });
}

void FHWMenu::Rebuild()
{
	Items.Reset();
	Info = FHWMenuPageInfo();
	if (Stack.Num() == 0) { return; }
	FEntry& E = Stack.Last();
	if (Builder) { Builder(E.Page, E.Tab, E.Slide, Items, Info); }
	if (Info.NumSlides > 0) { E.Slide = FMath::Clamp(E.Slide, 0, Info.NumSlides - 1); }
}

void FHWMenu::FixSelection(int32 Dir)
{
	if (Stack.Num() == 0) { return; }
	FEntry& E = Stack.Last();
	const int32 N = Items.Num();
	if (N == 0) { E.Selected = INDEX_NONE; return; }
	E.Selected = FMath::Clamp(E.Selected, 0, N - 1);
	const int32 Step = Dir < 0 ? -1 : 1;
	for (int32 Tries = 0; Tries < N; ++Tries)
	{
		if (Items[E.Selected].IsSelectable()) { return; }
		E.Selected = (E.Selected + Step + N) % N;
	}
	E.Selected = INDEX_NONE; // nothing selectable on this page
}

void FHWMenu::Open(EHWMenuPage Page, EHWSettingsTab Tab)
{
	Stack.Reset();
	bConfirm = false;
	EndCapture();
	Push(Page, Tab);
}

void FHWMenu::Push(EHWMenuPage Page, EHWSettingsTab Tab)
{
	if (Page == EHWMenuPage::None) { return; }
	bConfirm = false;
	EndCapture();
	FEntry E;
	E.Page = Page;
	E.Tab = Tab;
	Stack.Add(E);
	Rebuild();
	if (Info.NumSlides > 0)
	{
		// A slide deck opens on its Next button.
		const int32 Next = FindItem(HWMenuIds::SlideNext);
		if (Next != INDEX_NONE) { Stack.Last().Selected = Next; }
	}
	FixSelection(+1);
}

bool FHWMenu::Back()
{
	if (bCapture) { EndCapture(); return true; }
	if (bConfirm) { bConfirm = false; return true; }
	if (Stack.Num() == 0) { return false; }
	Stack.Pop();
	if (Stack.Num() == 0)
	{
		Items.Reset();
		Info = FHWMenuPageInfo();
		return false;
	}
	Rebuild();
	FixSelection(+1);
	return true;
}

void FHWMenu::Close()
{
	Stack.Reset();
	Items.Reset();
	Info = FHWMenuPageInfo();
	bConfirm = false;
	EndCapture();
}

void FHWMenu::Refresh()
{
	if (Stack.Num() == 0) { return; }
	const FHWMenuItem* Sel = GetSelectedItem();
	const FName Keep = Sel != nullptr ? Sel->Id : NAME_None;
	Rebuild();
	const int32 Found = FindItem(Keep);
	if (Found != INDEX_NONE) { Stack.Last().Selected = Found; }
	FixSelection(+1);
}

// =================================================================================================
// Navigation and editing
// =================================================================================================

void FHWMenu::Move(int32 Delta)
{
	if (Stack.Num() == 0 || bCapture || Delta == 0) { return; }
	if (bConfirm)
	{
		ConfirmChoice = ConfirmChoice == 0 ? 1 : 0;
		return;
	}
	FEntry& E = Stack.Last();
	const int32 N = Items.Num();
	if (N == 0) { return; }
	const int32 Step = Delta < 0 ? -1 : 1;
	for (int32 K = 0; K < FMath::Abs(Delta); ++K)
	{
		int32 I = E.Selected == INDEX_NONE ? (Step > 0 ? -1 : 0) : E.Selected;
		for (int32 Tries = 0; Tries < N; ++Tries)
		{
			I = (I + Step + N) % N; // wrap around
			if (Items[I].IsSelectable()) { E.Selected = I; break; }
		}
	}
}

FHWMenuEvent FHWMenu::ChangeSlide(int32 Dir)
{
	FHWMenuEvent Ev;
	if (Stack.Num() == 0 || Info.NumSlides <= 0) { return Ev; }
	FEntry& E = Stack.Last();
	const int32 Want = FMath::Clamp(E.Slide + Dir, 0, Info.NumSlides - 1);
	if (Want == E.Slide) { return Ev; }
	E.Slide = Want;
	Refresh();
	return Ev;
}

FHWMenuEvent FHWMenu::Adjust(int32 Dir)
{
	FHWMenuEvent Ev;
	if (Stack.Num() == 0 || bCapture || Dir == 0) { return Ev; }
	if (bConfirm)
	{
		ConfirmChoice = FMath::Clamp(ConfirmChoice + (Dir > 0 ? 1 : -1), 0, 1);
		return Ev;
	}
	const int32 Sel = GetSelected();
	if (!Items.IsValidIndex(Sel))
	{
		return Info.NumSlides > 0 ? ChangeSlide(Dir) : Ev;
	}
	FHWMenuItem& It = Items[Sel];
	Ev.Id = It.Id;
	switch (It.Kind)
	{
	case EHWMenuItemKind::Choice:
	{
		const int32 N = It.Options.Num();
		if (N == 0) { return FHWMenuEvent(); }
		const int32 Want = It.bWrap ? (It.Index + (Dir > 0 ? 1 : -1) + N) % N : FMath::Clamp(It.Index + (Dir > 0 ? 1 : -1), 0, N - 1);
		if (Want == It.Index) { return FHWMenuEvent(); }
		It.Index = Want;
		Ev.Type = EHWMenuEventType::Changed;
		Ev.Index = Want;
		return Ev;
	}
	case EHWMenuItemKind::Slider:
	{
		const float Raw = It.Value + (Dir > 0 ? It.Step : -It.Step);
		const float Snapped = It.Min + FMath::RoundToFloat((Raw - It.Min) / It.Step) * It.Step;
		const float Want = FMath::Clamp(Snapped, It.Min, It.Max);
		if (FMath::IsNearlyEqual(Want, It.Value, It.Step * 0.01f)) { return FHWMenuEvent(); }
		It.Value = Want;
		Ev.Type = EHWMenuEventType::Changed;
		Ev.Value = Want;
		return Ev;
	}
	case EHWMenuItemKind::Toggle:
		It.bOn = !It.bOn;
		Ev.Type = EHWMenuEventType::Changed;
		Ev.bOn = It.bOn;
		return Ev;
	default:
		// Buttons: on a slide deck, left / right turn the page.
		return Info.NumSlides > 0 ? ChangeSlide(Dir) : FHWMenuEvent();
	}
}

FHWMenuEvent FHWMenu::Accept()
{
	FHWMenuEvent Ev;
	if (Stack.Num() == 0 || bCapture) { return Ev; }
	if (bConfirm)
	{
		bConfirm = false;
		if (ConfirmChoice == 1)
		{
			Ev.Type = EHWMenuEventType::Confirmed;
			Ev.Id = ConfirmId;
		}
		return Ev;
	}
	const int32 Sel = GetSelected();
	if (!Items.IsValidIndex(Sel) || !Items[Sel].IsSelectable()) { return Ev; }
	FHWMenuItem& It = Items[Sel];
	Ev.Id = It.Id;
	switch (It.Kind)
	{
	case EHWMenuItemKind::Action:
		if (It.Id == HWMenuIds::SlidePrev) { return ChangeSlide(-1); }
		if (It.Id == HWMenuIds::SlideNext)
		{
			if (GetSlide() < Info.NumSlides - 1) { return ChangeSlide(+1); }
			Ev.Type = EHWMenuEventType::Activated;
			Ev.Id = HWMenuIds::SlidesDone;
			return Ev;
		}
		if (!It.Confirm.IsEmpty())
		{
			bConfirm = true;
			ConfirmText = It.Confirm;
			ConfirmId = It.Id;
			ConfirmChoice = 0; // the safe answer is the default
			return FHWMenuEvent();
		}
		Ev.Type = EHWMenuEventType::Activated;
		return Ev;
	case EHWMenuItemKind::Choice:
		if (It.Options.Num() == 0) { return FHWMenuEvent(); }
		It.Index = (It.Index + 1) % It.Options.Num();
		Ev.Type = EHWMenuEventType::Changed;
		Ev.Index = It.Index;
		return Ev;
	case EHWMenuItemKind::Toggle:
		It.bOn = !It.bOn;
		Ev.Type = EHWMenuEventType::Changed;
		Ev.bOn = It.bOn;
		return Ev;
	case EHWMenuItemKind::Binding:
		if (It.bReadOnly) { return FHWMenuEvent(); }
		bCapture = true;
		CaptureId = It.Id;
		Ev.Type = EHWMenuEventType::Capture;
		return Ev;
	default:
		return FHWMenuEvent();
	}
}

void FHWMenu::SetTab(EHWSettingsTab Tab)
{
	if (Stack.Num() == 0) { return; }
	FEntry& E = Stack.Last();
	if (E.Tab == Tab) { return; }
	bConfirm = false;
	EndCapture();
	E.Tab = Tab;
	E.Selected = 0;
	E.FirstVisible = 0;
	Rebuild();
	FixSelection(+1);
}

void FHWMenu::SwitchTab(int32 Dir)
{
	if (Stack.Num() == 0 || !Info.bTabs || Dir == 0 || bCapture) { return; }
	const int32 N = static_cast<int32>(EHWSettingsTab::Count);
	const int32 Cur = static_cast<int32>(Stack.Last().Tab);
	SetTab(static_cast<EHWSettingsTab>((Cur + (Dir > 0 ? 1 : -1) + N) % N));
}

void FHWMenu::SetSlide(int32 Slide)
{
	if (Stack.Num() == 0) { return; }
	ChangeSlide(Slide - Stack.Last().Slide);
}

void FHWMenu::Hover(int32 Index)
{
	if (Stack.Num() == 0 || bConfirm || bCapture || !Items.IsValidIndex(Index) || !Items[Index].IsSelectable()) { return; }
	Stack.Last().Selected = Index;
}

FHWMenuEvent FHWMenu::Click(int32 Index)
{
	Hover(Index);
	if (GetSelected() != Index) { return FHWMenuEvent(); }
	return Accept();
}

void FHWMenu::HoverConfirm(int32 Choice)
{
	if (bConfirm) { ConfirmChoice = FMath::Clamp(Choice, 0, 1); }
}

FHWMenuEvent FHWMenu::ClickConfirm(int32 Choice)
{
	if (!bConfirm) { return FHWMenuEvent(); }
	HoverConfirm(Choice);
	return Accept();
}

void FHWMenu::EnsureVisible(int32 VisibleRows)
{
	if (Stack.Num() == 0) { return; }
	FEntry& E = Stack.Last();
	const int32 V = FMath::Max(1, VisibleRows);
	const int32 N = Items.Num();
	E.FirstVisible = FMath::Clamp(E.FirstVisible, 0, FMath::Max(0, N - V));
	if (E.Selected == INDEX_NONE) { return; }
	if (E.Selected < E.FirstVisible) { E.FirstVisible = E.Selected; }
	if (E.Selected >= E.FirstVisible + V) { E.FirstVisible = E.Selected - V + 1; }
	// Keep a section's header with its first item.
	if (E.Selected == E.FirstVisible && E.Selected > 0 && Items[E.Selected - 1].Kind == EHWMenuItemKind::Header && V > 1)
	{
		E.FirstVisible = E.Selected - 1;
	}
}

// =================================================================================================
// The tutorial
// =================================================================================================

namespace HWTutorial
{
	static const FSlide GSlides[] = {
		{ TEXT("TWO MODES"),
		  TEXT("In Normal the keepers fight in fixed patterns: learn them, and they stay learned. In Adaptive AI the keeper is a neural "
		       "network that taught itself to fight - over a billion decisions against simulated players, starting from random moves, "
		       "with no script and no hand-written rule - and it learns YOU as you fight. A trick that works at first stops working.") },
		{ TEXT("IT SEES YOU A TENTH OF A SECOND LATE"),
		  TEXT("Like a person reading your animations, it sees what your body is doing about 0.1 s after it happens. It never sees "
		       "your buttons: a press you have not shown yet is invisible to it.") },
		{ TEXT("IT REMEMBERS YOU"),
		  TEXT("Every exchange this session feeds its memory - what it threw, how you answered, how it went. That memory is carried from "
		       "keeper to keeper across the valley, and forgotten the moment you quit.") },
		{ TEXT("READ IS TESTIMONY, NOT A TELEGRAPH"),
		  TEXT("READ flashes only AFTER one of its counters has landed on the answer it predicted. It tells you what it expected you to "
		       "do. It never warns you what is coming.") },
		{ TEXT("HABITS ARE WHAT IT FEEDS ON"),
		  TEXT("Always parry the fast slash, always dodge left after a heavy - it will notice, and throw the counter. It gets surer of you "
		       "the more your answers repeat, and the surer it is, the harder it fights. Change your tricks and it has to learn you again. "
		       "The keeper's notebook (in the pause menu) shows what it has written down about you.") },
		{ TEXT("DIFFICULTY, PARRIES, THREE KEEPERS"),
		  TEXT("Difficulty sets how hard the keepers hit and, in Adaptive AI, how strong they grow once they know you. The parry assist "
		       "lights a red ring on the keeper's weapon exactly while a parry would land; violet marks an attack no parry stops. "
		       "A clean parry stuns it and buys you a breath. The Warden, the Sage and the Returned each fight their own way - but all "
		       "three read the same you.\n\n"
		       "Anonymous gameplay stats are sent for research. See or reset yours on the website.") },
	};

	int32 NumSlides() { return UE_ARRAY_COUNT(GSlides); }
	const FSlide& Slide(int32 Index) { return GSlides[FMath::Clamp(Index, 0, NumSlides() - 1)]; }
}
