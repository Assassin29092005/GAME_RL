// Hellwalker — canvas HUD (no UMG assets).
//
// Player / boss health and sha-chi, the tier, the Read Meter (PLAN §2.4: the boss's top-1 predicted
// player action with a confidence bar, flashed ~0.4 s immediately AFTER a model-driven counter lands —
// testimony, not telegraph), impact flashes, the killer-move telegraph marker, start / end screens, and
// the A2 numeric overlay (F3) that the CSV must reconcile against.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HWHUD.generated.h"

class UHWDuelSubsystem;
class UFont;
class AHWOpenWorldGameMode;

UCLASS()
class HELLWALKERRL_API AHWHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	/** F1: the full controls panel, over any screen. */
	void ToggleHelp() { bShowHelp = !bShowHelp; }

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

	void Bar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill, const FLinearColor& Back);
	void Text(const FString& S, float X, float Y, const FLinearColor& C, UFont* Font, float Scale = 1.f, bool bCenter = false);

	float Clock = 0.f;
};
