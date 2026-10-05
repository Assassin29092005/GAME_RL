#include "HWHUD.h"

#include "HWDuelSubsystem.h"
#include "HWOpenWorldGameMode.h"
#include "HWSaveGame.h"
#include "HWSessionSubsystem.h"
#include "HWCharacterBase.h"
#include "HWMap.h"
#include "HWMenu.h"
#include "HWOpenWorld.h"
#include "HWPlayerController.h"
#include "HWSettings.h"
#include "HWTypesUE.h"
#include "HWWorldGen.h"
#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GlobalRenderResources.h"

namespace
{
	FString HumanSym(HW::ESym S)
	{
		switch (S)
		{
		case HW::ESym::Neutral: return TEXT("Stand your ground");
		case HW::ESym::Advance: return TEXT("Walk in");
		case HW::ESym::Retreat: return TEXT("Back off");
		case HW::ESym::Light:   return TEXT("Light attack");
		case HW::ESym::Heavy:   return TEXT("Heavy attack");
		case HW::ESym::Block:   return TEXT("Block");
		case HW::ESym::Parry:   return TEXT("Parry");
		case HW::ESym::StepF:   return TEXT("Ghoststep in");
		case HW::ESym::StepB:   return TEXT("Ghoststep back");
		case HW::ESym::StepL:   return TEXT("Ghoststep left");
		case HW::ESym::StepR:   return TEXT("Ghoststep right");
		case HW::ESym::Switch:  return TEXT("Weapon switch");
		default:                return ToFString(HW::SymName(S));
		}
	}

	const FLinearColor Ink(0.92f, 0.9f, 0.86f);
	const FLinearColor Dim(0.55f, 0.53f, 0.5f);
	const FLinearColor Crimson(0.78f, 0.06f, 0.05f);
	const FLinearColor Gold(0.95f, 0.72f, 0.2f);
	const FLinearColor Teal(0.15f, 0.75f, 0.8f);
	const FLinearColor BackBar(0.02f, 0.02f, 0.03f, 0.75f);
	// Menus: a lighter red for text on dark panels, the panel, the selection.
	const FLinearColor Ember(0.95f, 0.24f, 0.19f);
	const FLinearColor Bright(1.f, 0.98f, 0.95f);
	const FLinearColor PanelBack(0.018f, 0.015f, 0.017f, 0.93f);
	const FLinearColor Select(0.78f, 0.06f, 0.05f, 0.26f);
	const FLinearColor Disabled(0.34f, 0.33f, 0.32f);
	// The duel's tells: red means "parry now" (the parry assist) and nothing else; the unblockable telegraph is violet.
	const FLinearColor ParryRed(1.f, 0.12f, 0.10f);
	const FLinearColor ParryGrey(0.62f, 0.62f, 0.64f);
	const FLinearColor Violet(0.62f, 0.30f, 1.f);

	FLinearColor WithAlpha(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}

	/** The mode as players know it ("NORMAL" / "ADAPTIVE AI"); the tier names stay internal. */
	FString ModeLabel(EHWTier Tier)
	{
		return Tier == EHWTier::Hellwalker ? FString(TEXT("ADAPTIVE AI")) : FString(TEXT("NORMAL"));
	}

	/** "up 12 after this fight" / "down 8 ..." / "unchanged ..." — an insight change in percentage points. */
	FString InsightChangeText(float Change, const TCHAR* When)
	{
		const int32 Points = FMath::RoundToInt32(Change * 100.f);
		if (Points == 0) { return FString::Printf(TEXT("unchanged %s"), When); }
		return FString::Printf(TEXT("%s %d %s"), Points > 0 ? TEXT("up") : TEXT("down"), FMath::Abs(Points), When);
	}

	FLinearColor ToneColor(EHWMenuTone T)
	{
		switch (T)
		{
		case EHWMenuTone::Crimson: return Ember;
		case EHWMenuTone::Gold:    return Gold;
		case EHWMenuTone::Dim:     return FLinearColor(0.68f, 0.66f, 0.62f);
		default:                   return Ink;
		}
	}

	const TCHAR* TabName(EHWSettingsTab T)
	{
		switch (T)
		{
		case EHWSettingsTab::Gameplay:      return TEXT("Gameplay");
		case EHWSettingsTab::Controls:      return TEXT("Controls");
		case EHWSettingsTab::Graphics:      return TEXT("Graphics");
		case EHWSettingsTab::Audio:         return TEXT("Audio");
		case EHWSettingsTab::Accessibility: return TEXT("Accessibility");
		default:                            return TEXT("?");
		}
	}

	const TCHAR* Roman(int32 I)
	{
		static const TCHAR* R[] = { TEXT("I"), TEXT("II"), TEXT("III"), TEXT("IV"), TEXT("V"), TEXT("VI"), TEXT("VII"), TEXT("VIII") };
		return R[FMath::Clamp(I, 0, 7)];
	}

	FName Box(const TCHAR* Kind, int32 I) { return FName(*FString::Printf(TEXT("M:%s:%d"), Kind, I)); }
}

// =================================================================================================
// Helpers
// =================================================================================================

void AHWHUD::Bar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill, const FLinearColor& Back)
{
	DrawRect(Back, X - 2.f, Y - 2.f, W + 4.f, H + 4.f);
	DrawRect(Fill, X, Y, W * FMath::Clamp(Fraction, 0.f, 1.f), H);
}

void AHWHUD::Text(const FString& S, float X, float Y, const FLinearColor& C, UFont* Font, float Scale, bool bCenter)
{
	if (bCenter)
	{
		float W = 0.f;
		float H = 0.f;
		GetTextSize(S, W, H, Font, Scale);
		X -= W * 0.5f;
	}
	DrawText(S, C, X, Y, Font, Scale);
}

float AHWHUD::TextWidth(const FString& S, UFont* Font, float Scale) const
{
	float W = 0.f;
	float H = 0.f;
	GetTextSize(S, W, H, Font, Scale);
	return W;
}

void AHWHUD::Wrap(const FString& S, UFont* Font, float Scale, float MaxW, TArray<FString>& OutLines) const
{
	TArray<FString> Words;
	S.ParseIntoArray(Words, TEXT(" "), false);
	FString Line;
	for (const FString& Word : Words)
	{
		const FString Try = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
		if (!Line.TrimStartAndEnd().IsEmpty() && TextWidth(Try, Font, Scale) > MaxW)
		{
			OutLines.Add(Line.TrimEnd());
			Line = Word;
		}
		else
		{
			Line = Try;
		}
	}
	if (!Line.TrimStartAndEnd().IsEmpty()) { OutLines.Add(Line.TrimEnd()); }
}

bool AHWHUD::PadActive() const
{
	const AHWPlayerController* PC = Cast<AHWPlayerController>(GetOwningPlayerController());
	return PC != nullptr && PC->IsGamepadActive();
}

FString AHWHUD::KeyName(uint8 Bind, bool bPad) const
{
	const EHWBind A = static_cast<EHWBind>(Bind);
	if (bPad) { return FHWBindingTable::GamepadLabel(A); }
	const UGameInstance* GI = GetGameInstance();
	const UHWSettingsSubsystem* Settings = GI != nullptr ? GI->GetSubsystem<UHWSettingsSubsystem>() : nullptr;
	return FHWBindingTable::KeyLabel(Settings != nullptr ? Settings->GetBindings().Get(A) : FHWBindingTable::DefaultKey(A));
}

void AHWHUD::Outline(float X, float Y, float W, float H, const FLinearColor& C, float T)
{
	DrawRect(C, X, Y, W, T);
	DrawRect(C, X, Y + H - T, W, T);
	DrawRect(C, X, Y + T, T, H - 2.f * T);
	DrawRect(C, X + W - T, Y + T, T, H - 2.f * T);
}

void AHWHUD::Panel(float X, float Y, float W, float H)
{
	DrawRect(PanelBack, X, Y, W, H);
	Outline(X, Y, W, H, FLinearColor(1.f, 1.f, 1.f, 0.07f), FMath::Max(1.f, UI));
	DrawRect(WithAlpha(Crimson, 0.95f), X, Y, W, 3.f * UI);
}

void AHWHUD::FillTriangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
{
	FCanvasUVTri T;
	T.V0_Pos = A;
	T.V1_Pos = B;
	T.V2_Pos = C;
	T.V0_Color = Color;
	T.V1_Color = Color;
	T.V2_Color = Color;
	FCanvasTriangleItem Item(A, B, C, GWhiteTexture);
	Item.TriangleList.Reset();
	Item.TriangleList.Add(T);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AHWHUD::FillDiamond(const FVector2D& At, float R, const FLinearColor& Color)
{
	const FVector2D N(At.X, At.Y - R);
	const FVector2D E(At.X + R, At.Y);
	const FVector2D So(At.X, At.Y + R);
	const FVector2D W(At.X - R, At.Y);
	FillTriangle(N, E, So, Color);
	FillTriangle(N, So, W, Color);
}

void AHWHUD::FillCircle(const FVector2D& At, float R, const FLinearColor& Color)
{
	constexpr int32 Segments = 20;
	FCanvasTriangleItem Item(At, At, At, GWhiteTexture);
	Item.TriangleList.Reset();
	for (int32 I = 0; I < Segments; ++I)
	{
		const float A0 = 2.f * UE_PI * static_cast<float>(I) / Segments;
		const float A1 = 2.f * UE_PI * static_cast<float>(I + 1) / Segments;
		FCanvasUVTri T;
		T.V0_Pos = At;
		T.V1_Pos = At + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * R;
		T.V2_Pos = At + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * R;
		T.V0_Color = Color;
		T.V1_Color = Color;
		T.V2_Color = Color;
		Item.TriangleList.Add(T);
	}
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AHWHUD::RingCircle(const FVector2D& At, float R, const FLinearColor& Color, float Thickness)
{
	constexpr int32 Segments = 28;
	for (int32 I = 0; I < Segments; ++I)
	{
		const float A0 = 2.f * UE_PI * static_cast<float>(I) / Segments;
		const float A1 = 2.f * UE_PI * static_cast<float>(I + 1) / Segments;
		DrawLine(At.X + FMath::Cos(A0) * R, At.Y + FMath::Sin(A0) * R, At.X + FMath::Cos(A1) * R, At.Y + FMath::Sin(A1) * R, Color, Thickness);
	}
}

void AHWHUD::KeeperIcon(const FVector2D& At, float R, bool bCleared, bool bSealed)
{
	const FLinearColor C = AHWOpenWorldGameMode::KeeperColor(bCleared, bSealed);
	FillDiamond(At, R + FMath::Max(2.f, 2.5f * UI), FLinearColor(0.f, 0.f, 0.f, 0.85f));
	FillDiamond(At, R, bCleared ? WithAlpha(C, 0.55f) : C);
	const float T = FMath::Max(1.5f, 2.2f * UI);
	if (bSealed) { DrawRect(Ink, At.X - R * 0.62f, At.Y - T * 0.5f, R * 1.24f, T); } // the seal across its gate
	if (bCleared)
	{
		DrawLine(At.X - R * 0.45f, At.Y - R * 0.45f, At.X + R * 0.45f, At.Y + R * 0.45f, Ink, T);
		DrawLine(At.X - R * 0.45f, At.Y + R * 0.45f, At.X + R * 0.45f, At.Y - R * 0.45f, Ink, T);
	}
}

void AHWHUD::ShadowText(const FString& S, float X, float Y, const FLinearColor& C, UFont* Font, float Scale, bool bCenter)
{
	const float O = FMath::Max(1.f, 1.5f * UI);
	Text(S, X + O, Y + O, FLinearColor(0.f, 0.f, 0.f, 0.85f * C.A), Font, Scale, bCenter);
	Text(S, X, Y, C, Font, Scale, bCenter);
}

// =================================================================================================
// Frame
// =================================================================================================

void AHWHUD::DrawHUD()
{
	Super::DrawHUD();
	if (GetWorld() == nullptr || Canvas == nullptr) { return; }
	Clock += GetWorld()->GetDeltaSeconds();
	const UGameInstance* GI = GetGameInstance();
	const UHWSettingsSubsystem* Settings = GI != nullptr ? GI->GetSubsystem<UHWSettingsSubsystem>() : nullptr;
	HudScale = Settings != nullptr ? Settings->GetHudScale() : 1.f;
	UI = Canvas->ClipY / 1080.f * HudScale;
	DrawScreens();
	AHWPlayerController* PC = Cast<AHWPlayerController>(GetOwningPlayerController());
	if (PC != nullptr && PC->IsMenuOpen()) { DrawMenu(PC); }
	if (bShowHelp) { DrawHelp(); }
}

void AHWHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	if (AHWPlayerController* PC = Cast<AHWPlayerController>(GetOwningPlayerController())) { PC->MenuPointer(BoxName, true); }
}

void AHWHUD::NotifyHitBoxBeginCursorOver(FName BoxName)
{
	Super::NotifyHitBoxBeginCursorOver(BoxName);
	if (AHWPlayerController* PC = Cast<AHWPlayerController>(GetOwningPlayerController())) { PC->MenuPointer(BoxName, false); }
}

void AHWHUD::DrawHelp()
{
	// The whole map, in the game (README "Controls" is the same list). Keyboard keys from the player's bindings | gamepad.
	struct FRow { FString Key; FString Pad; FString What; };
	auto K = [this](EHWBind A) { return KeyName(static_cast<uint8>(A), false); };
	auto P = [](EHWBind A) { return FHWBindingTable::GamepadLabel(A); };
	const TArray<FRow> Explore = {
		{ TEXT("W A S D"), TEXT("left stick"), TEXT("move (camera-relative)") },
		{ TEXT("Mouse"), TEXT("right stick"), TEXT("look") },
		{ TEXT("Shift (hold)"), TEXT("LB (hold)"), TEXT("sprint") },
		{ TEXT("Left Ctrl"), TEXT("RB"), TEXT("walk / run toggle") },
		{ TEXT("Space"), TEXT("A"), TEXT("jump - at a wall: hurdle, vault, mantle or climb (up to ~2.2 m)") },
		{ TEXT("C"), TEXT("B"), TEXT("crouch - while running: slide") },
		{ TEXT("R"), TEXT("X"), TEXT("ragdoll - Space to get back up") },
		{ TEXT("Middle mouse"), TEXT("R3"), TEXT("strafe (face the camera direction)") },
		{ TEXT("RMB (hold)"), TEXT("LT (hold)"), TEXT("aim") },
		{ K(EHWBind::Interact), P(EHWBind::Interact), TEXT("ring a bell / challenge a keeper at a shrine gate") },
		{ K(EHWBind::Map), P(EHWBind::Map), TEXT("the valley map: every keeper and bell, where you are - pick the keeper to track") },
	};
	const TArray<FRow> Fight = {
		{ TEXT("W A S D"), TEXT("left stick"), TEXT("move (locked on: strafe around the keeper)") },
		{ K(EHWBind::Light), P(EHWBind::Light), TEXT("light attack (chain)") },
		{ K(EHWBind::Heavy), P(EHWBind::Heavy), TEXT("heavy attack") },
		{ K(EHWBind::Guard) + TEXT(" (hold)"), P(EHWBind::Guard), TEXT("block") },
		{ K(EHWBind::Parry), P(EHWBind::Parry), TEXT("parry (just before the hit: press while the red ring is lit)") },
		{ K(EHWBind::Step) + TEXT(" + direction"), P(EHWBind::Step), TEXT("ghoststep (dodge)") },
		{ K(EHWBind::Switch), P(EHWBind::Switch), TEXT("switch weapon: twin blades / greatblade") },
		{ K(EHWBind::LockOn), P(EHWBind::LockOn), TEXT("lock-on") },
		{ K(EHWBind::Restart) + TEXT("  /  1, 2"), TEXT("menu  /  D-pad L, R"), TEXT("arena: fight again (it remembers)  /  pick the mode (its start screen names them)") },
	};
	const TArray<FRow> Menus = {
		{ TEXT("Esc  /  ") + K(EHWBind::Pause), TEXT("Start"), TEXT("pause menu: settings, key bindings, the tutorial, restart, quit") },
		{ K(EHWBind::Notebook), P(EHWBind::Notebook), TEXT("the keeper's notebook") },
		{ K(EHWBind::Help), P(EHWBind::Help), TEXT("this panel") },
		{ K(EHWBind::Debug), P(EHWBind::Debug), TEXT("debug overlay (the keeper's choice, its read of you, hit volumes)") },
		{ TEXT("1 / 2"), TEXT("title menu"), TEXT("after an ending: a new walk - Normal / Adaptive AI") },
	};
	// Capped so that the panel always fits the screen, whatever the HUD scale.
	const float S = FMath::Min(UI, FMath::Min(Canvas->ClipY / 1000.f, Canvas->ClipX / 1280.f));
	const float W = 1180.f * S;
	const float X0 = (Canvas->ClipX - W) * 0.5f;
	float Y = 50.f * S;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.84f), X0 - 30.f * S, Y - 24.f * S, W + 60.f * S, 930.f * S);
	auto Section = [&](const TCHAR* Title, const TArray<FRow>& Rows)
	{
		Text(Title, X0, Y, Gold, GEngine->GetMediumFont(), 1.15f * S);
		Y += 34.f * S;
		for (const FRow& R : Rows)
		{
			Text(R.Key, X0, Y, Ink, GEngine->GetSmallFont(), 1.15f * S);
			Text(R.Pad, X0 + 230.f * S, Y, Dim, GEngine->GetSmallFont(), 1.15f * S);
			Text(R.What, X0 + 440.f * S, Y, Ink, GEngine->GetSmallFont(), 1.15f * S);
			Y += 24.f * S;
		}
		Y += 16.f * S;
	};
	Section(TEXT("EXPLORING  (Game Animation Sample character)"), Explore);
	Section(TEXT("FIGHTING  (a keeper's duel, or the arena)"), Fight);
	Section(TEXT("MENUS AND DEBUG"), Menus);
	const FString Close = FString::Printf(TEXT("%s to close  -  keys can be changed in Settings > Controls"), *KeyName(static_cast<uint8>(EHWBind::Help), PadActive()));
	Text(Close, X0 + W - TextWidth(Close, GEngine->GetSmallFont(), 1.1f * S), Y, Dim, GEngine->GetSmallFont(), 1.1f * S);
}

