// Hellwalker — canvas HUD (no UMG assets).
//
// Player / boss health and sha-chi, the tier, the Read Meter (PLAN §2.4: the boss's top-1 predicted
// player action with a confidence bar, flashed immediately AFTER a model-driven counter lands —
// testimony, not telegraph; held for the player's "READ banner hold" setting), impact flashes, the killer-move
// telegraph marker, start / end screens, the A2 numeric overlay (F3) that the CSV must reconcile against — and the
// menus (title, pause, settings, the tutorial, the keeper's notebook), drawn from AHWPlayerController's FHWMenu with
// mouse hit boxes ("M:I:<row>" and friends, see AHWPlayerController::MenuPointer).
//
// Everything is laid out in 1080p reference units times the player's HUD scale (UHWSettingsSubsystem::GetHudScale).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HWCore/HWBrain.h"
#include "HWHUD.generated.h"

class UHWDuelSubsystem;
class UFont;
class AHWOpenWorldGameMode;
class AHWPlayerController;
class FHWMenu;
struct FHWMenuItem;

UCLASS()
class HELLWALKERRL_API AHWHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;
	virtual void NotifyHitBoxBeginCursorOver(FName BoxName) override;
	/** F1: the full controls panel, over any screen. */
	void ToggleHelp() { bShowHelp = !bShowHelp; }

	/**
	 * The keeper's notebook page (pause menu), inside the panel's content rectangle (pixels). The notebook itself is
	 * HW::FRLNotebook (what the player answered per keeper move class, what the read head predicted, accuracy, reads
	 * landed) — until it is wired, a placeholder.
	 */
	void DrawNotebookPage(float X, float Y, float W, float H);

private:
	void DrawScreens();
	void DrawHelp();
	bool bShowHelp = false;
	void DrawBars(UHWDuelSubsystem* Duel);
	void DrawReadMeter(UHWDuelSubsystem* Duel);
	void DrawWorldMarkers(UHWDuelSubsystem* Duel);
	void DrawOverlay(UHWDuelSubsystem* Duel);
	void DrawStartScreen(UHWDuelSubsystem* Duel);
	void DrawEndScreen(UHWDuelSubsystem* Duel);
	void DrawControls(float Alpha);

	// ---- the open world
	void DrawOpenWorld(AHWOpenWorldGameMode* GM, UHWDuelSubsystem* Duel);
	void DrawTitle(AHWOpenWorldGameMode* GM);
	void DrawExplore(AHWOpenWorldGameMode* GM);
	void DrawCompass(AHWOpenWorldGameMode* GM);
	void DrawDuelResult(AHWOpenWorldGameMode* GM, UHWDuelSubsystem* Duel);
	void DrawEnding(AHWOpenWorldGameMode* GM, UHWDuelSubsystem* Duel, bool bRegret);
	void DrawBanner(AHWOpenWorldGameMode* GM);

	// ---- menus
	void DrawMenu(AHWPlayerController* PC);
	void DrawTitleMenu(AHWPlayerController* PC, const FHWMenu& M);
	void DrawPauseMenu(AHWPlayerController* PC, const FHWMenu& M);
	void DrawSettingsMenu(AHWPlayerController* PC, const FHWMenu& M);
	void DrawTutorial(AHWPlayerController* PC, const FHWMenu& M);
	void DrawNotebookMenu(AHWPlayerController* PC, const FHWMenu& M);
	/** One line from the notebook (and the adaptive change) on the duel result / end screens. */
	void DrawNotebookLine(UHWDuelSubsystem* Duel, float CX, float Y);
	void DrawConfirm(AHWPlayerController* PC, const FHWMenu& M);
	void DrawCapture(AHWPlayerController* PC, const FHWMenu& M);
	/** One settings row (label, then its value: arrows, a bar, a switch or a key). */
	void DrawSettingRow(const FHWMenu& M, int32 Index, float X, float Y, float W, float H, bool bHitBoxes);
	/** A row of buttons (the tutorial, the notebook), centered on CX. */
	void DrawButtonRow(const FHWMenu& M, float CX, float Y, float ButtonW, float ButtonH, bool bHitBoxes);
	/** The device's key hints for the current page, and the controller's message line. */
	void DrawMenuFooter(AHWPlayerController* PC, const FHWMenu& M, float X, float Y, float W, bool bCenter);
	void Panel(float X, float Y, float W, float H);
	void Outline(float X, float Y, float W, float H, const FLinearColor& C, float T);

	void Bar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill, const FLinearColor& Back);
	void Text(const FString& S, float X, float Y, const FLinearColor& C, UFont* Font, float Scale = 1.f, bool bCenter = false);
	float TextWidth(const FString& S, UFont* Font, float Scale) const;
	void Wrap(const FString& S, UFont* Font, float Scale, float MaxW, TArray<FString>& OutLines) const;
	/** A rebindable action's key as shown now (the gamepad's when the gamepad was used last). */
	FString KeyName(uint8 Bind, bool bPad) const;
	bool PadActive() const;

	float Clock = 0.f;
	/** Pixels per 1080p reference unit, times the HUD scale (this frame). */
	float UI = 1.f;
	float HudScale = 1.f;

	// The READ banner, held for the player's setting (game time: it does not fade while paused).
	HW::FReadMeterEvent ReadShown;
	bool bReadShown = false;
	double ReadShownAt = 0.0;
	float ReadLastAlpha = 0.f;
};
