// HellwalkerRL — the menu model: pages, items, selection, value editing, navigation, the back stack, key capture and
// the confirm dialog. No drawing and no engine input here, so it is unit-tested (Project.HellwalkerRL.Menu.*).
//
// AHWPlayerController owns one FHWMenu, gives it a Builder that fills each page's items from the settings
// (HWPlayerControllerMenu.cpp), feeds it navigation (keyboard, gamepad, mouse) and turns the events it returns into
// actions; AHWHUD draws it. Values edited here are echoed back as Changed events — the owner applies them and calls
// Refresh(), which rebuilds the page from the source of truth while keeping the selection.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

enum class EHWMenuPage : uint8
{
	None,
	Title,      // the open world's title (root)
	Pause,      // the pause menu (root)
	Settings,   // tabbed
	Tutorial,   // "How the keeper learns you": slides
	Notebook,   // the keeper's notebook (drawn by AHWHUD::DrawNotebookPage)
	Map         // the open world's valley map (root): one item per keeper — accept tracks it (drawn by AHWHUD::DrawMapMenu)
};

enum class EHWSettingsTab : uint8
{
	Gameplay,
	Controls,
	Graphics,
	Audio,
	Accessibility,
	Count
};

enum class EHWMenuItemKind : uint8
{
	Action,    // Accept activates (or asks first, when Confirm is set)
	Choice,    // Left / Right pick among Options (clamped unless bWrap); Accept steps forward (wrapping)
	Slider,    // Left / Right step Value within [Min, Max]
	Toggle,    // Accept / Left / Right flip
	Binding,   // Accept waits for a key (unless read-only)
	Header     // a section label: never selected
};

/** A colour hint for the HUD (the model never draws). */
enum class EHWMenuTone : uint8
{
	Normal,
	Crimson,
	Gold,
	Dim
};

struct HELLWALKERRL_API FHWMenuItem
{
	FName Id;
	EHWMenuItemKind Kind = EHWMenuItemKind::Action;
	FString Label;
	/** One line shown under the list while selected. */
	FString Hint;
	EHWMenuTone Tone = EHWMenuTone::Normal;
	bool bEnabled = true;
	/** "[1]" — a keyboard shortcut shown beside the label. */
	FString Shortcut;

	/** Action: non-empty = ask this before activating. */
	FString Confirm;

	TArray<FString> Options;
	int32 Index = 0;
	bool bWrap = false;

	float Value = 0.f;
	float Min = 0.f;
	float Max = 1.f;
	float Step = 0.1f;
	/** Slider display: a percentage of 1.0 ("75%"), else the number with Decimals and Suffix ("1.25x", "0.4 s"). */
	bool bPercent = false;
	int32 Decimals = 2;
	FString Suffix;

	bool bOn = false;

	FString KeyText;
	FString PadText;
	bool bReadOnly = false;

	bool IsSelectable() const { return bEnabled && Kind != EHWMenuItemKind::Header; }
	/** "Hellwalker", "75%", "On", "E". */
	FString ValueText() const;
	/** Where a slider sits, 0..1 (for a bar). */
	float SliderAlpha() const { return Max > Min ? FMath::Clamp((Value - Min) / (Max - Min), 0.f, 1.f) : 0.f; }

	static FHWMenuItem Action(FName Id, const FString& Label, const FString& Hint = FString());
	static FHWMenuItem Choice(FName Id, const FString& Label, const TArray<FString>& Options, int32 Index, const FString& Hint = FString());
	static FHWMenuItem Slider(FName Id, const FString& Label, float Value, float Min, float Max, float Step, const FString& Hint = FString());
	static FHWMenuItem Toggle(FName Id, const FString& Label, bool bOn, const FString& Hint = FString());
	static FHWMenuItem Binding(FName Id, const FString& Label, const FString& KeyText, const FString& PadText, bool bReadOnly = false, const FString& Hint = FString());
	static FHWMenuItem Header(const FString& Label);
};

enum class EHWMenuEventType : uint8
{
	None,
	Activated,  // an Action (or a confirmed one: see Confirmed), or the tutorial's last Next
	Changed,    // a value: Index (Choice), Value (Slider), bOn (Toggle)
	Capture,    // a Binding wants a key: the owner captures the next one, then calls EndCapture()
	Confirmed   // the confirm dialog said yes to Id
};

struct HELLWALKERRL_API FHWMenuEvent
{
	EHWMenuEventType Type = EHWMenuEventType::None;
	FName Id;
	int32 Index = 0;
	float Value = 0.f;
	bool bOn = false;
};

struct HELLWALKERRL_API FHWMenuPageInfo
{
	FString Title;
	/** Tutorial pages: how many slides. */
	int32 NumSlides = 0;
	/** Settings: LB / RB (Q / E) switch tabs. */
	bool bTabs = false;
};