void AHWHUD::DrawScreens()
{
	UWorld* World = GetWorld();
	UHWDuelSubsystem* Duel = World->GetSubsystem<UHWDuelSubsystem>();
	if (AHWOpenWorldGameMode* GM = World->GetAuthGameMode<AHWOpenWorldGameMode>())
	{
		DrawOpenWorld(GM, Duel);
		return;
	}
	if (Duel == nullptr || Duel->GetEncounter() == nullptr) { return; }

	if (Duel->GetState() == EHWEncounterState::WaitingToStart)
	{
		DrawStartScreen(Duel);
		return;
	}
	DrawWorldMarkers(Duel);
	DrawParryCue(Duel);
	DrawBars(Duel);
	DrawReadMeter(Duel);
	if (Duel->bDebugDraw) { DrawOverlay(Duel); }
	if (Duel->GetState() != EHWEncounterState::Running) { DrawEndScreen(Duel); }

	const float Seconds = static_cast<float>(Duel->GetDuelFrame()) / static_cast<float>(HW::FramesPerSecond);
	DrawControls(Duel->GetState() == EHWEncounterState::Running ? FMath::Clamp(1.f - (Seconds - 14.f) / 4.f, 0.f, 1.f) : 1.f);
}

// =================================================================================================
// The duel
// =================================================================================================

void AHWHUD::DrawBars(UHWDuelSubsystem* Duel)
{
	const float S = UI;
	UFont* Large = GEngine->GetLargeFont();
	UFont* Medium = GEngine->GetMediumFont();
	const HW::FFighter& P = Duel->GetFighterState(HW::ESide::Player);
	const HW::FFighter& B = Duel->GetFighterState(HW::ESide::Boss);

	// Boss — top centre.
	const float BW = FMath::Min(Canvas->ClipX * 0.46f * HudScale, Canvas->ClipX - 80.f * S);
	const float BX = (Canvas->ClipX - BW) * 0.5f;
	Text(Duel->BossTitle.ToUpper(), Canvas->ClipX * 0.5f, 26.f * S, Ink, Large, 1.1f * S, true);
	Bar(BX, 64.f * S, BW, 14.f * S, B.Health / FMath::Max(1.f, B.HealthMax), Crimson, BackBar);
	const bool bExposed = B.State == HW::EFighterState::GuardBroken;
	const FLinearColor Posture = bExposed ? FMath::Lerp(Gold, FLinearColor::White, 0.5f + 0.5f * FMath::Sin(Clock * 20.f)) : Gold;
	Bar(BX + BW * 0.2f, 84.f * S, BW * 0.6f, 5.f * S, B.ShaChi / FMath::Max(1.f, B.ShaChiMax), Posture, BackBar);
	if (bExposed) { Text(TEXT("EXPOSED"), Canvas->ClipX * 0.5f, 94.f * S, Gold, Medium, 1.f * S, true); }

	// Player — bottom left.
	const float PX = 40.f * S;
	const float PY = Canvas->ClipY - 118.f * S;
	const float PW = FMath::Min(Canvas->ClipX * 0.26f * HudScale, Canvas->ClipX * 0.5f);
	Text(TEXT("SOUL"), PX, PY - 30.f * S, Ink, Medium, 1.1f * S);
	Bar(PX, PY, PW, 12.f * S, P.Health / FMath::Max(1.f, P.HealthMax), Crimson, BackBar);
	Bar(PX, PY + 20.f * S, PW * 0.75f, 6.f * S, P.ShaChi / FMath::Max(1.f, P.ShaChiMax),
		P.State == HW::EFighterState::GuardBroken ? Gold : Teal, BackBar);
	Text(P.Weapon == 1 ? TEXT("Glaive") : TEXT("Twin Blades"), PX, PY + 34.f * S, Dim, Medium, 0.9f * S);

	// The mode — top right (the medium font: right-aligned large text measured short and ran off the edge).
	if (const UHWSessionSubsystem* Session = Duel->GetSession())
	{
		const bool bHell = Duel->GetTier() == EHWTier::Hellwalker;
		const FLinearColor C = Session->bBlind ? Ink : (bHell ? Ember : Dim);
		const FString Label = Session->bBlind ? Session->TierLabel(Duel->GetTier()).ToUpper() : ModeLabel(Duel->GetTier());
		const FString Enc = FString::Printf(TEXT("encounter %d"), Session->EncountersStarted);
		Text(Label, Canvas->ClipX - 40.f * S - TextWidth(Label, Medium, 1.2f * S), 30.f * S, C, Medium, 1.2f * S);
		Text(Enc, Canvas->ClipX - 40.f * S - TextWidth(Enc, Medium, 0.8f * S), 62.f * S, Dim, Medium, 0.8f * S);
	}
}

void AHWHUD::DrawReadMeter(UHWDuelSubsystem* Duel)
{
	// Testimony, not telegraph: this appears only after the counter has already landed. The duel raises it for ~0.4 s;
	// the HUD holds it for the player's setting, in game time (a pause does not eat it).
	HW::FReadMeterEvent RM;
	float DuelAlpha = 0.f;
	const bool bActive = Duel->GetActiveReadMeter(RM, DuelAlpha);
	const double Now = GetWorld()->GetTimeSeconds();
	if (bActive && (!bReadShown || RM.Frame != ReadShown.Frame || DuelAlpha > ReadLastAlpha + 0.01f))
	{
		ReadShown = RM;
		bReadShown = true;
		ReadShownAt = Now;
	}
	ReadLastAlpha = bActive ? DuelAlpha : 0.f;
	if (bReadShown && Duel->GetDuelFrame() < ReadShown.Frame) { bReadShown = false; } // a new encounter began
	if (!bReadShown) { return; }
	const UGameInstance* GI = GetGameInstance();
	const UHWSettingsSubsystem* Settings = GI != nullptr ? GI->GetSubsystem<UHWSettingsSubsystem>() : nullptr;
	const float Hold = Settings != nullptr ? Settings->GetReadHoldSeconds() : 0.4f;
	const float T = static_cast<float>(Now - ReadShownAt);
	if (T >= Hold)
	{
		bReadShown = false;
		return;
	}
	const float Alpha = 1.f - T / FMath::Max(Hold, 0.01f);

	const float S = UI;
	const float CX = Canvas->ClipX * 0.5f;
	const float Y = Canvas->ClipY * 0.70f;
	const float A = FMath::Clamp(Alpha * 1.6f, 0.f, 1.f);
	const FLinearColor Red = WithAlpha(Crimson, A);
	const FLinearColor Light = WithAlpha(Ink, A);
	Text(TEXT("READ"), CX, Y, Red, GEngine->GetMediumFont(), 1.0f * S, true);
	Text(HumanSym(ReadShown.Predicted), CX, Y + 24.f * S, Light, GEngine->GetLargeFont(), 1.2f * S, true);
	const float W = 220.f * S;
	Bar(CX - W * 0.5f, Y + 64.f * S, W, 6.f * S, ReadShown.Confidence, Red, WithAlpha(BackBar, A));
	Text(FString::Printf(TEXT("%.0f%%  -  %s"), ReadShown.Confidence * 100.f, *ToFString(HW::Move(ReadShown.Counter).Name)), CX, Y + 76.f * S, Light,
		GEngine->GetSmallFont(), 1.f * S, true);
}

void AHWHUD::DrawWorldMarkers(UHWDuelSubsystem* Duel)
{
	const float S = UI;
	for (const UHWDuelSubsystem::FFlash& Fl : Duel->GetFlashes())
	{
		const FVector P = Project(Fl.Location);
		if (P.Z <= 0.f) { continue; }
		const float K = static_cast<float>(Fl.FramesLeft) / static_cast<float>(FMath::Max(1, Fl.FramesTotal));
		const float R = Fl.Size * 46.f * S * (1.4f - K * 0.6f);
		FLinearColor C = Fl.Color;
		C.A = K;
		for (int32 I = 0; I < 10; ++I)
		{
			const float Ang = (static_cast<float>(I) / 10.f) * 2.f * UE_PI + Fl.Size;
			const float In = R * 0.35f;
			DrawLine(P.X + FMath::Cos(Ang) * In, P.Y + FMath::Sin(Ang) * In, P.X + FMath::Cos(Ang) * R, P.Y + FMath::Sin(Ang) * R, C, 3.f * S);
		}
	}

	// Killer-move telegraph: a violet mark over the keeper while an unblockable wind-up is running (step it; red is the
	// parry assist's alone).
	const HW::FFighter& B = Duel->GetFighterState(HW::ESide::Boss);
	if (const AHWCharacterBase* Boss = Duel->GetFighterActor(HW::ESide::Boss))
	{
		if (B.State == HW::EFighterState::Acting && B.CurrentMove().bUnblockable && B.T < B.CurrentMove().Startup)
		{
			const FVector P = Project(Boss->GetActorLocation() + FVector(0.f, 0.f, 230.f));
			if (P.Z > 0.f)
			{
				const float R = (22.f + 6.f * FMath::Sin(Clock * 24.f)) * S;
				DrawLine(P.X, P.Y - R, P.X + R, P.Y, Violet, 4.f * S);
				DrawLine(P.X + R, P.Y, P.X, P.Y + R, Violet, 4.f * S);
				DrawLine(P.X, P.Y + R, P.X - R, P.Y, Violet, 4.f * S);
				DrawLine(P.X - R, P.Y, P.X, P.Y - R, Violet, 4.f * S);
				Text(TEXT("!"), P.X, P.Y - 16.f * S, Violet, GEngine->GetLargeFont(), 1.1f * S, true);
			}
		}
		// Lock-on reticle.
		const FVector Chest = Project(Boss->GetActorLocation() + FVector(0.f, 0.f, 40.f));
		if (Chest.Z > 0.f)
		{
			const float R = 7.f * S;
			DrawLine(Chest.X - R, Chest.Y, Chest.X + R, Chest.Y, FLinearColor(1.f, 1.f, 1.f, 0.6f), 2.f * S);
			DrawLine(Chest.X, Chest.Y - R, Chest.X, Chest.Y + R, FLinearColor(1.f, 1.f, 1.f, 0.6f), 2.f * S);
		}
	}
}

void AHWHUD::DrawParryCue(UHWDuelSubsystem* Duel)
{
	// The parry assist. Red = a parry pressed NOW lands (the duel's cue is exact to the frame, HWParryAssist.h); no
	// countdown before the window opens. Nothing for unparryable moves, resolved swings or swings that cannot reach.
	if (Duel->GetState() != EHWEncounterState::Running) { return; }
	const FHWAssistParams& Assist = Duel->GetFightAssist();
	const FHWParryCue& Cue = Duel->GetParryCue();
	const bool bRing = Assist.bRing && Cue.bActive && Cue.bInWindow;
	const bool bGlow = Assist.bIncomingGlow && Cue.bActive && Cue.bIncoming;
	if (!bRing && !bGlow) { return; }
	const AHWCharacterBase* Boss = Duel->GetFighterActor(HW::ESide::Boss);
	APlayerController* PCtrl = GetOwningPlayerController();
	if (Boss == nullptr || PCtrl == nullptr) { return; }
	TArray<FVector> Points;
	Boss->GetTelegraphPoints(Points);
	FVector Eye;
	FRotator View;
	PCtrl->GetPlayerViewPoint(Eye, View);
	const FVector CamRight = FRotationMatrix(View).GetScaledAxis(EAxis::Y);

	const float S = UI;
	const bool bLit = bRing && Cue.IsLit();
	const bool bLast = bLit && Cue.FramesToImpact <= Cue.WindowFirst + 3; // the window's last 4 frames: brightest
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Clock * 28.f);
	bool bKeyShown = false;
	for (const FVector& W : Points)
	{
		const FVector At = Project(W);
		if (At.Z <= 0.f) { continue; }
		// The ring's size: 60 cm at the hand, as seen from here — never smaller than a thumbnail, so a newcomer catches it
		// mid-swing at any distance.
		const FVector Edge = Project(W + CamRight * 60.f);
		const float Base = FMath::Clamp(static_cast<float>(FVector2D::Distance(FVector2D(At.X, At.Y), FVector2D(Edge.X, Edge.Y))), 34.f * S, 160.f * S);
		const FVector2D C(At.X, At.Y);
		if (!bRing)
		{
			// Easy: the swing is coming (no timing in it).
			RingCircle(C, Base * 1.15f, FLinearColor(1.f, 0.86f, 0.72f, 0.3f), FMath::Max(1.f, 1.5f * S));
			continue;
		}
		const float R = Base * FMath::Lerp(1.f, 0.45f, Cue.Progress); // closes as the window does
		if (!bLit)
		{
			// The window is open, but you cannot get a parry out in time (mid-swing, stunned, a parry already spent).
			RingCircle(C, R, WithAlpha(ParryGrey, 0.75f), 3.5f * S);
			continue;
		}
		const float Strength = bLast ? 1.f : 0.8f;
		FillCircle(C, R * (1.3f + 0.12f * Pulse), WithAlpha(ParryRed, (bLast ? 0.3f : 0.15f) * (0.65f + 0.35f * Pulse)));
		RingCircle(C, R, WithAlpha(ParryRed, Strength), (bLast ? 8.f : 5.5f) * S);
		RingCircle(C, R + 4.f * S, WithAlpha(FLinearColor(1.f, 0.6f, 0.55f), 0.4f * Strength), FMath::Max(1.f, 1.5f * S));
		if (!bKeyShown)
		{
			ShadowText(KeyName(static_cast<uint8>(EHWBind::Parry), PadActive()), static_cast<float>(C.X), static_cast<float>(C.Y) + Base * 1.3f + 6.f * S, ParryRed,
				GEngine->GetMediumFont(), 1.05f * S, true);
			bKeyShown = true;
		}
	}
}

void AHWHUD::DrawOverlay(UHWDuelSubsystem* Duel)
{
	// A2 numeric overlay — the CSV must reconcile against these numbers.
	const float S = UI;
	UFont* Small = GEngine->GetSmallFont();
	const HW::FEncounter& E = *Duel->GetEncounter();
	const HW::FFighter& P = E.Duel.Get(HW::ESide::Player);
	const HW::FFighter& B = E.Duel.Get(HW::ESide::Boss);
	const HW::FBrainDecision& D = E.LastDecision();
	const HW::FEncounterStats& St = E.Stats;
	const HW::IBossBrain* Brain = E.Brain();
	const HW::FRLBrain* RL = Duel->GetRLBrain();

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("frame %d   mean frame %.2f ms   %s   distance %.0f"), E.Duel.Frame, Duel->GetMeanFrameMs(),
		Brain != nullptr ? UTF8_TO_TCHAR(Brain->Name()) : TEXT("no brain"), Duel->FighterDistance()));
	Lines.Add(FString::Printf(TEXT("player  %-10s %-14s T%-3d HP %5.0f  SC %5.1f  FA %+d"), *ToFString(HW::FighterStateName(P.State)),
		*ToFString(HW::Move(P.Move).Name), P.T, P.Health, P.ShaChi, E.Duel.FrameAdvantage(HW::ESide::Player)));
	Lines.Add(FString::Printf(TEXT("boss    %-10s %-14s T%-3d HP %5.0f  SC %5.1f"), *ToFString(HW::FighterStateName(B.State)),
		*ToFString(HW::Move(B.Move).Name), B.T, B.Health, B.ShaChi));
	{
		// The fight's assist and damage (start-of-fight values), the duel's speed now and the cue (D = frames to the real impact).
		const FHWParryCue& Cue = Duel->GetParryCue();
		const FString CueText = !Cue.bActive ? FString(TEXT("-"))
			: FString::Printf(TEXT("D %d  window %d..%d  %s"), Cue.FramesToImpact, Cue.WindowFirst, Cue.WindowLast,
				Cue.IsLit() ? TEXT("LIT") : (Cue.bInWindow ? TEXT("grey") : TEXT("incoming")));
		Lines.Add(FString::Printf(TEXT("assist %s  slow x%.2f (now x%.2f)  keeper damage x%.2f  insight %.0f%%  cue %s"), *Duel->GetFightAssistName(),
			Duel->GetFightSlowmoScale(), Duel->GetTimeScale(), Duel->GetFightKeeperDamageScale(), Duel->GetFightInsight() * 100.f, *CueText));
	}
	const int32 Decisions = Brain != nullptr ? Brain->Decisions() : 0;
	if (RL != nullptr)
	{
		// The RL keeper: what it chose and how sure it was, what its read head expects you to answer, how it rates the
		// position, and how much of you it remembers (RL.md section 5.3: the F3 overlay).
		const HW::FRLSession* Mem = Duel->GetSession() != nullptr ? &Duel->GetSession()->GetMemory() : nullptr;
		Lines.Add(FString::Printf(TEXT("decision #%d  %s  [%s]  %s  prev %s  FA %+d  value %+.2f"), Decisions,
			D.Chosen != HW::EMoveId::None ? *ToFString(HW::Move(D.Chosen).Name) : TEXT("Wait"), *ToFString(HW::DecisionKindName(D.Kind)),
			D.bChain ? TEXT("chain") : TEXT(""), *ToFString(HW::OutcomeName(D.PrevOutcome)), D.FrameAdvantage, D.Value));
		Lines.Add(FString::Printf(TEXT("read head: you will %s  p=%.2f  (%.2f bits of certainty)"), *ToFString(HW::SymName(D.Predicted)), D.PredictedP, D.ReadBits));
		Lines.Add(FString::Printf(TEXT("keeper: the %s  skill %.2f (sees you %d frames late, strings of %d)%s%s"), UTF8_TO_TCHAR(HW::RL::KeeperName(Duel->GetKeeperIdentity())),
				Duel->GetKeeperSkill(), HW::RL::SkillParams(Duel->GetKeeperSkill()).Perception, HW::RL::SkillParams(Duel->GetKeeperSkill()).MaxString,
				Duel->GetKeeperTemperature() > 0.f ? *FString::Printf(TEXT("  sampling T %.1f"), Duel->GetKeeperTemperature()) : TEXT(""),
				Duel->GetKeeperSwingGap() > 0 ? *FString::Printf(TEXT("  breather %d f"), Duel->GetKeeperSwingGap()) : TEXT("")));
		if (Mem != nullptr)
		{
			Lines.Add(FString::Printf(TEXT("memory: fight %d of this session, %d exchanges remembered, %d keeper swings seen"),
				Mem->FightIndex + 1, Mem->NumTokens, Mem->BossSwingsSeen));
		}
		Lines.Add(FString::Printf(TEXT("swings/min %.1f  dmg/min %.1f  reads landed %d  player dmg taken %.0f  boss dmg taken %.0f"),
			St.SwingsPerMin(), St.DamagePerMin(), St.CountersLanded, St.PlayerDamageTaken, St.BossDamageTaken));
		for (int32 K = 0; K < 3 && K < D.NumTop; ++K)
		{
			Lines.Add(FString::Printf(TEXT("  top%d %-14s p %.2f  logit %+6.2f"), K + 1,
				D.Top[K].Move != HW::EMoveId::None ? *ToFString(HW::Move(D.Top[K].Move).Name) : TEXT("Wait"), D.Top[K].Score, D.Top[K].Expected));
		}
	}
	else
	{
		Lines.Add(FString::Printf(TEXT("decision #%d  slot %s%s  %s -> %s  [%s]  prev %s  FA %+d"), Decisions,
			*ToFString(HW::SlotTypeName(D.Slot)), D.bChain ? TEXT(" chain") : TEXT(""), *ToFString(HW::Move(D.Scripted).Name),
			*ToFString(HW::Move(D.Chosen).Name), *ToFString(HW::DecisionKindName(D.Kind)), *ToFString(HW::OutcomeName(D.PrevOutcome)), D.FrameAdvantage));
		Lines.Add(FString::Printf(TEXT("swings/min %.1f  dmg/min %.1f  decisions %d  player dmg taken %.0f  boss dmg taken %.0f"),
			St.SwingsPerMin(), St.DamagePerMin(), St.Decisions, St.PlayerDamageTaken, St.BossDamageTaken));
	}
	Lines.Add(FString::Printf(TEXT("telemetry %s"), *Duel->GetTelemetryPath()));
	for (const FString& L : Duel->GetDecisionLog()) { Lines.Add(L); }

	// The hit volumes of the last contact queries (red = contact), on the canvas: DrawDebugBox is compiled out of Shipping.
	for (const HW::ESide Side : { HW::ESide::Player, HW::ESide::Boss })
	{
		const UHWDuelSubsystem::FHitVolume& V = Duel->GetLastHitVolume(Side);
		if (V.Frame < 0 || Duel->GetDuelFrame() - V.Frame >= 2) { continue; }
		FVector C[8];
		for (int32 K = 0; K < 8; ++K)
		{
			const FVector Corner((K & 1) ? V.Extent.X : -V.Extent.X, (K & 2) ? V.Extent.Y : -V.Extent.Y, (K & 4) ? V.Extent.Z : -V.Extent.Z);
			C[K] = Project(V.Center + V.Rotation.RotateVector(Corner));
		}
		const FLinearColor Edge = V.bContact ? FLinearColor::Red : FLinearColor::Yellow;
		for (int32 A = 0; A < 8; ++A)
		{
			for (int32 Bit = 1; Bit < 8; Bit <<= 1)
			{
				const int32 B2 = A | Bit; // the 12 edges: corners that differ in one axis
				if (B2 != A && C[A].Z > 0.f && C[B2].Z > 0.f) { DrawLine(C[A].X, C[A].Y, C[B2].X, C[B2].Y, Edge, 2.f); }
			}
		}
	}

	const float X = 30.f * S;
	float Y = 130.f * S;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X - 10.f * S, Y - 8.f * S, FMath::Min(Canvas->ClipX * 0.62f * HudScale, Canvas->ClipX - 40.f * S),
		(Lines.Num() * 17.f + 14.f) * S);
	for (const FString& L : Lines)
	{
		Text(L, X, Y, FLinearColor(0.75f, 1.f, 0.8f), Small, 1.f * S);
		Y += 17.f * S;
	}
}

void AHWHUD::DrawStartScreen(UHWDuelSubsystem* Duel)
{
	const float S = UI;
	const float CX = Canvas->ClipX * 0.5f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	Text(TEXT("HELLWALKER"), CX, Canvas->ClipY * 0.2f, Crimson, GEngine->GetLargeFont(), 3.2f * S, true);
	Text(FString::Printf(TEXT("%s reads you.  It adapts.  It counterattacks."), *Duel->BossTitle), CX, Canvas->ClipY * 0.2f + 110.f * S, Ink, GEngine->GetMediumFont(), 1.3f * S, true);

	const UHWSessionSubsystem* Session = Duel->GetSession();
	const bool bBlind = Session != nullptr && Session->bBlind;
	const bool bPad = PadActive();
	const float Y = Canvas->ClipY * 0.45f;
	if (bBlind)
	{
		Text(bPad ? TEXT("[D-pad left]  VARIANT A") : TEXT("[1]  VARIANT A"), CX, Y, Ink, GEngine->GetLargeFont(), 1.4f * S, true);
		Text(bPad ? TEXT("[D-pad right]  VARIANT B") : TEXT("[2]  VARIANT B"), CX, Y + 60.f * S, Ink, GEngine->GetLargeFont(), 1.4f * S, true);
	}
	else
	{
		Text(FString::Printf(TEXT("[%s]  NORMAL  -  a fixed pattern. Learn it."), bPad ? TEXT("D-pad left") : TEXT("1")), CX, Y, Dim,
			GEngine->GetLargeFont(), 1.2f * S, true);
		Text(FString::Printf(TEXT("[%s]  ADAPTIVE AI  -  it learns you. Stay unpredictable."), bPad ? TEXT("D-pad right") : TEXT("2")), CX, Y + 60.f * S, Ember,
			GEngine->GetLargeFont(), 1.2f * S, true);
	}
	Text(TEXT("The keepers remember you for the whole session.  They forget when you quit."), CX, Y + 150.f * S, Dim, GEngine->GetMediumFont(), 1.f * S, true);
	Text(bPad ? TEXT("[Start]  menu: settings, difficulty, how the keeper learns you") : TEXT("[Esc]  menu: settings, difficulty, how the keeper learns you"),
		CX, Y + 186.f * S, Dim, GEngine->GetMediumFont(), 1.f * S, true);
	DrawControls(1.f);
}

void AHWHUD::DrawEndScreen(UHWDuelSubsystem* Duel)
{
	const float S = UI;
	const float CX = Canvas->ClipX * 0.5f;
	const bool bWon = Duel->GetState() == EHWEncounterState::PlayerWon;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), 0.f, Canvas->ClipY * 0.3f, Canvas->ClipX, FMath::Max(Canvas->ClipY * 0.32f, 230.f * S));
	Text(bWon ? TEXT("THE WARDEN FALLS") : TEXT("YOU DIED"), CX, Canvas->ClipY * 0.33f, bWon ? Gold : Crimson, GEngine->GetLargeFont(), 2.6f * S, true);

	const HW::FEncounter& E = *Duel->GetEncounter();
	const HW::FEncounterStats& St = E.Stats;
	Text(FString::Printf(TEXT("%.1f s   -   you dealt %.0f   -   you took %.0f   -   the Warden landed %d reads"),
		St.Frames / 60.f, St.BossDamageTaken, St.PlayerDamageTaken, St.CountersLanded), CX, Canvas->ClipY * 0.33f + 95.f * S, Ink,
		GEngine->GetMediumFont(), 1.1f * S, true);

	const UHWSessionSubsystem* Session = Duel->GetSession();
	if (Session != nullptr && !Session->bBlind)
	{
		// The naming test (PLAN 1.1), from the keeper's side: what its read head expects of you against a fast swing.
		HW::ESym Sym = HW::ESym::Neutral;
		float P = 0.f;
		if (Session->PredictAnswer(HW::EMoveId::BFastSlash, Sym, P))
		{
			Text(FString::Printf(TEXT("What it expects of you against a fast swing:  %s  (%.0f%%)"), *HumanSym(Sym), P * 100.f),
				CX, Canvas->ClipY * 0.33f + 130.f * S, Dim, GEngine->GetMediumFont(), 1.f * S, true);
		}
		DrawNotebookLine(Duel, CX, Canvas->ClipY * 0.33f + 210.f * S);
	}
	const bool bBlind = Session != nullptr && Session->bBlind; // blind (B4): the keys name variants, never the modes
	const FString Modes = bBlind ? FString(TEXT("Variant A / B")) : FString(TEXT("Normal / Adaptive AI"));
	const FString Again = PadActive()
		? FString::Printf(TEXT("[Start] menu: restart - it remembers        [D-pad left / right] %s"), *Modes)
		: FString::Printf(TEXT("[%s] fight again - it remembers        [1] / [2] %s        [Esc] menu"), *KeyName(static_cast<uint8>(EHWBind::Restart), false), *Modes);
	Text(Again, CX, Canvas->ClipY * 0.33f + 170.f * S, Ink, GEngine->GetMediumFont(), 1.f * S, true);
}

void AHWHUD::DrawControls(float Alpha)
{
	if (Alpha <= 0.f) { return; }
	const float S = UI;
	const bool bPad = PadActive();
	auto K = [this, bPad](EHWBind A) { return KeyName(static_cast<uint8>(A), bPad); };
	const FLinearColor C = WithAlpha(Dim, Alpha);
	const FLinearColor KC = WithAlpha(Ink, Alpha * 0.85f);
	struct FCell { FString Key; const TCHAR* What; };
	const FCell Cells[4][2] = {
		{ { K(EHWBind::Light), TEXT("light (chain)") }, { K(EHWBind::Heavy), TEXT("heavy") } },
		{ { K(EHWBind::Guard), TEXT("block (hold)") }, { K(EHWBind::Parry), TEXT("parry") } },
		{ { bPad ? FString(TEXT("A + stick")) : K(EHWBind::Step) + TEXT(" + WASD"), TEXT("ghoststep") }, { K(EHWBind::Switch), TEXT("switch weapon") } },
		{ { K(EHWBind::LockOn), TEXT("lock-on") }, { bPad ? FString(TEXT("Start")) : FString(TEXT("Esc")), TEXT("menu") } },
	};
	UFont* Font = GEngine->GetSmallFont();
	const float X = Canvas->ClipX - 480.f * S;
	float Y = Canvas->ClipY - 110.f * S;
	for (const auto& Row : Cells)
	{
		for (int32 Col = 0; Col < 2; ++Col)
		{
			const float CX = X + Col * 260.f * S;
			Text(Row[Col].Key, CX, Y, KC, Font, 1.1f * S);
			Text(Row[Col].What, CX + TextWidth(Row[Col].Key, Font, 1.1f * S) + 10.f * S, Y, C, Font, 1.1f * S);
		}
		Y += 22.f * S;
	}
}

// =================================================================================================
// The open world

void AHWHUD::DrawOpenWorld(AHWOpenWorldGameMode* GM, UHWDuelSubsystem* Duel)
{
	switch (GM->GetPhase())
	{
	case EHWWorldPhase::Title:
		DrawTitle(GM);
		return;
	case EHWWorldPhase::Exploring:
	{
		// Under the valley map the exploring HUD would only show through its veil.
		const AHWPlayerController* PC = Cast<AHWPlayerController>(GetOwningPlayerController());
		if (PC == nullptr || !PC->IsMapOpen()) { DrawExplore(GM); }
		break;
	}
	case EHWWorldPhase::Duel:
	case EHWWorldPhase::DuelOver:
		if (Duel != nullptr && Duel->GetEncounter() != nullptr)
		{
			DrawWorldMarkers(Duel);
			DrawParryCue(Duel);
			DrawBars(Duel);
			DrawReadMeter(Duel);
			if (Duel->bDebugDraw) { DrawOverlay(Duel); }
			if (GM->GetPhase() == EHWWorldPhase::DuelOver) { DrawDuelResult(GM, Duel); }
			else { DrawControls(FMath::Clamp(1.f - (GM->GetPhaseSeconds() - 10.f) / 4.f, 0.f, 1.f)); }
		}
		break;
	case EHWWorldPhase::Ending:
		DrawEnding(GM, Duel, false);
		return;
	case EHWWorldPhase::Regret:
		DrawEnding(GM, Duel, true);
		return;
	}
	DrawBanner(GM);
}

void AHWHUD::DrawTitle(AHWOpenWorldGameMode* GM)
{
	// The logo and the line; the choices are the title menu (DrawTitleMenu), open whenever the title is.
	(void)GM;
	const float S = UI;
	const float CX = Canvas->ClipX * 0.5f;
	const float LogoY = FMath::Max(36.f, Canvas->ClipY / S * 0.12f) * S;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	Text(TEXT("HELLWALKER"), CX, LogoY, Crimson, GEngine->GetLargeFont(), 3.4f * S, true);
	Text(TEXT("An ash valley.  Three seals.  In Adaptive AI every keeper reads you  -  and they all read the same you."), CX, LogoY + 112.f * S,
		Ink, GEngine->GetMediumFont(), 1.25f * S, true);
}

void AHWHUD::DrawCompass(AHWOpenWorldGameMode* GM)
{
	APlayerController* PCtrl = GetOwningPlayerController();
	if (PCtrl == nullptr) { return; }
	const float S = UI;
	const float CX = Canvas->ClipX * 0.5f;
	const float W = FMath::Min(760.f * S, Canvas->ClipX * 0.6f);
	const float Y = 34.f * S;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), CX - W * 0.5f, Y - 6.f * S, W, 30.f * S);
	FVector Eye;
	FRotator View;
	PCtrl->GetPlayerViewPoint(Eye, View);
	const float Yaw = static_cast<float>(View.Yaw);

	// Cardinal ticks.
	const TCHAR* Names[] = { TEXT("E"), TEXT("S"), TEXT("W"), TEXT("N") }; // +X east, +Y south in this valley
	for (int32 I = 0; I < 4; ++I)
	{
		const float D = FMath::FindDeltaAngleDegrees(Yaw, 90.f * I);
		if (FMath::Abs(D) > 90.f) { continue; }
		Text(Names[I], CX + D / 90.f * W * 0.5f, Y - 2.f * S, Dim, GEngine->GetSmallFont(), 1.1f * S, true);
	}
	TArray<AHWOpenWorldGameMode::FMarker> Markers;
	GM->GetMarkers(Markers);
	FVector From = Eye;
	float FromYaw = 0.f;
	GM->GetPlayerSpot(From, FromYaw); // distances from where you stand, as on the map
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Clock * 5.f);
	auto Diamond = [this](float X, float CY, float R, const FLinearColor& C, float T)
	{
		DrawLine(X, CY - R, X + R, CY, C, T);
		DrawLine(X + R, CY, X, CY + R, C, T);
		DrawLine(X, CY + R, X - R, CY, C, T);
		DrawLine(X - R, CY, X, CY - R, C, T);
	};
	for (const AHWOpenWorldGameMode::FMarker& M : Markers)
	{
		const FVector To = M.Location - Eye;
		const float D = FMath::FindDeltaAngleDegrees(Yaw, static_cast<float>(To.Rotation().Yaw));
		const float X = CX + FMath::Clamp(D, -90.f, 90.f) / 90.f * W * 0.5f;
		if (M.bTracked)
		{
			// The tracked keeper: larger, filled, pulsing, ringed in gold.
			const float R = (8.f + 2.f * Pulse) * S;
			FillDiamond(FVector2D(X, Y + 6.f * S), R, M.Color);
			Diamond(X, Y + 6.f * S, R + 3.f * S, WithAlpha(Gold, 0.55f + 0.45f * Pulse), 2.f * S);
		}
		else
		{
			Diamond(X, Y + 6.f * S, (M.bPrimary ? 7.f : 5.f) * S, M.Color, 2.f * S);
		}

		// In-world marker with the distance, for anything ahead within reason (the tracked keeper's: DrawKeeperTracking).
		const float Dist = static_cast<float>(FVector::Dist2D(M.Location, From)) / 100.f;
		const FVector P = Project(M.Location);
		if (!M.bTracked && P.Z > 0.f && Dist < 900.f && Dist > 25.f && P.X > 0.f && P.X < Canvas->ClipX && P.Y > 0.f && P.Y < Canvas->ClipY)
		{
			FLinearColor C = M.Color;
			C.A = FMath::Clamp(1.2f - Dist / 900.f, 0.35f, 1.f);
			Text(FString::Printf(TEXT("%s  %.0f m"), *M.Label, Dist), P.X, P.Y, C, GEngine->GetSmallFont(), 1.1f * S, true);
		}
	}
}