/** Item ids the model itself understands (tutorial slide buttons). */
namespace HWMenuIds
{
	extern HELLWALKERRL_API const FName SlidePrev;
	extern HELLWALKERRL_API const FName SlideNext;
	/** Emitted (Activated) by SlideNext on the last slide. */
	extern HELLWALKERRL_API const FName SlidesDone;
}

class HELLWALKERRL_API FHWMenu
{
public:
	using FBuilder = TFunction<void(EHWMenuPage Page, EHWSettingsTab Tab, int32 Slide, TArray<FHWMenuItem>& OutItems, FHWMenuPageInfo& OutInfo)>;
	/** Fills a page's items; called on every page change, tab / slide change and Refresh(). */
	FBuilder Builder;

	bool IsOpen() const { return Stack.Num() > 0; }
	EHWMenuPage GetPage() const { return Stack.Num() > 0 ? Stack.Last().Page : EHWMenuPage::None; }
	EHWMenuPage GetRootPage() const { return Stack.Num() > 0 ? Stack[0].Page : EHWMenuPage::None; }
	EHWSettingsTab GetTab() const { return Stack.Num() > 0 ? Stack.Last().Tab : EHWSettingsTab::Gameplay; }
	int32 GetSlide() const { return Stack.Num() > 0 ? Stack.Last().Slide : 0; }
	int32 GetDepth() const { return Stack.Num(); }
	int32 GetSelected() const { return Stack.Num() > 0 ? Stack.Last().Selected : INDEX_NONE; }
	const TArray<FHWMenuItem>& GetItems() const { return Items; }
	const FHWMenuItem* GetSelectedItem() const { return Items.IsValidIndex(GetSelected()) ? &Items[GetSelected()] : nullptr; }
	const FHWMenuPageInfo& GetInfo() const { return Info; }
	int32 FindItem(FName Id) const;

	/** A new root page (clears the stack). */
	void Open(EHWMenuPage Page, EHWSettingsTab Tab = EHWSettingsTab::Gameplay);
	/** On top of the current page: Back returns to it, selection intact. */
	void Push(EHWMenuPage Page, EHWSettingsTab Tab = EHWSettingsTab::Gameplay);
	/** Cancel the capture / the confirm dialog, else pop a page. False when that closed the menu. */
	bool Back();
	void Close();
	/** Rebuild the current page's items (values changed outside), keeping the selection by item id. */
	void Refresh();

	/** Up / down: the next selectable item, wrapping around. In the confirm dialog: switch its buttons. */
	void Move(int32 Delta);
	/** Left / right: values (clamped), tutorial slides, the confirm dialog's buttons. */
	FHWMenuEvent Adjust(int32 Dir);
	FHWMenuEvent Accept();
	/** Settings tabs, wrapping. */
	void SwitchTab(int32 Dir);
	void SetTab(EHWSettingsTab Tab);
	void SetSlide(int32 Slide);
	/** The mouse: hovering selects, clicking selects and accepts. */
	void Hover(int32 Index);
	FHWMenuEvent Click(int32 Index);

	bool IsConfirming() const { return bConfirm; }
	const FString& GetConfirmText() const { return ConfirmText; }
	FName GetConfirmId() const { return ConfirmId; }
	/** 0 = cancel (the default), 1 = confirm. */
	int32 GetConfirmChoice() const { return ConfirmChoice; }
	void HoverConfirm(int32 Choice);
	FHWMenuEvent ClickConfirm(int32 Choice);

	bool IsCapturing() const { return bCapture; }
	FName GetCaptureId() const { return CaptureId; }
	void EndCapture() { bCapture = false; CaptureId = NAME_None; }

	/** Scrolling: the first row shown, kept so that the selection is inside VisibleRows. */
	int32 GetFirstVisible() const { return Stack.Num() > 0 ? Stack.Last().FirstVisible : 0; }
	void EnsureVisible(int32 VisibleRows);

private:
	struct FEntry
	{
		EHWMenuPage Page = EHWMenuPage::None;
		EHWSettingsTab Tab = EHWSettingsTab::Gameplay;
		int32 Selected = 0;
		int32 Slide = 0;
		int32 FirstVisible = 0;
	};
	void Rebuild();
	/** Put the selection on a selectable item, searching in Dir from the current one. */
	void FixSelection(int32 Dir);
	FHWMenuEvent ChangeSlide(int32 Dir);

	TArray<FEntry> Stack;
	TArray<FHWMenuItem> Items;
	FHWMenuPageInfo Info;
	bool bConfirm = false;
	FString ConfirmText;
	FName ConfirmId;
	int32 ConfirmChoice = 0;
	bool bCapture = false;
	FName CaptureId;
};

/** "How the keeper learns you": the tutorial's slides (title + body). */
namespace HWTutorial
{
	struct FSlide
	{
		const TCHAR* Title;
		const TCHAR* Body;
	};
	HELLWALKERRL_API int32 NumSlides();
	HELLWALKERRL_API const FSlide& Slide(int32 Index);
}