void AHWHUD::DrawKeeperTracking(AHWOpenWorldGameMode* GM, float TopInset, float BottomInset, const TArray<FBox2D>& Avoid)
{
	APlayerController* PCtrl = GetOwningPlayerController();
	if (PCtrl == nullptr) { return; }
	const float S = UI;
	FVector Eye;
	FRotator View;
	PCtrl->GetPlayerViewPoint(Eye, View);
	const float Fov = PCtrl->PlayerCameraManager != nullptr ? PCtrl->PlayerCameraManager->GetFOVAngle() : 90.f;
	const FVector2D Screen(Canvas->ClipX, Canvas->ClipY);
	const double Focal = HWMap::FocalPixels(Screen.X, Fov);
	HWMap::FInsets In;
	In.Left = 58.f * S;
	In.Right = 58.f * S;
	In.Top = TopInset;
	In.Bottom = BottomInset;
	FVector From = Eye;
	float FromYaw = 0.f;
	GM->GetPlayerSpot(From, FromYaw);

	TArray<AHWOpenWorldGameMode::FMarker> Markers;
	GM->GetMarkers(Markers);
	Markers.RemoveAll([](const AHWOpenWorldGameMode::FMarker& M) { return M.Keeper < 0; });
	Markers.StableSort([](const AHWOpenWorldGameMode::FMarker& A, const AHWOpenWorldGameMode::FMarker& B) { return !A.bTracked && B.bTracked; }); // tracked on top
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Clock * 5.f);
	TArray<FBox2D> Taken = Avoid; // + the labels drawn so far (two keepers in one direction must not print over each other)
	for (const AHWOpenWorldGameMode::FMarker& M : Markers)
	{
		const float Dist = static_cast<float>(FVector::Dist2D(M.Location, From)) / 100.f;
		const FString Label = FString::Printf(TEXT("%s  %.0f m"), *M.Label, Dist);
		UFont* Font = M.bTracked ? GEngine->GetMediumFont() : GEngine->GetSmallFont();
		const float TS = (M.bTracked ? 0.95f : 1.05f) * S;
		float TW = 0.f;
		float TH = 0.f;
		GetTextSize(Label, TW, TH, Font, TS);
		const HWMap::FEdgeMarker E = HWMap::PlaceEdgeMarker(HWMap::ToCameraSpace(Eye, View, M.Location), Screen, Focal, In);
		if (E.bOnScreen)
		{
			// On screen: the compass's world label covers the others (within 900 m); the tracked keeper is marked at any distance.
			if (!M.bTracked) { continue; }
			const FVector P = Project(M.Location);
			const FVector2D At = P.Z > 0.f ? FVector2D(P.X, P.Y) : E.Pos;
			const float R = (8.f + 3.f * Pulse) * S;
			FillDiamond(At, R + 2.5f * S, FLinearColor(0.f, 0.f, 0.f, 0.6f));
			FillDiamond(At, R, M.Color);
			RingCircle(At, R + (7.f + 3.f * Pulse) * S, WithAlpha(Gold, 0.35f + 0.5f * Pulse), 2.f * S);
			const FVector2D LabelAt(FMath::Clamp(static_cast<float>(At.X), 14.f * S + TW * 0.5f, Canvas->ClipX - 14.f * S - TW * 0.5f), At.Y + R + 14.f * S);
			const FBox2D LabelBox(FVector2D(LabelAt.X - TW * 0.5f - 6.f * S, LabelAt.Y - 3.f * S), FVector2D(LabelAt.X + TW * 0.5f + 6.f * S, LabelAt.Y + TH + 3.f * S));
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), LabelBox.Min.X, LabelBox.Min.Y, LabelBox.Max.X - LabelBox.Min.X, LabelBox.Max.Y - LabelBox.Min.Y);
			Text(Label, LabelAt.X, LabelAt.Y, Gold, Font, TS, true);
			Taken.Add(LabelBox);
			continue;
		}

		// Off screen: a notched arrowhead on the inset border pointing at it, its name and distance on the inner side.
		const float A = (M.bTracked ? 17.f + 3.f * Pulse : 12.f) * S;
		const FVector2D D = E.Dir;
		const FVector2D Side(-D.Y, D.X);
		auto Head = [&](float K, const FLinearColor& C)
		{
			const FVector2D Tip = E.Pos + D * (A * K);
			const FVector2D L = E.Pos - D * (A * 0.55f * K) + Side * (A * 0.6f * K);
			const FVector2D R = E.Pos - D * (A * 0.55f * K) - Side * (A * 0.6f * K);
			const FVector2D Notch = E.Pos - D * (A * 0.1f * K);
			FillTriangle(Tip, L, Notch, C);
			FillTriangle(Tip, Notch, R, C);
		};
		if (M.bTracked) { Head(1.45f, WithAlpha(Gold, 0.5f + 0.45f * Pulse)); }
		Head(1.22f, FLinearColor(0.f, 0.f, 0.f, 0.75f));
		Head(1.f, M.Color);
		const FVector2D Anchor = E.Pos - D * (A * 0.75f + 10.f * S);
		float TX = static_cast<float>(Anchor.X - TW * 0.5f * (1.0 + D.X));
		float TY = static_cast<float>(Anchor.Y - TH * 0.5f * (1.0 + D.Y));
		TX = FMath::Clamp(TX, 8.f * S, Canvas->ClipX - 8.f * S - TW);
		TY = FMath::Clamp(TY, TopInset - TH, Canvas->ClipY - 8.f * S - TH);
		FBox2D LabelBox(FVector2D(TX - 6.f * S, TY - 3.f * S), FVector2D(TX + TW + 6.f * S, TY + TH + 3.f * S));
		const float Push = (TH + 8.f * S) * (TY + TH * 0.5f > Canvas->ClipY * 0.5f ? -1.f : 1.f); // stack toward the middle of the screen
		for (int32 Try = 0; Try < 6 && Taken.ContainsByPredicate([&LabelBox](const FBox2D& Other) { return Other.Intersect(LabelBox); }); ++Try)
		{
			TY += Push;
			LabelBox = LabelBox.ShiftBy(FVector2D(0.0, Push));
		}
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), LabelBox.Min.X, LabelBox.Min.Y, LabelBox.Max.X - LabelBox.Min.X, LabelBox.Max.Y - LabelBox.Min.Y);
		Text(Label, TX, TY, M.bTracked ? Gold : WithAlpha(Ink, 0.92f), Font, TS);
		Taken.Add(LabelBox);
	}
}

void AHWHUD::DrawExplore(AHWOpenWorldGameMode* GM)
{
	const float S = UI;
	UFont* Small = GEngine->GetSmallFont();
	UFont* Medium = GEngine->GetMediumFont();
	UFont* Large = GEngine->GetLargeFont();
	DrawCompass(GM);

	// Measured first, drawn last: the objective and the tracking line (top left), the prompt (centre), the exploring keys
	// (bottom right, the first 20 s). The keepers' edge arrows keep clear of all of them.
	const FString Objective = GM->ObjectiveText();
	float OW = 0.f;
	float OH = 0.f;
	GetTextSize(Objective.IsEmpty() ? FString(TEXT("Ag")) : Objective, OW, OH, Medium, 1.1f * S);
	const float TrackY = 34.f * S + OH + 4.f * S;
	TArray<AHWOpenWorldGameMode::FKeeperView> Keepers;
	GM->GetKeepers(Keepers);
	const int32 Tracked = GM->GetTrackedKeeper();
	const AHWOpenWorldGameMode::FKeeperView* K = Keepers.FindByPredicate([Tracked](const AHWOpenWorldGameMode::FKeeperView& V) { return V.Index == Tracked; });
	FString Line;
	float LW = 0.f;
	float LH = 0.f;
	if (K != nullptr)
	{
		FVector From = FVector::ZeroVector;
		float FromYaw = 0.f;
		GM->GetPlayerSpot(From, FromYaw);
		Line = FString::Printf(TEXT("Tracking: %s  %.0f m%s"), *K->Title, FVector::Dist2D(From, K->Location) / 100.0, K->bSealed ? TEXT("  (sealed)") : TEXT(""));
		GetTextSize(Line, LW, LH, Medium, 0.9f * S);
	}

	const FString& Prompt = GM->GetPrompt();
	const float PromptY = Canvas->ClipY * 0.74f;
	float PW = 0.f;
	float PH = 0.f;
	if (!Prompt.IsEmpty()) { GetTextSize(Prompt, PW, PH, Large, 1.f * S); }
	const FBox2D PromptBox(FVector2D(Canvas->ClipX * 0.5f - PW * 0.5f - 18.f * S, PromptY - 8.f * S), FVector2D(Canvas->ClipX * 0.5f + PW * 0.5f + 18.f * S, PromptY + PH + 8.f * S));

	const float HelpA = FMath::Clamp(1.f - (GM->GetPhaseSeconds() - 20.f) / 4.f, 0.f, 1.f);
	const bool bPad = PadActive();
	const FString HelpLines[] = {
		bPad ? FString(TEXT("Left stick  move    LB  sprint    A  jump / vault / mantle / climb"))
			: FString(TEXT("WASD  move    Shift  sprint    Space  jump / vault / mantle / climb")),
		bPad ? FString(TEXT("B  crouch (running: slide)    X  ragdoll (A: get up)    RB  walk"))
			: FString(TEXT("C  crouch (running: slide)    R  ragdoll (Space: get up)    Ctrl  walk")),
		FString::Printf(TEXT("%s  ring a bell / challenge a keeper    %s  map    %s  all controls    %s  menu"), *KeyName(static_cast<uint8>(EHWBind::Interact), bPad),
			*KeyName(static_cast<uint8>(EHWBind::Map), bPad), *KeyName(static_cast<uint8>(EHWBind::Help), bPad), bPad ? TEXT("Start") : TEXT("Esc")),
	};
	float Widest = 0.f;
	for (const FString& L : HelpLines) { Widest = FMath::Max(Widest, TextWidth(L, Small, 1.1f * S)); }
	const float HelpX = Canvas->ClipX - 40.f * S - Widest;
	const float HelpY = Canvas->ClipY - 92.f * S;
	const FBox2D HelpBox(FVector2D(HelpX - 10.f * S, HelpY - 6.f * S), FVector2D(Canvas->ClipX - 30.f * S, HelpY + 3.f * 22.f * S + 4.f * S));

	TArray<FBox2D> Avoid;
	if (!Prompt.IsEmpty()) { Avoid.Add(PromptBox); }
	if (HelpA > 0.f) { Avoid.Add(HelpBox); }
	// Below the compass, the objective and the tracking line; above the keys while they show (eased as they fade).
	DrawKeeperTracking(GM, FMath::Max(TrackY + LH + 30.f * S, 110.f * S), (74.f + 78.f * HelpA) * S, Avoid);

	// A soft backing so the two lines read over a bright slope too.
	const FString MapHint = FString::Printf(TEXT("[%s]  map"), *KeyName(static_cast<uint8>(EHWBind::Map), bPad));
	const float HintW = K != nullptr ? TextWidth(MapHint, Small, 1.0f * S) : 0.f;
	const float BlockW = FMath::Max(OW, K != nullptr ? LW + 16.f * S + HintW : 0.f);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.3f), 30.f * S, 28.f * S, BlockW + 20.f * S, TrackY - 28.f * S + (K != nullptr ? LH + 6.f * S : 0.f));
	if (K != nullptr)
	{
		ShadowText(Line, 40.f * S, TrackY, Gold, Medium, 0.9f * S);
		ShadowText(MapHint, 40.f * S + LW + 16.f * S, TrackY + 3.f * S, Dim, Small, 1.0f * S);
	}
	Text(Objective, 40.f * S, 34.f * S, Ink, Medium, 1.1f * S);
	if (const UHWSaveGame* Save = GM->GetSave())
	{
		// The mode as players know it (a retired 66 Days walk plays, and reads, as Adaptive AI).
		const FString Right = ModeLabel(Save->Mode == EHWPlayMode::Pathbreaker ? EHWTier::Pathbreaker : EHWTier::Hellwalker);
		Text(Right, Canvas->ClipX - 40.f * S - TextWidth(Right, Medium, 1.f * S), 34.f * S, Save->Mode == EHWPlayMode::Pathbreaker ? Dim : Ember, Medium, 1.f * S);
	}
	if (!Prompt.IsEmpty())
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), PromptBox.Min.X, PromptBox.Min.Y, PromptBox.Max.X - PromptBox.Min.X, PromptBox.Max.Y - PromptBox.Min.Y);
		Text(Prompt, Canvas->ClipX * 0.5f, PromptY, Gold, Large, 1.f * S, true);
	}
	if (HelpA > 0.f)
	{
		float Y = HelpY;
		for (const FString& L : HelpLines)
		{
			Text(L, HelpX, Y, WithAlpha(Dim, HelpA), Small, 1.1f * S);
			Y += 22.f * S;
		}
	}
}

void AHWHUD::DrawBanner(AHWOpenWorldGameMode* GM)
{
	FString Title;
	FString Sub;
	float Alpha = 0.f;
	if (!GM->GetBanner(Title, Sub, Alpha)) { return; }
	const float S = UI;
	const float CX = Canvas->ClipX * 0.5f;
	const float Y = Canvas->ClipY * 0.2f;
	Text(Title, CX, Y, WithAlpha(Ink, Alpha), GEngine->GetLargeFont(), 2.f * S, true);
	DrawRect(WithAlpha(Crimson, Alpha * 0.8f), CX - 220.f * S, Y + 74.f * S, 440.f * S, 2.f * S);
	if (!Sub.IsEmpty()) { Text(Sub, CX, Y + 86.f * S, WithAlpha(Gold, Alpha), GEngine->GetMediumFont(), 1.2f * S, true); }
}

void AHWHUD::DrawDuelResult(AHWOpenWorldGameMode* GM, UHWDuelSubsystem* Duel)
{
	const float S = UI;
	const float CX = Canvas->ClipX * 0.5f;
	const bool bWon = GM->LastDuelWon();
	const float A = FMath::Clamp(GM->GetPhaseSeconds() / 0.8f, 0.f, 1.f);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f * A), 0.f, Canvas->ClipY * 0.3f, Canvas->ClipX, FMath::Max(Canvas->ClipY * 0.3f, 210.f * S));
	Text(bWon ? FString::Printf(TEXT("%s FALLS"), *Duel->BossTitle.ToUpper()) : FString(TEXT("YOU DIED")), CX, Canvas->ClipY * 0.33f,
		WithAlpha(bWon ? Gold : Crimson, A), GEngine->GetLargeFont(), 2.4f * S, true);
	const HW::FEncounterStats& St = Duel->GetEncounter()->Stats;
	Text(FString::Printf(TEXT("%.1f s   -   you dealt %.0f   -   you took %.0f   -   it landed %d reads"),
		St.Frames / 60.f, St.BossDamageTaken, St.PlayerDamageTaken, St.CountersLanded), CX, Canvas->ClipY * 0.33f + 90.f * S, Ink,
		GEngine->GetMediumFont(), 1.1f * S, true);
	if (const UHWSessionSubsystem* Session = Duel->GetSession())
	{
		HW::ESym Sym = HW::ESym::Neutral;
		float P = 0.f;
		if (Session->PredictAnswer(HW::EMoveId::BFastSlash, Sym, P))
		{
			Text(FString::Printf(TEXT("What they have learned: against a fast swing you %s  (%.0f%%)"), *HumanSym(Sym).ToLower(), P * 100.f),
				CX, Canvas->ClipY * 0.33f + 125.f * S, Dim, GEngine->GetMediumFont(), 1.f * S, true);
		}
		DrawNotebookLine(Duel, CX, Canvas->ClipY * 0.33f + 205.f * S);
	}
	Text(bWon ? TEXT("The seal breaks...") : TEXT("You wake again..."), CX, Canvas->ClipY * 0.33f + 165.f * S, Dim,
		GEngine->GetMediumFont(), 1.f * S, true);
}

void AHWHUD::DrawEnding(AHWOpenWorldGameMode* GM, UHWDuelSubsystem* Duel, bool bRegret)
{
	const float S = UI;
	const float CX = Canvas->ClipX * 0.5f;
	const float A = FMath::Clamp(GM->GetPhaseSeconds() / 2.f, 0.f, 1.f);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f * A), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	const FLinearColor Head = WithAlpha(bRegret ? Crimson : Gold, A);
	const FLinearColor Body = WithAlpha(Ink, A);
	const FLinearColor Low = WithAlpha(Dim, A);
	float Y = Canvas->ClipY * 0.22f;
	if (bRegret)
	{
		Text(TEXT("THE SIXTY-SIXTH DAY"), CX, Y, Head, GEngine->GetLargeFont(), 2.6f * S, true);
		Text(TEXT("The heart gives out on the road.  This story is over, and its save is gone."), CX, Y + 110.f * S, Body, GEngine->GetMediumFont(), 1.3f * S, true);
	}
	else
	{
		Text(TEXT("THE SEALS ARE BROKEN"), CX, Y, Head, GEngine->GetLargeFont(), 2.6f * S, true);
		const UHWSaveGame* EndSave = GM->GetSave();
		const bool bNormalWalk = EndSave != nullptr && EndSave->Mode == EHWPlayMode::Pathbreaker;
		Text(bNormalWalk ? TEXT("You learned their patterns and broke them, one keeper after another.")
			: TEXT("The Warden read you the whole way to its gate.  You were harder to read than it was."), CX, Y + 110.f * S, Body, GEngine->GetMediumFont(), 1.3f * S, true);
		if (const UHWSaveGame* Save = EndSave)
		{
			const FString Line = FString::Printf(TEXT("%d min in the valley   -   %d death%s   -   %s"), FMath::RoundToInt32(Save->PlaySeconds / 60.f), Save->Deaths,
				Save->Deaths == 1 ? TEXT("") : TEXT("s"), Save->Mode == EHWPlayMode::Pathbreaker ? TEXT("Normal") : TEXT("Adaptive AI"));
			Text(Line, CX, Y + 160.f * S, Low, GEngine->GetMediumFont(), 1.1f * S, true);
		}
	}
	const UHWSaveGame* WalkSave = GM->GetSave();
	const bool bAdaptiveWalk = WalkSave == nullptr || WalkSave->Mode != EHWPlayMode::Pathbreaker;
	if (Duel != nullptr && bAdaptiveWalk)
	{
		if (const UHWSessionSubsystem* Session = Duel->GetSession())
		{
			// What the keepers expect of you — shown only when they have a read (Normal's scripted keepers learn nothing).
			const HW::EMoveId After[] = { HW::EMoveId::BFastSlash, HW::EMoveId::BHeavyCleave, HW::EMoveId::BFeintMid };
			const TCHAR* Names[] = { TEXT("a fast swing"), TEXT("a heavy swing"), TEXT("a feint") };
			TArray<FString> Rows;
			for (int32 I = 0; I < 3; ++I)
			{
				HW::ESym Sym = HW::ESym::Neutral;
				float P = 0.f;
				if (!Session->PredictAnswer(After[I], Sym, P)) { continue; }
				Rows.Add(FString::Printf(TEXT("against %s  -  %s  (%.0f%%)"), Names[I], *HumanSym(Sym).ToLower(), P * 100.f));
			}
			if (Rows.Num() > 0)
			{
				Y = Canvas->ClipY * 0.52f;
				Text(TEXT("WHAT THEY LEARNED ABOUT YOU"), CX, Y, Low, GEngine->GetMediumFont(), 1.1f * S, true);
				for (int32 I = 0; I < Rows.Num(); ++I)
				{
					Text(Rows[I], CX, Y + (40.f + 34.f * I) * S, Body, GEngine->GetMediumFont(), 1.1f * S, true);
				}
			}
		}
	}
	Text(PadActive() ? TEXT("[Start]  menu  -  Quit to title for a new walk")
		: TEXT("[1] Normal     [2] Adaptive AI     -     a new walk        [Esc] menu"), CX, Canvas->ClipY * 0.86f, Low,
		GEngine->GetMediumFont(), 1.1f * S, true);
}

// =================================================================================================
// Menus
// =================================================================================================

void AHWHUD::DrawMenu(AHWPlayerController* PC)
{
	const FHWMenu& M = PC->GetMenu();
	if (M.GetPage() != EHWMenuPage::Title) { DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY); }
	switch (M.GetPage())
	{
	case EHWMenuPage::Title:    DrawTitleMenu(PC, M); break;
	case EHWMenuPage::Pause:    DrawPauseMenu(PC, M); break;
	case EHWMenuPage::Settings: DrawSettingsMenu(PC, M); break;
	case EHWMenuPage::Tutorial: DrawTutorial(PC, M); break;
	case EHWMenuPage::Notebook: DrawNotebookMenu(PC, M); break;
	case EHWMenuPage::Map:      DrawMapMenu(PC, M); break;
	default: break;
	}
	if (M.IsConfirming()) { DrawConfirm(PC, M); }
	if (M.IsCapturing()) { DrawCapture(PC, M); }
}

void AHWHUD::DrawMenuFooter(AHWPlayerController* PC, const FHWMenu& M, float X, float Y, float W, bool bCenter)
{
	const float S = UI;
	const bool bPad = PC->IsGamepadActive();
	const EHWMenuPage Page = M.GetPage();
	FString Hint;
	if (bPad)
	{
		switch (Page)
		{
		case EHWMenuPage::Settings: Hint = TEXT("D-pad  select     Left / Right  change     A  accept     B  back     LB / RB  tabs"); break;
		case EHWMenuPage::Tutorial: Hint = TEXT("Left / Right  turn the page     A  next     B  close"); break;
		case EHWMenuPage::Map:      Hint = TEXT("D-pad up / down  keeper     A  track it     LT / RT  zoom     right stick  pan     B / D-pad left  close"); break;
		default:                    Hint = TEXT("D-pad  select     A  accept     B  back"); break;
		}
		if (Page != EHWMenuPage::Title && Page != EHWMenuPage::Map) { Hint += TEXT("     Start  resume"); }
	}
	else
	{
		switch (Page)
		{
		case EHWMenuPage::Settings: Hint = TEXT("Up / Down  select     Left / Right  change     Enter  accept     Esc  back     Q / E  tabs"); break;
		case EHWMenuPage::Tutorial: Hint = TEXT("Left / Right  turn the page     Enter  next     Esc  close"); break;
		case EHWMenuPage::Title:    Hint = TEXT("Up / Down  select     Enter  accept     1 / 2  a new walk: Normal / Adaptive AI"); break;
		case EHWMenuPage::Map:
			Hint = FString::Printf(TEXT("Up / Down  keeper     Enter or click  track it     Wheel  zoom     Drag  pan     %s / Esc  close"),
				*KeyName(static_cast<uint8>(EHWBind::Map), false));
			break;
		default:                    Hint = TEXT("Up / Down  select     Enter  accept     Esc  back"); break;
		}
	}
	Text(Hint, bCenter ? X + W * 0.5f : X, Y, Dim, GEngine->GetSmallFont(), 1.1f * S, bCenter);
	const FString& Msg = PC->GetMenuMessage();
	const float A = PC->GetMenuMessageAlpha();
	if (!Msg.IsEmpty() && A > 0.f && !M.IsCapturing())
	{
		Text(Msg, bCenter ? X + W * 0.5f : X, Y - 32.f * S, WithAlpha(Gold, A), GEngine->GetMediumFont(), 1.05f * S, bCenter);
	}
}

void AHWHUD::DrawTitleMenu(AHWPlayerController* PC, const FHWMenu& M)
{
	const float S = UI;
	const float CX = Canvas->ClipX * 0.5f;
	const float HU = Canvas->ClipY / S;
	UFont* Large = GEngine->GetLargeFont();
	const TArray<FHWMenuItem>& Items = M.GetItems();
	const int32 N = Items.Num();
	const float LogoU = FMath::Max(36.f, HU * 0.12f);
	const float ListU = LogoU + 112.f + 74.f;
	const float RowU = FMath::Clamp((HU - 140.f - ListU) / FMath::Max(1, N), 38.f, 54.f);
	const float RowH = RowU * S;
	const float TS = 1.05f * S;
	const bool bBoxes = !M.IsConfirming();
	float TW = 0.f;
	float TH = 0.f;
	GetTextSize(TEXT("Ag"), TW, TH, Large, TS);
	float Y = ListU * S;
	for (int32 I = 0; I < N; ++I)
	{
		const FHWMenuItem& It = Items[I];
		const bool bSel = I == M.GetSelected();
		const float LW = TextWidth(It.Label, Large, TS);
		const float Half = LW * 0.5f + 70.f * S;
		const float BoxH = RowH - 6.f * S;
		if (bSel)
		{
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), CX - Half, Y, Half * 2.f, BoxH);
			DrawRect(Ember, CX - Half, Y, 3.f * S, BoxH);
			DrawRect(Ember, CX + Half - 3.f * S, Y, 3.f * S, BoxH);
		}
		const FLinearColor C = !It.bEnabled ? Disabled : (bSel ? FMath::Lerp(ToneColor(It.Tone), Bright, 0.35f) : WithAlpha(ToneColor(It.Tone), 0.85f));
		Text(It.Label, CX, Y + (BoxH - TH) * 0.5f, C, Large, TS, true);
		if (!It.Shortcut.IsEmpty() && !PC->IsGamepadActive())
		{
			const FString K = FString::Printf(TEXT("[%s]"), *It.Shortcut);
			Text(K, CX - Half + 14.f * S, Y + (BoxH - TH) * 0.5f + 4.f * S, WithAlpha(Dim, bSel ? 1.f : 0.7f), GEngine->GetSmallFont(), 1.1f * S);
		}
		if (bBoxes) { AddHitBox(FVector2D(CX - 320.f * S, Y), FVector2D(640.f * S, BoxH), Box(TEXT("I"), I), true); }
		Y += RowH;
	}
	if (const FHWMenuItem* Sel = M.GetSelectedItem())
	{
		if (!Sel->Hint.IsEmpty()) { Text(Sel->Hint, CX, Y + 10.f * S, Ink, GEngine->GetMediumFont(), 1.05f * S, true); }
	}
	Text(TEXT("The keepers remember you for the whole session.  They forget when you quit."), CX, Canvas->ClipY - 80.f * S, Dim,
		GEngine->GetMediumFont(), 1.f * S, true);
	DrawMenuFooter(PC, M, 0.f, Canvas->ClipY - 42.f * S, Canvas->ClipX, true);
}

void AHWHUD::DrawPauseMenu(AHWPlayerController* PC, const FHWMenu& M)
{
	const float S = UI;
	UFont* Medium = GEngine->GetMediumFont();
	const TArray<FHWMenuItem>& Items = M.GetItems();
	const int32 N = Items.Num();
	const float RowH = 52.f * S;
	const float PW = FMath::Min(680.f * S, Canvas->ClipX - 40.f * S);
	const float PH = 168.f * S + N * RowH + 150.f * S;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = FMath::Max(10.f * S, (Canvas->ClipY - PH) * 0.5f);
	const float CX = PX + PW * 0.5f;
	Panel(PX, PY, PW, PH);
	Text(M.GetInfo().Title, CX, PY + 26.f * S, Ink, GEngine->GetLargeFont(), 1.7f * S, true);

	// Where you are, and how hard the keeper is set.
	FString Context;
	UWorld* World = GetWorld();
	const UHWDuelSubsystem* Duel = World->GetSubsystem<UHWDuelSubsystem>();
	const AHWOpenWorldGameMode* GM = World->GetAuthGameMode<AHWOpenWorldGameMode>();
	const UHWSettingsSubsystem* Settings = PC->GetSettings();
	const bool bFight = Duel != nullptr && Duel->GetEncounter() != nullptr && Duel->GetState() != EHWEncounterState::WaitingToStart
		&& (GM == nullptr || GM->GetPhase() == EHWWorldPhase::Duel || GM->GetPhase() == EHWWorldPhase::DuelOver);
	if (bFight)
	{
		Context = Duel->BossTitle + (Duel->GetTier() == EHWTier::Hellwalker ? TEXT("  -  it reads you") : TEXT("  -  its pattern"));
	}
	else if (GM != nullptr && GM->GetPhase() == EHWWorldPhase::Exploring)
	{
		Context = GM->ObjectiveText();
	}
	else if (GM == nullptr)
	{
		Context = TEXT("The arena");
	}
	if (Settings != nullptr) { Context += FString::Printf(TEXT("%sdifficulty: %s"), Context.IsEmpty() ? TEXT("") : TEXT("   -   "), *UHWSettingsSubsystem::DifficultyName(Settings->GetDifficulty())); }
	Text(Context, CX, PY + 94.f * S, Dim, Medium, 1.f * S, true);
	DrawRect(WithAlpha(Crimson, 0.9f), CX - 110.f * S, PY + 130.f * S, 220.f * S, 2.f * S);

	const bool bBoxes = !M.IsConfirming() && !M.IsCapturing();
	const float TS = 1.35f * S;
	float TW = 0.f;
	float TH = 0.f;
	GetTextSize(TEXT("Ag"), TW, TH, Medium, TS);
	float Y = PY + 152.f * S;
	for (int32 I = 0; I < N; ++I)
	{
		const FHWMenuItem& It = Items[I];
		const bool bSel = I == M.GetSelected();
		const float BX = PX + 28.f * S;
		const float BW = PW - 56.f * S;
		const float BH = RowH - 6.f * S;
		if (bSel)
		{
			DrawRect(Select, BX, Y, BW, BH);
			DrawRect(Ember, BX, Y, 4.f * S, BH);
		}
		const FLinearColor C = !It.bEnabled ? Disabled : (bSel ? Bright : ToneColor(It.Tone));
		Text(It.Label, CX, Y + (BH - TH) * 0.5f, C, Medium, TS, true);
		if (bBoxes) { AddHitBox(FVector2D(BX, Y), FVector2D(BW, BH), Box(TEXT("I"), I), true); }
		Y += RowH;
	}
	if (const FHWMenuItem* Sel = M.GetSelectedItem())
	{
		TArray<FString> Lines;
		Wrap(Sel->Hint, Medium, 1.0f * S, PW - 70.f * S, Lines);
		for (int32 L = 0; L < Lines.Num() && L < 2; ++L) { Text(Lines[L], CX, Y + (12.f + 26.f * L) * S, Dim, Medium, 1.0f * S, true); }
	}
	DrawMenuFooter(PC, M, PX + 30.f * S, PY + PH - 40.f * S, PW - 60.f * S, true);
}

void AHWHUD::DrawSettingRow(const FHWMenu& M, int32 Index, float X, float Y, float W, float H, bool bHitBoxes)
{
	const float S = UI;
	const FHWMenuItem& It = M.GetItems()[Index];
	UFont* Medium = GEngine->GetMediumFont();
	UFont* Small = GEngine->GetSmallFont();
	const float TS = 1.15f * S;
	float TW = 0.f;
	float TH = 0.f;
	GetTextSize(TEXT("Ag"), TW, TH, Medium, TS);
	const float TextY = Y + (H - TH) * 0.5f;
	if (It.Kind == EHWMenuItemKind::Header)
	{
		const float HY = Y + H - 24.f * S;
		Text(It.Label, X - 16.f * S, HY, Gold, Small, 1.15f * S);
		const float LW = TextWidth(It.Label, Small, 1.15f * S);
		DrawRect(WithAlpha(Gold, 0.3f), X - 16.f * S + LW + 14.f * S, HY + 9.f * S, W + 16.f * S - LW - 14.f * S, 1.f * S);
		return;
	}
	const bool bSel = Index == M.GetSelected();
	const bool bCapturing = M.IsCapturing() && bSel;
	const float RX = X - 16.f * S;
	const float RW = W + 32.f * S;
	if (bSel)
	{
		DrawRect(Select, RX, Y + 3.f * S, RW, H - 6.f * S);
		DrawRect(Ember, RX, Y + 3.f * S, 4.f * S, H - 6.f * S);
	}
	const FLinearColor LabelC = !It.bEnabled ? Disabled : (bSel ? Bright : ToneColor(It.Tone));
	Text(It.Label, X, TextY, LabelC, Medium, TS);
	if (bHitBoxes) { AddHitBox(FVector2D(RX, Y + 3.f * S), FVector2D(RW, H - 6.f * S), Box(TEXT("I"), Index), true, 0); }

	const float VX = X + W * 0.5f; // the value column
	const float VW = W * 0.5f;
	const FLinearColor ValC = bSel ? Bright : FLinearColor(0.82f, 0.8f, 0.76f);
	switch (It.Kind)
	{
	case EHWMenuItemKind::Choice:
	case EHWMenuItemKind::Slider:
	{
		const float AW = 36.f * S;
		const bool bSlider = It.Kind == EHWMenuItemKind::Slider;
		const bool bCanL = bSlider ? It.Value > It.Min + 1e-4f : (It.bWrap || It.Index > 0);
		const bool bCanR = bSlider ? It.Value < It.Max - 1e-4f : (It.bWrap || It.Index < It.Options.Num() - 1);
		const FLinearColor ArrowOn = bSel ? Ember : Dim;
		const FLinearColor ArrowOff = WithAlpha(Disabled, 0.5f);
		Text(TEXT("<"), VX + AW * 0.5f, TextY, bCanL ? ArrowOn : ArrowOff, Medium, TS, true);
		Text(TEXT(">"), VX + VW - AW * 0.5f, TextY, bCanR ? ArrowOn : ArrowOff, Medium, TS, true);
		if (bHitBoxes)
		{
			AddHitBox(FVector2D(VX, Y + 3.f * S), FVector2D(AW, H - 6.f * S), Box(TEXT("L"), Index), true, 1);
			AddHitBox(FVector2D(VX + VW - AW, Y + 3.f * S), FVector2D(AW, H - 6.f * S), Box(TEXT("R"), Index), true, 1);
		}
		if (!bSlider)
		{
			Text(It.ValueText(), VX + VW * 0.5f, TextY, ValC, Medium, TS, true);
		}
		else
		{
			const float ValueW = 96.f * S;
			const float BX = VX + AW + 8.f * S;
			const float BW = VW - 2.f * AW - 24.f * S - ValueW;
			Bar(BX, Y + H * 0.5f - 3.f * S, BW, 6.f * S, It.SliderAlpha(), bSel ? Ember : Crimson, BackBar);
			const FString V = It.ValueText();
			Text(V, VX + VW - AW - 8.f * S - TextWidth(V, Medium, TS), TextY, ValC, Medium, TS);
		}
		break;
	}
	case EHWMenuItemKind::Toggle:
	{
		const float PWd = 92.f * S;
		const float PH = H - 16.f * S;
		const float PY = Y + 8.f * S;
		const float PX0 = VX + VW * 0.5f - PWd - 4.f * S;
		for (int32 K = 0; K < 2; ++K)
		{
			const bool bOnCell = K == 1;
			const bool bActive = It.bOn == bOnCell;
			const float CX0 = PX0 + K * (PWd + 8.f * S);
			if (bActive) { DrawRect(bOnCell ? WithAlpha(Crimson, bSel ? 0.85f : 0.6f) : FLinearColor(0.25f, 0.24f, 0.23f, 0.9f), CX0, PY, PWd, PH); }
			Outline(CX0, PY, PWd, PH, bActive ? (bSel ? Ember : WithAlpha(Dim, 0.8f)) : WithAlpha(Dim, 0.35f), FMath::Max(1.f, 1.5f * S));
			Text(bOnCell ? TEXT("On") : TEXT("Off"), CX0 + PWd * 0.5f, PY + (PH - TH) * 0.5f, bActive ? Bright : Dim, Medium, TS, true);
		}
		break;
	}
	case EHWMenuItemKind::Binding:
	{
		const float KX = VX + 36.f * S;
		const float KW = 230.f * S;
		const float KH = H - 14.f * S;
		const float KY = Y + 7.f * S;
		if (It.bReadOnly)
		{
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.3f), KX, KY, KW, KH);
			Text(It.KeyText, KX + KW * 0.5f, KY + (KH - TH) * 0.5f, Dim, Medium, TS, true);
		}
		else if (bCapturing)
		{
			const float Pulse = 0.55f + 0.45f * FMath::Sin(Clock * 8.f);
			DrawRect(WithAlpha(Gold, 0.18f * Pulse), KX, KY, KW, KH);
			Outline(KX, KY, KW, KH, WithAlpha(Gold, Pulse), FMath::Max(1.f, 2.f * S));
			Text(TEXT("press a key"), KX + KW * 0.5f, KY + (KH - TH) * 0.5f, Gold, Medium, TS, true);
		}
		else
		{
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), KX, KY, KW, KH);
			Outline(KX, KY, KW, KH, bSel ? Ember : WithAlpha(Dim, 0.6f), FMath::Max(1.f, 1.5f * S));
			Text(It.KeyText, KX + KW * 0.5f, KY + (KH - TH) * 0.5f, ValC, Medium, TS, true);
		}
		float SW = 0.f;
		float SH = 0.f;
		GetTextSize(TEXT("Ag"), SW, SH, Small, 1.1f * S);
		Text(It.PadText, KX + KW + 22.f * S, Y + (H - SH) * 0.5f, Dim, Small, 1.1f * S);
		break;
	}
	default:
		break;
	}
}

void AHWHUD::DrawSettingsMenu(AHWPlayerController* PC, const FHWMenu& M)
{
	const float S = UI;
	UFont* Medium = GEngine->GetMediumFont();
	const float PW = FMath::Min(1180.f * S, Canvas->ClipX - 40.f * S);
	const float PH = FMath::Min(900.f * S, Canvas->ClipY - 30.f * S);
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = (Canvas->ClipY - PH) * 0.5f;
	const float Margin = 44.f * S;
	const bool bBoxes = !M.IsConfirming() && !M.IsCapturing();
	Panel(PX, PY, PW, PH);
	Text(M.GetInfo().Title, PX + Margin, PY + 24.f * S, Ink, GEngine->GetLargeFont(), 1.5f * S);
	if (!PC->GetSettings() || !PC->GetSettings()->IsPersistent())
	{
		const FString Note = TEXT("not saved this run (-HWNoSave)");
		Text(Note, PX + PW - Margin - TextWidth(Note, GEngine->GetSmallFont(), 1.0f * S), PY + 40.f * S, Dim, GEngine->GetSmallFont(), 1.0f * S);
	}

	// Tabs.
	const int32 NumTabs = static_cast<int32>(EHWSettingsTab::Count);
	const float TabY = PY + 92.f * S;
	const float TabH = 46.f * S;
	const float TabW = (PW - 2.f * Margin) / NumTabs;
	const float TabTS = 1.2f * S;
	float TW = 0.f;
	float TH = 0.f;
	GetTextSize(TEXT("Ag"), TW, TH, Medium, TabTS);
	for (int32 T = 0; T < NumTabs; ++T)
	{
		const float TX = PX + Margin + T * TabW;
		const bool bSel = static_cast<int32>(M.GetTab()) == T;
		if (bSel)
		{
			DrawRect(WithAlpha(Crimson, 0.2f), TX + 4.f * S, TabY, TabW - 8.f * S, TabH);
			DrawRect(Ember, TX + 4.f * S, TabY + TabH - 3.f * S, TabW - 8.f * S, 3.f * S);
		}
		Text(TabName(static_cast<EHWSettingsTab>(T)), TX + TabW * 0.5f, TabY + (TabH - TH) * 0.5f, bSel ? Bright : Dim, Medium, TabTS, true);
		if (bBoxes) { AddHitBox(FVector2D(TX + 4.f * S, TabY), FVector2D(TabW - 8.f * S, TabH), Box(TEXT("T"), T), true, 1); }
	}
	DrawRect(WithAlpha(Dim, 0.35f), PX + Margin, TabY + TabH + 6.f * S, PW - 2.f * Margin, FMath::Max(1.f, S));

	// The list, scrolled to keep the selection in view.
	const float FooterH = 150.f * S;
	const float ListY = TabY + TabH + 22.f * S;
	const float RowH = 46.f * S;
	const int32 Rows = FMath::Max(1, FMath::FloorToInt32((PY + PH - FooterH - ListY) / RowH));
	PC->SetMenuVisibleRows(Rows);
	const TArray<FHWMenuItem>& Items = M.GetItems();
	const int32 First = M.GetFirstVisible();
	const int32 Last = FMath::Min(Items.Num(), First + Rows);
	for (int32 I = First; I < Last; ++I)
	{
		DrawSettingRow(M, I, PX + Margin + 16.f * S, ListY + (I - First) * RowH, PW - 2.f * Margin - 32.f * S, RowH, bBoxes);
	}
	auto Chevron = [this, S](float CX, float CY, bool bUp)
	{
		const float R = 8.f * S;
		const float D = bUp ? -1.f : 1.f;
		DrawLine(CX - R, CY - D * R * 0.5f, CX, CY + D * R * 0.5f, Dim, 2.f * S);
		DrawLine(CX, CY + D * R * 0.5f, CX + R, CY - D * R * 0.5f, Dim, 2.f * S);
	};
	if (First > 0) { Chevron(PX + PW - Margin * 0.5f, ListY + 14.f * S, true); }
	if (Last < Items.Num()) { Chevron(PX + PW - Margin * 0.5f, ListY + Rows * RowH - 14.f * S, false); }

	// The selection's explanation (the difficulty's line, a key's gamepad button, "takes effect on Apply").
	const float HY = PY + PH - FooterH + 16.f * S;
	DrawRect(WithAlpha(Dim, 0.35f), PX + Margin, HY - 8.f * S, PW - 2.f * Margin, FMath::Max(1.f, S));
	if (const FHWMenuItem* Sel = M.GetSelectedItem())
	{
		TArray<FString> Lines;
		Wrap(Sel->Hint, Medium, 1.05f * S, PW - 2.f * Margin, Lines);
		for (int32 L = 0; L < Lines.Num() && L < 2; ++L) { Text(Lines[L], PX + Margin, HY + 4.f * S + L * 27.f * S, Ink, Medium, 1.05f * S); }
	}
	DrawMenuFooter(PC, M, PX + Margin, PY + PH - 38.f * S, PW - 2.f * Margin, false);
}

void AHWHUD::DrawButtonRow(const FHWMenu& M, float CX, float Y, float ButtonW, float ButtonH, bool bHitBoxes)
{
	const float S = UI;
	UFont* Medium = GEngine->GetMediumFont();
	const TArray<FHWMenuItem>& Items = M.GetItems();
	const int32 N = Items.Num();
	const float Gap = 24.f * S;
	const float Total = N * ButtonW + (N - 1) * Gap;
	const float TS = 1.2f * S;
	float TW = 0.f;
	float TH = 0.f;
	GetTextSize(TEXT("Ag"), TW, TH, Medium, TS);
	for (int32 I = 0; I < N; ++I)
	{
		const FHWMenuItem& It = Items[I];
		const float X = CX - Total * 0.5f + I * (ButtonW + Gap);
		const bool bSel = I == M.GetSelected();
		DrawRect(bSel ? WithAlpha(Crimson, 0.6f) : FLinearColor(0.f, 0.f, 0.f, 0.4f), X, Y, ButtonW, ButtonH);
		Outline(X, Y, ButtonW, ButtonH, bSel ? Ember : WithAlpha(Dim, It.bEnabled ? 0.6f : 0.25f), FMath::Max(1.f, 1.5f * S));
		const FLinearColor C = !It.bEnabled ? Disabled : (bSel ? Bright : ToneColor(It.Tone));
		Text(It.Label, X + ButtonW * 0.5f, Y + (ButtonH - TH) * 0.5f, C, Medium, TS, true);
		if (bHitBoxes && It.bEnabled) { AddHitBox(FVector2D(X, Y), FVector2D(ButtonW, ButtonH), Box(TEXT("I"), I), true); }
	}
}

void AHWHUD::DrawTutorial(AHWPlayerController* PC, const FHWMenu& M)
{
	const float S = UI;
	UFont* Large = GEngine->GetLargeFont();
	UFont* Medium = GEngine->GetMediumFont();
	UFont* Small = GEngine->GetSmallFont();
	const float PW = FMath::Min(1240.f * S, Canvas->ClipX - 40.f * S);
	const float PH = FMath::Min(800.f * S, Canvas->ClipY - 30.f * S);
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = (Canvas->ClipY - PH) * 0.5f;
	const float CX = PX + PW * 0.5f;
	const float Margin = 56.f * S;
	const bool bBoxes = !M.IsConfirming();
	Panel(PX, PY, PW, PH);
	const int32 Slide = M.GetSlide();
	const int32 NS = FMath::Max(1, M.GetInfo().NumSlides);
	const HWTutorial::FSlide& Sl = HWTutorial::Slide(Slide);

	const FString Num = Roman(Slide);
	Text(Num, PX + PW - Margin - TextWidth(Num, Large, 6.f * S), PY + 46.f * S, WithAlpha(Crimson, 0.16f), Large, 6.f * S);
	Text(M.GetInfo().Title, PX + Margin, PY + 30.f * S, Dim, Small, 1.2f * S);
	const FString Page = FString::Printf(TEXT("%d / %d"), Slide + 1, NS);
	Text(Page, PX + PW - Margin - TextWidth(Page, Small, 1.2f * S), PY + 30.f * S, Dim, Small, 1.2f * S);
	Text(Sl.Title, PX + Margin, PY + 80.f * S, Ink, Large, 1.5f * S);
	DrawRect(Ember, PX + Margin, PY + 138.f * S, 140.f * S, 3.f * S);
	TArray<FString> Lines;
	Wrap(Sl.Body, Medium, 1.3f * S, PW - 2.f * Margin, Lines);
	float Y = PY + 170.f * S;
	for (const FString& L : Lines)
	{
		Text(L, PX + Margin, Y, Ink, Medium, 1.3f * S);
		Y += 38.f * S;
	}

	// Page dots (click to jump), the buttons, the keys.
	const float Dot = 14.f * S;
	const float DotGap = 18.f * S;
	const float DotsW = NS * Dot + (NS - 1) * DotGap;
	const float DotY = PY + PH - 136.f * S;
	for (int32 D = 0; D < NS; ++D)
	{
		const float DX = CX - DotsW * 0.5f + D * (Dot + DotGap);
		DrawRect(D == Slide ? Ember : WithAlpha(Dim, D < Slide ? 0.75f : 0.35f), DX, DotY, Dot, Dot);
		if (bBoxes) { AddHitBox(FVector2D(DX - 6.f * S, DotY - 6.f * S), FVector2D(Dot + 12.f * S, Dot + 12.f * S), Box(TEXT("S"), D), true, 1); }
	}
	DrawButtonRow(M, CX, PY + PH - 104.f * S, 210.f * S, 50.f * S, bBoxes);
	DrawMenuFooter(PC, M, PX + Margin, PY + PH - 38.f * S, PW - 2.f * Margin, true);
}

void AHWHUD::DrawNotebookMenu(AHWPlayerController* PC, const FHWMenu& M)
{
	const float S = UI;
	const float PW = FMath::Min(1100.f * S, Canvas->ClipX - 40.f * S);
	const float PH = FMath::Min(740.f * S, Canvas->ClipY - 30.f * S);
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = (Canvas->ClipY - PH) * 0.5f;
	const float Margin = 48.f * S;
	Panel(PX, PY, PW, PH);
	Text(M.GetInfo().Title, PX + Margin, PY + 26.f * S, Ink, GEngine->GetLargeFont(), 1.5f * S);
	DrawRect(Ember, PX + Margin, PY + 84.f * S, 140.f * S, 3.f * S);
	// How well it knows you (Adaptive AI's insight): what sets how hard the next keeper fights.
	const UHWSessionSubsystem* Session = GetGameInstance() != nullptr ? GetGameInstance()->GetSubsystem<UHWSessionSubsystem>() : nullptr;
	if (Session != nullptr && Session->GetPolicy() != nullptr && !Session->bBlind)
	{
		UFont* Small = GEngine->GetSmallFont();
		const float Insight = Session->GetInsight();
		const float Change = Session->GetLastInsightChange();
		const float Right = PX + PW - Margin;
		const FString Head = FString::Printf(TEXT("HOW WELL IT KNOWS YOU   %.0f%%"), Insight * 100.f);
		Text(Head, Right - TextWidth(Head, Small, 1.15f * S), PY + 30.f * S, Gold, Small, 1.15f * S);
		const float BW = 260.f * S;
		Bar(Right - BW, PY + 56.f * S, BW, 6.f * S, Insight, Insight >= 0.5f ? Crimson : WithAlpha(Ember, 0.7f), BackBar);
		const FString Sub = InsightChangeText(Change, TEXT("after the last fight")) + TEXT("  -  the better it knows you, the harder it fights");
		Text(Sub, Right - TextWidth(Sub, Small, 0.95f * S), PY + 68.f * S, Change > 0.005f ? Ember : (Change < -0.005f ? Teal : Dim), Small, 0.95f * S);
	}
	DrawNotebookPage(PX + Margin, PY + 108.f * S, PW - 2.f * Margin, PH - 108.f * S - 150.f * S);
	DrawButtonRow(M, PX + PW * 0.5f, PY + PH - 104.f * S, 210.f * S, 50.f * S, !M.IsConfirming());
	DrawMenuFooter(PC, M, PX + Margin, PY + PH - 38.f * S, PW - 2.f * Margin, true);
}

UTexture2D* AHWHUD::GetMapTexture(const AHWOpenWorldGameMode* GM)
{
	if (MapTexture != nullptr) { return MapTexture; }
	const AHWOpenWorld* World = GM != nullptr ? GM->GetOpenWorld() : nullptr;
	const HWMap::FPicture* Picture = World != nullptr ? World->GetMapPicture() : nullptr;
	if (Picture == nullptr || Picture->Size <= 0 || Picture->Pixels.Num() != Picture->Size * Picture->Size) { return nullptr; }
	// One upload (1 MB): sRGB pixels in PF_B8G8R8A8, which is FColor's own layout.
	const TConstArrayView64<uint8> Bytes(reinterpret_cast<const uint8*>(Picture->Pixels.GetData()), static_cast<int64>(Picture->Pixels.Num()) * sizeof(FColor));
	MapTexture = UTexture2D::CreateTransient(Picture->Size, Picture->Size, PF_B8G8R8A8, NAME_None, Bytes);
	return MapTexture;
}

void AHWHUD::DrawMapMenu(AHWPlayerController* PC, const FHWMenu& M)
{
	UWorld* World = GetWorld();
	AHWOpenWorldGameMode* GM = World != nullptr ? World->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr;
	if (GM == nullptr) { return; }
	const float S = UI;
	UFont* Small = GEngine->GetSmallFont();
	UFont* Medium = GEngine->GetMediumFont();
	DrawRect(FLinearColor(0.012f, 0.01f, 0.011f, 0.8f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);

	// Layout: the map square on the left, the keepers and the legend beside it, the keys underneath.
	const float Gap = 28.f * S;
	const float TopH = 80.f * S;
	const float FootH = 86.f * S;
	const float SideW = FMath::Clamp(Canvas->ClipX * 0.27f, 340.f * S, 470.f * S);
	const float Side = FMath::Max(160.f, FMath::Min(Canvas->ClipY - TopH - FootH, Canvas->ClipX - SideW - 3.f * Gap));
	const float X0 = FMath::Max(Gap, (Canvas->ClipX - Side - Gap - SideW) * 0.5f);
	const float Y0 = TopH;
	const float SX = X0 + Side + Gap;
	PC->SetMapScreen(FVector2D(X0, Y0), Side);
	const HWMap::FView V = PC->GetMapView();
	const bool bBoxes = !M.IsConfirming();
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Clock * 4.f);
	auto ToScreen = [&V](const FVector& W) { return V.UVToScreen(HWMap::WorldToUV(FVector2D(W.X, W.Y), FHWWorldGen::HalfExtent)); };

	Text(M.GetInfo().Title, X0, 20.f * S, Ink, GEngine->GetLargeFont(), 1.3f * S);
	const FString Objective = GM->ObjectiveText();
	Text(Objective, X0 + Side - TextWidth(Objective, Medium, 1.f * S), 36.f * S, Dim, Medium, 1.f * S);

	// ---- the picture --------------------------------------------------------------------------------------------
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.95f), X0 - 3.f * S, Y0 - 3.f * S, Side + 6.f * S, Side + 6.f * S);
	if (UTexture2D* Tex = GetMapTexture(GM))
	{
		const FVector2D Min = V.WindowMin();
		DrawTexture(Tex, X0, Y0, Side, Side, Min.X, Min.Y, V.WindowSize(), V.WindowSize(), FLinearColor::White, BLEND_Opaque);
	}
	else
	{
		DrawRect(FLinearColor(0.05f, 0.045f, 0.04f, 1.f), X0, Y0, Side, Side);
		Text(TEXT("Charting the valley..."), X0 + Side * 0.5f, Y0 + Side * 0.5f - 12.f * S, Dim, Medium, 1.1f * S, true);
	}

	// ---- bells ----------------------------------------------------------------------------------------------------
	const float LabelPad = 8.f * S;
	auto ClampLabelX = [&](float CX, float W) { return FMath::Clamp(CX, X0 + LabelPad + W * 0.5f, X0 + Side - LabelPad - W * 0.5f); };
	TArray<AHWOpenWorldGameMode::FBellView> Bells;
	GM->GetBells(Bells);
	for (const AHWOpenWorldGameMode::FBellView& B : Bells)
	{
		const FVector2D P = ToScreen(B.Location);
		if (!V.Contains(P, 4.0)) { continue; }
		FillCircle(P, 7.5f * S, FLinearColor(0.f, 0.f, 0.f, 0.8f));
		FillCircle(P, 5.f * S, B.bLit ? FLinearColor(1.f, 0.7f, 0.25f) : FLinearColor(0.55f, 0.5f, 0.45f));
		if (B.bCheckpoint) { RingCircle(P, 11.f * S, WithAlpha(Gold, 0.9f), 1.5f * S); }
		const float W = TextWidth(B.Title, Small, 0.95f * S);
		const bool bLeft = P.X + 14.f * S + W > X0 + Side - LabelPad; // no room on the right: write it on the left
		ShadowText(B.Title, bLeft ? P.X - 14.f * S - W : P.X + 14.f * S, P.Y - 8.f * S, B.bLit ? WithAlpha(Gold, 0.95f) : FLinearColor(0.78f, 0.75f, 0.7f), Small, 0.95f * S);
	}

	// ---- keepers (click one to track it) ---------------------------------------------------------------------------
	TArray<AHWOpenWorldGameMode::FKeeperView> Keepers;
	GM->GetKeepers(Keepers);
	const int32 Tracked = GM->GetTrackedKeeper();
	FVector PlayerAt = FVector::ZeroVector;
	float PlayerYaw = 0.f;
	const bool bPlayer = GM->GetPlayerSpot(PlayerAt, PlayerYaw);
	auto StatusLine = [&](const AHWOpenWorldGameMode::FKeeperView& K)
	{
		if (K.bCleared) { return FString(TEXT("cleared")); }
		const FString State = K.bSealed ? TEXT("sealed until the others fall") : TEXT("open");
		return bPlayer ? FString::Printf(TEXT("%s  -  %.0f m"), *State, FVector::Dist2D(PlayerAt, K.Location) / 100.0) : State;
	};
	for (const AHWOpenWorldGameMode::FKeeperView& K : Keepers)
	{
		const FVector2D P = ToScreen(K.Location);
		if (!V.Contains(P, 2.0)) { continue; }
		const int32 Item = M.FindItem(AHWPlayerController::TrackId(K.Index));
		const bool bSel = Item != INDEX_NONE && Item == M.GetSelected();
		const bool bTracked = K.Index == Tracked;
		const float R = (bTracked ? 12.f + 2.f * Pulse : 11.f) * S;
		if (bTracked) { RingCircle(P, R + (8.f + 3.f * Pulse) * S, WithAlpha(Gold, 0.45f + 0.45f * Pulse), 2.5f * S); }
		if (bSel) { Outline(P.X - R - 6.f * S, P.Y - R - 6.f * S, 2.f * R + 12.f * S, 2.f * R + 12.f * S, WithAlpha(Bright, 0.85f), FMath::Max(1.f, 1.5f * S)); }
		KeeperIcon(P, R, K.bCleared, K.bSealed);
		// Name and state under the icon (over it near the bottom edge), kept inside the frame.
		const FString Status = StatusLine(K);
		const float NW = TextWidth(K.Title, Medium, 0.9f * S);
		const float SW = TextWidth(Status, Small, 0.95f * S);
		const float Clear = R + (bTracked ? 16.f : 8.f) * S; // below the gold ring when tracked
		const bool bAbove = P.Y + Clear + 44.f * S > Y0 + Side;
		const float NY = bAbove ? P.Y - Clear - 44.f * S : P.Y + Clear;
		const FLinearColor NameC = K.bCleared ? FLinearColor(0.7f, 0.78f, 0.95f) : (bTracked ? Gold : Bright);
		ShadowText(K.Title, ClampLabelX(P.X, NW), NY, NameC, Medium, 0.9f * S, true);
		ShadowText(Status, ClampLabelX(P.X, SW), NY + 24.f * S, K.bCleared ? Dim : FLinearColor(0.85f, 0.82f, 0.78f), Small, 0.95f * S, true);
		if (bBoxes && Item != INDEX_NONE && M.GetItems()[Item].bEnabled)
		{
			AddHitBox(FVector2D(P.X - R - 8.f * S, P.Y - R - 8.f * S), FVector2D(2.f * R + 16.f * S, 2.f * R + 16.f * S), Box(TEXT("I"), Item), true, 2);
		}
	}

	// ---- you ------------------------------------------------------------------------------------------------------
	if (bPlayer)
	{
		FVector2D P = ToScreen(PlayerAt);
		P.X = FMath::Clamp(P.X, X0 + 6.0, X0 + Side - 6.0); // zoomed away from you: pinned to the frame's edge
		P.Y = FMath::Clamp(P.Y, Y0 + 6.0, Y0 + Side - 6.0);
		// Where the camera looks (as the compass): a faint wedge. Then the arrow: the way you face.
		const float Look = FMath::DegreesToRadians(static_cast<float>(PC->GetControlRotation().Yaw));
		for (int32 I = 0; I < 3; ++I)
		{
			const float A0 = Look + FMath::DegreesToRadians(-30.f + 20.f * I);
			const float A1 = A0 + FMath::DegreesToRadians(20.f);
			FillTriangle(P, P + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * (52.f * S), P + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * (52.f * S),
				FLinearColor(0.62f, 0.88f, 1.f, 0.16f));
		}
		const float Rad = FMath::DegreesToRadians(PlayerYaw);
		const FVector2D F(FMath::Cos(Rad), FMath::Sin(Rad)); // +X east = right, +Y south = down
		const FVector2D Rt(-F.Y, F.X);
		const float L = 14.f * S;
		RingCircle(P, (17.f + 5.f * Pulse) * S, FLinearColor(0.55f, 0.85f, 1.f, 0.6f - 0.4f * Pulse), 2.f * S);
		auto Arrow = [&](float K, const FLinearColor& C)
		{
			const FVector2D Tip = P + F * (L * K);
			const FVector2D BackL = P - F * (L * 0.6f * K) - Rt * (L * 0.6f * K);
			const FVector2D BackR = P - F * (L * 0.6f * K) + Rt * (L * 0.6f * K);
			const FVector2D Notch = P - F * (L * 0.2f * K);
			FillTriangle(Tip, BackL, Notch, C);
			FillTriangle(Tip, Notch, BackR, C);
		};
		Arrow(1.3f, FLinearColor(0.f, 0.f, 0.f, 0.85f));
		Arrow(1.f, FLinearColor(0.62f, 0.88f, 1.f));
	}

	// ---- compass letters and the scale ---------------------------------------------------------------------------
	float TW = 0.f;
	float TH = 0.f;
	GetTextSize(TEXT("N"), TW, TH, Medium, 0.95f * S);
	struct FCard { const TCHAR* Letter; FVector2D At; };
	const FCard Cards[] = {
		{ TEXT("N"), FVector2D(X0 + Side * 0.5f, Y0 + 16.f * S) }, { TEXT("S"), FVector2D(X0 + Side * 0.5f, Y0 + Side - 16.f * S) },
		{ TEXT("W"), FVector2D(X0 + 16.f * S, Y0 + Side * 0.5f) }, { TEXT("E"), FVector2D(X0 + Side - 16.f * S, Y0 + Side * 0.5f) },
	};
	for (const FCard& C : Cards)
	{
		FillCircle(C.At, 12.f * S, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Text(C.Letter, C.At.X, C.At.Y - TH * 0.5f, C.Letter[0] == TEXT('N') ? Ember : Ink, Medium, 0.95f * S, true);
	}
	{
		const double MetresAcross = 2.0 * FHWWorldGen::HalfExtent / 100.0 / V.Zoom;
		const double PxPerMetre = Side / MetresAcross;
		double Metres = 50.0;
		for (const double Nice : { 100.0, 200.0, 250.0, 500.0 }) { if (Nice * PxPerMetre <= 170.0 * S) { Metres = Nice; } }
		const float Len = static_cast<float>(Metres * PxPerMetre);
		const float BX = X0 + 40.f * S;
		const float BY = Y0 + Side - 22.f * S;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), BX - 8.f * S, BY - 26.f * S, Len + 16.f * S, 36.f * S);
		DrawRect(Ink, BX, BY, Len, 3.f * S);
		DrawRect(Ink, BX, BY - 5.f * S, 2.f * S, 11.f * S);
		DrawRect(Ink, BX + Len - 2.f * S, BY - 5.f * S, 2.f * S, 11.f * S);
		Text(FString::Printf(TEXT("%.0f m"), Metres), BX + Len * 0.5f, BY - 24.f * S, Ink, Small, 1.0f * S, true);
	}
	Outline(X0 - 3.f * S, Y0 - 3.f * S, Side + 6.f * S, Side + 6.f * S, FLinearColor(1.f, 1.f, 1.f, 0.12f), FMath::Max(1.f, S));
	DrawRect(WithAlpha(Crimson, 0.95f), X0 - 3.f * S, Y0 - 3.f * S, Side + 6.f * S, 3.f * S);
	if (V.Zoom > 1.01)
	{
		const FString Zoom = FString::Printf(TEXT("x%.1f"), V.Zoom);
		ShadowText(Zoom, X0 + Side - 14.f * S - TextWidth(Zoom, Small, 1.f * S), Y0 + 12.f * S, Dim, Small, 1.f * S);
	}

	// ---- beside it: the keepers, the selection's line, the legend ---------------------------------------------------
	Panel(SX, Y0 - 3.f * S, SideW, Side + 6.f * S);
	const float PX = SX + 24.f * S;
	const float PW = SideW - 48.f * S;
	float Y = Y0 + 18.f * S;
	Text(TEXT("KEEPERS"), PX, Y, Gold, Small, 1.1f * S);
	Y += 30.f * S;
	const TArray<FHWMenuItem>& Items = M.GetItems();
	const float RowH = 70.f * S;
	for (int32 I = 0; I < Items.Num(); ++I)
	{
		const FHWMenuItem& It = Items[I];
		const int32 KI = AHWPlayerController::ParseTrackId(It.Id);
		const AHWOpenWorldGameMode::FKeeperView* K = Keepers.FindByPredicate([KI](const AHWOpenWorldGameMode::FKeeperView& Kv) { return Kv.Index == KI; });
		if (K == nullptr) { continue; }
		const bool bSel = I == M.GetSelected();
		const bool bTracked = K->Index == Tracked;
		const float RH = RowH - 8.f * S;
		if (bSel)
		{
			DrawRect(Select, PX - 12.f * S, Y, PW + 24.f * S, RH);
			DrawRect(Ember, PX - 12.f * S, Y, 4.f * S, RH);
		}
		KeeperIcon(FVector2D(PX + 12.f * S, Y + RH * 0.5f), 10.f * S, K->bCleared, K->bSealed);
		const FLinearColor TitleC = !It.bEnabled ? Disabled : (bSel ? Bright : (bTracked ? Gold : Ink));
		Text(It.Label, PX + 38.f * S, Y + 6.f * S, TitleC, Medium, 0.95f * S);
		Text(StatusLine(*K), PX + 38.f * S, Y + 34.f * S, Dim, Small, 1.0f * S);
		if (bTracked)
		{
			const FString Tag = TEXT("TRACKED");
			const float TagW = TextWidth(Tag, Small, 0.95f * S);
			DrawRect(WithAlpha(Gold, 0.18f + 0.1f * Pulse), PX + PW - TagW - 10.f * S, Y + 34.f * S, TagW + 10.f * S, 20.f * S);
			Text(Tag, PX + PW - TagW - 5.f * S, Y + 35.f * S, Gold, Small, 0.95f * S);
		}
		if (bBoxes && It.bEnabled) { AddHitBox(FVector2D(PX - 12.f * S, Y), FVector2D(PW + 24.f * S, RH), Box(TEXT("I"), I), true, 1); }
		Y += RowH;
	}
	if (const FHWMenuItem* Sel = M.GetSelectedItem())
	{
		TArray<FString> Lines;
		Wrap(Sel->Hint, Small, 1.0f * S, PW, Lines);
		for (int32 L = 0; L < Lines.Num() && L < 3; ++L) { Text(Lines[L], PX, Y + 4.f * S + L * 21.f * S, FLinearColor(0.78f, 0.76f, 0.72f), Small, 1.0f * S); }
		Y += (8.f + 21.f * FMath::Min(Lines.Num(), 3)) * S;
	}

	// The legend, at the panel's foot (as many rows as fit at this HUD scale).
	struct FLegendRow { int32 Kind; const TCHAR* What; };
	const FLegendRow Legend[] = {
		{ 0, TEXT("You: the way you face (the wedge: your view)") }, { 1, TEXT("Keeper - open") }, { 2, TEXT("Keeper - sealed until the others fall") },
		{ 3, TEXT("Keeper - cleared") }, { 4, TEXT("The tracked keeper") }, { 5, TEXT("Bell - rung (ringed: the last you rang)") }, { 6, TEXT("Bell - not yet rung") },
		{ 7, TEXT("Path and plazas") },
	};
	const float LegendRowH = 25.f * S;
	const int32 Fit = FMath::Clamp(FMath::FloorToInt32((Y0 + Side - 12.f * S - (Y + 44.f * S)) / LegendRowH), 0, static_cast<int32>(UE_ARRAY_COUNT(Legend)));
	float LY = Y0 + Side - 12.f * S - Fit * LegendRowH;
	if (Fit > 0)
	{
		DrawRect(WithAlpha(Dim, 0.3f), PX, LY - 34.f * S, PW, FMath::Max(1.f, S));
		Text(TEXT("LEGEND"), PX, LY - 26.f * S, Gold, Small, 1.1f * S);
	}
	for (int32 I = 0; I < Fit; ++I)
	{
		const FVector2D C(PX + 12.f * S, LY + LegendRowH * 0.5f);
		switch (Legend[I].Kind)
		{
		case 0:
			FillTriangle(C, C + FVector2D(16.f, -9.f) * S, C + FVector2D(16.f, 9.f) * S, FLinearColor(0.62f, 0.88f, 1.f, 0.2f));
			FillTriangle(C + FVector2D(10.f, 0.f) * S, C + FVector2D(-6.f, -6.f) * S, C + FVector2D(-2.f, 0.f) * S, FLinearColor(0.62f, 0.88f, 1.f));
			FillTriangle(C + FVector2D(10.f, 0.f) * S, C + FVector2D(-2.f, 0.f) * S, C + FVector2D(-6.f, 6.f) * S, FLinearColor(0.62f, 0.88f, 1.f));
			break;
		case 1: KeeperIcon(C, 8.f * S, false, false); break;
		case 2: KeeperIcon(C, 8.f * S, false, true); break;
		case 3: KeeperIcon(C, 8.f * S, true, false); break;
		case 4:
			KeeperIcon(C, 7.f * S, false, false);
			RingCircle(C, 12.f * S, WithAlpha(Gold, 0.45f + 0.45f * Pulse), 2.f * S);
			break;
		case 5:
			FillCircle(C, 5.f * S, FLinearColor(1.f, 0.7f, 0.25f));
			RingCircle(C, 10.f * S, WithAlpha(Gold, 0.9f), 1.5f * S);
			break;
		case 6: FillCircle(C, 5.f * S, FLinearColor(0.55f, 0.5f, 0.45f)); break;
		default: DrawRect(FLinearColor(0.52f, 0.4f, 0.29f), C.X - 11.f * S, C.Y - 4.f * S, 22.f * S, 8.f * S); break; // the path dirt, as the picture shows it
		}
		Text(Legend[I].What, PX + 34.f * S, LY + (LegendRowH - 18.f * S) * 0.5f, FLinearColor(0.82f, 0.8f, 0.76f), Small, 1.0f * S);
		LY += LegendRowH;
	}

	DrawMenuFooter(PC, M, X0, Canvas->ClipY - 40.f * S, Side + Gap + SideW, true);
}

void AHWHUD::DrawNotebookPage(float X, float Y, float W, float H)
{
	// The keeper's notebook: what the RL keepers would write down about you this session — what they expect you to do
	// (the read head on their memory), what you actually did (ground truth, by the kind of move), how often they called
	// it, and the habit they lean on. Plain words: players do not know what a "read head" is.
	const float S = UI;
	UFont* Small = GEngine->GetSmallFont();
	UFont* Medium = GEngine->GetMediumFont();
	DrawRect(FLinearColor(0.06f, 0.05f, 0.045f, 0.9f), X, Y, W, H);
	for (float LY = Y + 40.f * S; LY < Y + H - 10.f * S; LY += 36.f * S)
	{
		DrawRect(FLinearColor(0.55f, 0.5f, 0.42f, 0.06f), X + 24.f * S, LY, W - 48.f * S, FMath::Max(1.f, S));
	}
	DrawRect(WithAlpha(Crimson, 0.25f), X + 70.f * S, Y, FMath::Max(1.f, S), H);

	const UWorld* World = GetWorld();
	const UHWSessionSubsystem* Session = World != nullptr && World->GetGameInstance() != nullptr
		? World->GetGameInstance()->GetSubsystem<UHWSessionSubsystem>() : nullptr;
	const HW::FRLNotebook* Book = Session != nullptr ? &Session->GetNotebook() : nullptr;
	const float CX = X + W * 0.5f;
	if (Session == nullptr || Session->GetPolicy() == nullptr)
	{
		Text(TEXT("No keeper is reading you"), CX, Y + H * 0.38f, Ink, Medium, 1.35f * S, true);
		Text(TEXT("The Adaptive AI keepers fill this notebook. Without their model the duels are scripted - nothing is written down."),
			CX, Y + H * 0.38f + 46.f * S, Dim, Medium, 1.0f * S, true);
		return;
	}
	if (Book->Predictions == 0 && Session->GetMemory().NumTokens == 0)
	{
		Text(TEXT("The notebook fills as the keepers read you"), CX, Y + H * 0.38f, Ink, Medium, 1.35f * S, true);
		Text(TEXT("Fight in Adaptive AI: every exchange is written down - what it threw, how you answered, whether it saw you coming."),
			CX, Y + H * 0.38f + 46.f * S, Dim, Medium, 1.0f * S, true);
		return;
	}

	const float L = X + 92.f * S;              // the left column (after the margin line)
	const float ColW = (W - 92.f * S - 40.f * S) * 0.5f;
	const float RCol = L + ColW + 30.f * S;
	float Ty = Y + 22.f * S;

	// ---- left: what it expects ----------------------------------------------------------------------------------
	Text(TEXT("WHAT IT EXPECTS YOU TO DO NEXT"), L, Ty, Gold, Small, 1.1f * S);
	struct FRow { HW::EMoveId Move; const TCHAR* Name; };
	static const FRow Rows[] = {
		{ HW::EMoveId::BFastSlash, TEXT("a fast slash") }, { HW::EMoveId::BSweepLeft, TEXT("a sweep to your left") },
		{ HW::EMoveId::BSweepRight, TEXT("a sweep to your right") }, { HW::EMoveId::BHeavyCleave, TEXT("a heavy cleave") },
		{ HW::EMoveId::BDelayedHeavy, TEXT("a delayed heavy") }, { HW::EMoveId::BFeintMid, TEXT("a feint") },
		{ HW::EMoveId::BGrab, TEXT("a grab") }, { HW::EMoveId::BKillerThrust, TEXT("the violet killer thrust") },
	};
	float RY = Ty + 34.f * S;
	for (const FRow& Row : Rows)
	{
		HW::ESym Sym = HW::ESym::Neutral;
		float P = 0.f;
		const bool bKnown = Session->PredictAnswer(Row.Move, Sym, P);
		Text(FString::Printf(TEXT("Against %s"), Row.Name), L, RY, Dim, Small, 1.0f * S);
		Text(bKnown ? HumanSym(Sym) : FString(TEXT("-")), L + ColW * 0.43f, RY, Ink, Small, 1.05f * S);
		const float BW = ColW * 0.17f;
		Bar(L + ColW - BW - 44.f * S, RY + 6.f * S, BW, 6.f * S, bKnown ? P : 0.f, P >= HW::RL::ReadMeterMinP ? Crimson : WithAlpha(Ember, 0.6f), BackBar);
		Text(bKnown ? FString::Printf(TEXT("%.0f%%"), P * 100.f) : FString(), L + ColW - 38.f * S, RY, P >= HW::RL::ReadMeterMinP ? Ember : Dim, Small, 1.0f * S);
		RY += 30.f * S;
	}

	// ---- right: what you actually did ---------------------------------------------------------------------------
	Text(TEXT("WHAT YOU ACTUALLY DID"), RCol, Ty, Gold, Small, 1.1f * S);
	static const TCHAR* ClassNames[4] = { TEXT("fast swings"), TEXT("heavy swings"), TEXT("feints"), TEXT("killer thrusts") };
	float CY = Ty + 34.f * S;
	int32 BestClass = -1, BestAnswer = -1, BestN = 0;
	float BestShare = 0.f;
	for (int32 C = 0; C < 4; ++C)
	{
		int32 N = 0;
		for (int32 K = 0; K < HW::NumPlayerSymbols; ++K) { N += Book->Answers[C][K]; }
		Text(FString::Printf(TEXT("To its %s  (%d)"), ClassNames[C], N), RCol, CY, Dim, Small, 1.0f * S);
		if (N == 0)
		{
			Text(TEXT("not seen yet"), RCol + ColW * 0.55f, CY, Disabled, Small, 1.0f * S);
			CY += 52.f * S;
			continue;
		}
		// The two answers you gave most.
		int32 Top[2] = { -1, -1 };
		for (int32 K = 0; K < HW::NumPlayerSymbols; ++K)
		{
			if (Top[0] < 0 || Book->Answers[C][K] > Book->Answers[C][Top[0]]) { Top[1] = Top[0]; Top[0] = K; }
			else if (Top[1] < 0 || Book->Answers[C][K] > Book->Answers[C][Top[1]]) { Top[1] = K; }
		}
		for (int32 J = 0; J < 2; ++J)
		{
			const int32 K = Top[J];
			if (K < 0 || Book->Answers[C][K] == 0) { continue; }
			const float Share = static_cast<float>(Book->Answers[C][K]) / static_cast<float>(N);
			const float LX = RCol + 14.f * S;
			const float LY2 = CY + (22.f + 20.f * J) * S;
			Text(HumanSym(static_cast<HW::ESym>(K)), LX, LY2, J == 0 ? Ink : Dim, Small, 1.0f * S);
			Bar(RCol + ColW * 0.55f, LY2 + 6.f * S, ColW * 0.3f, 5.f * S, Share, J == 0 ? Gold : WithAlpha(Gold, 0.5f), BackBar);
			Text(FString::Printf(TEXT("%.0f%%"), Share * 100.f), RCol + ColW * 0.88f, LY2, J == 0 ? Ink : Dim, Small, 1.0f * S);
			if (J == 0 && N >= 8 && Share > BestShare) { BestShare = Share; BestClass = C; BestAnswer = K; BestN = N; }
		}
		CY += 70.f * S;
	}

	// ---- bottom: how well it reads you + the habit it leans on ---------------------------------------------------
	const float BY = Y + H - 112.f * S;
	DrawRect(FLinearColor(0.55f, 0.5f, 0.42f, 0.18f), L, BY - 10.f * S, W - 92.f * S - 30.f * S, FMath::Max(1.f, S));
	Text(TEXT("HOW WELL IT READS YOU"), L, BY, Gold, Small, 1.1f * S);
	const float Acc = Book->Accuracy();
	const float Conf = Book->Confident > 0 ? static_cast<float>(Book->ConfidentCorrect) / static_cast<float>(Book->Confident) : 0.f;
	Text(FString::Printf(TEXT("It called your answer right %.0f%% of the time (%d of %d)  -  %.0f%% when it was sure  -  %d READ%s landed"),
		Acc * 100.f, Book->Correct, Book->Predictions, Conf * 100.f, Book->ReadsLanded, Book->ReadsLanded == 1 ? TEXT("") : TEXT("s")),
		L, BY + 24.f * S, Ink, Small, 1.05f * S);
	// One small bar per fight: does it read you better as the session goes on?
	const int32 Fights = FMath::Min(Book->Fights, HW::FRLNotebook::MaxFights);
	float FX = X + W - 40.f * S - static_cast<float>(FMath::Max(Fights, 1)) * 20.f * S;   // right-aligned, clear of the text
	Text(TEXT("per fight"), FX - 74.f * S, BY + 8.f * S, Dim, Small, 0.95f * S);
	for (int32 F = 0; F < Fights; ++F)
	{
		const int32 N = Book->FightPredictions[F];
		const float A = N > 0 ? static_cast<float>(Book->FightCorrect[F]) / static_cast<float>(N) : 0.f;
		const float BH = 34.f * S;
		DrawRect(BackBar, FX, BY - 4.f * S, 14.f * S, BH);
		DrawRect(N > 0 ? WithAlpha(Crimson, 0.85f) : Disabled, FX, BY - 4.f * S + BH * (1.f - A), 14.f * S, BH * A);
		FX += 20.f * S;
	}
	FString Habit = TEXT("No habit stands out yet. Keep it that way.");
	if (BestClass >= 0 && BestShare >= 0.55f)
	{
		Habit = FString::Printf(TEXT("Against its %s you %s %.0f%% of the time (%d times). It has noticed."), ClassNames[BestClass],
			*HumanSym(static_cast<HW::ESym>(BestAnswer)).ToLower(), BestShare * 100.f, BestN);
	}
	Text(Habit, L, BY + 56.f * S, BestShare >= 0.55f ? Ember : Dim, Medium, 1.05f * S);
}

void AHWHUD::DrawNotebookLine(UHWDuelSubsystem* Duel, float CX, float Y)
{
	// The result screens' glimpse of the notebook (the full page: Esc -> The keeper's notebook, or the notebook key).
	const UHWSessionSubsystem* Session = Duel != nullptr ? Duel->GetSession() : nullptr;
	if (Session == nullptr || Duel->GetRLBrain() == nullptr || Session->bBlind) { return; }
	const float S = UI;
	const HW::FRLNotebook& Book = Session->GetNotebook();
	if (Book.Predictions > 0)
	{
		Text(FString::Printf(TEXT("It called your answer right %.0f%% of the time this session  -  the notebook: [%s]"), Book.Accuracy() * 100.f,
			*KeyName(static_cast<uint8>(EHWBind::Notebook), PadActive())), CX, Y, Dim, GEngine->GetMediumFont(), 1.f * S, true);
	}
	// Adaptive AI's learning arc: the better it knows you, the stronger the next keeper (a trick repeated drives it up, a
	// new one makes its calls wrong and it falls back).
	const float Change = Session->GetLastInsightChange();
	Text(FString::Printf(TEXT("How well it knows you: %.0f%%  -  %s"), Session->GetInsight() * 100.f, *InsightChangeText(Change, TEXT("after this fight"))),
		CX, Y + 32.f * S, Change > 0.005f ? Ember : (Change < -0.005f ? Teal : Dim), GEngine->GetMediumFont(), 1.f * S, true);
}

void AHWHUD::DrawConfirm(AHWPlayerController* PC, const FHWMenu& M)
{
	(void)PC;
	const float S = UI;
	UFont* Medium = GEngine->GetMediumFont();
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	const float BW = FMath::Min(720.f * S, Canvas->ClipX - 60.f * S);
	TArray<FString> Lines;
	Wrap(M.GetConfirmText(), Medium, 1.25f * S, BW - 80.f * S, Lines);
	const float BH = (70.f + Lines.Num() * 34.f + 140.f) * S;
	const float BX = (Canvas->ClipX - BW) * 0.5f;
	const float BY = (Canvas->ClipY - BH) * 0.5f;
	const float CX = BX + BW * 0.5f;
	Panel(BX, BY, BW, BH);
	float Y = BY + 48.f * S;
	for (const FString& L : Lines)
	{
		Text(L, CX, Y, Ink, Medium, 1.25f * S, true);
		Y += 34.f * S;
	}
	const TCHAR* Labels[] = { TEXT("Cancel"), TEXT("Confirm") };
	const float ButtonW = 200.f * S;
	const float ButtonH = 50.f * S;
	const float Gap = 28.f * S;
	const float ButtonY = BY + BH - 112.f * S;
	float TW = 0.f;
	float TH = 0.f;
	GetTextSize(TEXT("Ag"), TW, TH, Medium, 1.2f * S);
	for (int32 I = 0; I < 2; ++I)
	{
		const float X = CX - ButtonW - Gap * 0.5f + I * (ButtonW + Gap);
		const bool bSel = M.GetConfirmChoice() == I;
		DrawRect(bSel ? WithAlpha(Crimson, 0.65f) : FLinearColor(0.f, 0.f, 0.f, 0.4f), X, ButtonY, ButtonW, ButtonH);
		Outline(X, ButtonY, ButtonW, ButtonH, bSel ? Ember : WithAlpha(Dim, 0.6f), FMath::Max(1.f, 1.5f * S));
		Text(Labels[I], X + ButtonW * 0.5f, ButtonY + (ButtonH - TH) * 0.5f, bSel ? Bright : Ink, Medium, 1.2f * S, true);
		AddHitBox(FVector2D(X, ButtonY), FVector2D(ButtonW, ButtonH), Box(TEXT("C"), I), true, 10);
	}
	Text(PC->IsGamepadActive() ? TEXT("Left / Right  choose     A  accept     B  cancel") : TEXT("Left / Right  choose     Enter  accept     Esc  cancel"),
		CX, BY + BH - 40.f * S, Dim, GEngine->GetSmallFont(), 1.1f * S, true);
}

void AHWHUD::DrawCapture(AHWPlayerController* PC, const FHWMenu& M)
{
	(void)PC;
	const float S = UI;
	const FHWMenuItem* Sel = M.GetSelectedItem();
	const float BW = FMath::Min(700.f * S, Canvas->ClipX - 60.f * S);
	const float BH = 200.f * S;
	const float BX = (Canvas->ClipX - BW) * 0.5f;
	const float BY = (Canvas->ClipY - BH) * 0.5f;
	const float CX = BX + BW * 0.5f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	Panel(BX, BY, BW, BH);
	const float Pulse = 0.55f + 0.45f * FMath::Sin(Clock * 8.f);
	Outline(BX, BY, BW, BH, WithAlpha(Gold, 0.6f * Pulse), FMath::Max(1.f, 2.f * S));
	Text(TEXT("PRESS A KEY OR MOUSE BUTTON FOR"), CX, BY + 30.f * S, Dim, GEngine->GetSmallFont(), 1.2f * S, true);
	Text(Sel != nullptr ? Sel->Label.ToUpper() : FString(), CX, BY + 66.f * S, Bright, GEngine->GetLargeFont(), 1.35f * S, true);
	Text(TEXT("Esc cancels.   W A S D, Esc, Enter, 1 - 3 and the wheel are fixed."), CX, BY + BH - 48.f * S, Dim, GEngine->GetSmallFont(), 1.1f * S, true);
}
