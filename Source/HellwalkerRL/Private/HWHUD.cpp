#include "HWHUD.h"

#include "HWDuelSubsystem.h"
#include "HWOpenWorldGameMode.h"
#include "HWSaveGame.h"
#include "HWSessionSubsystem.h"
#include "HWCharacterBase.h"
#include "HWTypesUE.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"

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
}

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

void AHWHUD::DrawHUD()
{
	Super::DrawHUD();
	if (GetWorld() == nullptr || Canvas == nullptr) { return; }
	Clock += GetWorld()->GetDeltaSeconds();
	DrawScreens();
	if (bShowHelp) { DrawHelp(); }
}

void AHWHUD::DrawHelp()
{
	// The whole map, in the game (README "Controls" is the same list). Keyboard | gamepad.
	struct FRow { const TCHAR* Key; const TCHAR* Pad; const TCHAR* What; };
	const FRow Explore[] = {
		{ TEXT("W A S D"), TEXT("left stick"), TEXT("move (camera-relative)") },
		{ TEXT("Mouse"), TEXT("right stick"), TEXT("look") },
		{ TEXT("Shift (hold)"), TEXT("LB (hold)"), TEXT("sprint") },
		{ TEXT("Left Ctrl"), TEXT("RB"), TEXT("walk / run toggle") },
		{ TEXT("Space"), TEXT("A"), TEXT("jump - at a wall: hurdle, vault, mantle or climb (up to ~2.2 m)") },
		{ TEXT("C"), TEXT("B"), TEXT("crouch - while running: slide") },
		{ TEXT("R"), TEXT("X"), TEXT("ragdoll - Space to get back up") },
		{ TEXT("Middle mouse"), TEXT("right stick press"), TEXT("strafe (face the camera direction)") },
		{ TEXT("RMB (hold)"), TEXT("LT (hold)"), TEXT("aim") },
		{ TEXT("E"), TEXT("Y"), TEXT("ring a bell (rest, checkpoint) / challenge a keeper at a shrine gate") },
	};
	const FRow Fight[] = {
		{ TEXT("W A S D"), TEXT("left stick"), TEXT("move (locked on: strafe around the keeper)") },
		{ TEXT("LMB"), TEXT("X"), TEXT("light attack (chain)") },
		{ TEXT("E"), TEXT("Y"), TEXT("heavy attack") },
		{ TEXT("RMB (hold)"), TEXT("LB (hold)"), TEXT("block") },
		{ TEXT("Q"), TEXT("RB"), TEXT("parry (just before the hit)") },
		{ TEXT("Space + direction"), TEXT("A + stick"), TEXT("ghoststep (dodge)") },
		{ TEXT("F"), TEXT("B"), TEXT("switch weapon: twin blades / greatblade") },
		{ TEXT("Tab / middle mouse"), TEXT("right stick press"), TEXT("lock-on") },
		{ TEXT("R"), TEXT("menu"), TEXT("fight again (arena)") },
	};
	const FRow Menus[] = {
		{ TEXT("1 / 2 / 3"), TEXT("-"), TEXT("title: new walk as Pathbreaker / Hellwalker / 66 Days") },
		{ TEXT("Enter"), TEXT("menu"), TEXT("title: continue the saved walk") },
		{ TEXT("F1"), TEXT("view"), TEXT("this panel") },
		{ TEXT("F3"), TEXT("-"), TEXT("debug overlay (boss read, hit volumes)") },
		{ TEXT("Esc"), TEXT("-"), TEXT("quit") },
	};
	const float S = Canvas->ClipY / 1080.f;
	const float W = 1180.f * S;
	const float X0 = (Canvas->ClipX - W) * 0.5f;
	float Y = 70.f * S;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.78f), X0 - 30.f * S, Y - 30.f * S, W + 60.f * S, 940.f * S);
	auto Section = [&](const TCHAR* Title, const FRow* Rows, int32 N)
	{
		Text(Title, X0, Y, Gold, GEngine->GetMediumFont(), 1.15f * S);
		Y += 34.f * S;
		for (int32 I = 0; I < N; ++I)
		{
			Text(Rows[I].Key, X0, Y, Ink, GEngine->GetSmallFont(), 1.15f * S);
			Text(Rows[I].Pad, X0 + 230.f * S, Y, Dim, GEngine->GetSmallFont(), 1.15f * S);
			Text(Rows[I].What, X0 + 440.f * S, Y, Ink, GEngine->GetSmallFont(), 1.15f * S);
			Y += 24.f * S;
		}
		Y += 16.f * S;
	};
	Section(TEXT("EXPLORING  (Game Animation Sample character)"), Explore, UE_ARRAY_COUNT(Explore));
	Section(TEXT("FIGHTING  (a keeper's duel, or the arena)"), Fight, UE_ARRAY_COUNT(Fight));
	Section(TEXT("MENUS AND DEBUG"), Menus, UE_ARRAY_COUNT(Menus));
	Text(TEXT("F1 to close"), X0 + W, Y, Dim, GEngine->GetSmallFont(), 1.1f * S);
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
	DrawBars(Duel);
	DrawReadMeter(Duel);
	if (Duel->bDebugDraw) { DrawOverlay(Duel); }
	if (Duel->GetState() != EHWEncounterState::Running) { DrawEndScreen(Duel); }

	const float Seconds = static_cast<float>(Duel->GetDuelFrame()) / static_cast<float>(HW::FramesPerSecond);
	DrawControls(Duel->GetState() == EHWEncounterState::Running ? FMath::Clamp(1.f - (Seconds - 14.f) / 4.f, 0.f, 1.f) : 1.f);
}

void AHWHUD::DrawBars(UHWDuelSubsystem* Duel)
{
	const float S = Canvas->ClipY / 1080.f;
	UFont* Large = GEngine->GetLargeFont();
	UFont* Medium = GEngine->GetMediumFont();
	const HW::FFighter& P = Duel->GetFighterState(HW::ESide::Player);
	const HW::FFighter& B = Duel->GetFighterState(HW::ESide::Boss);

	// Boss — top centre.
	const float BW = Canvas->ClipX * 0.46f;
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
	const float PW = Canvas->ClipX * 0.26f;
	Text(TEXT("SOUL"), PX, PY - 30.f * S, Ink, Medium, 1.1f * S);
	Bar(PX, PY, PW, 12.f * S, P.Health / FMath::Max(1.f, P.HealthMax), Crimson, BackBar);
	Bar(PX, PY + 20.f * S, PW * 0.75f, 6.f * S, P.ShaChi / FMath::Max(1.f, P.ShaChiMax),
		P.State == HW::EFighterState::GuardBroken ? Gold : Teal, BackBar);
	Text(P.Weapon == 1 ? TEXT("Glaive") : TEXT("Twin Blades"), PX, PY + 34.f * S, Dim, Medium, 0.9f * S);

	// Tier — top right.
	if (const UHWSessionSubsystem* Session = Duel->GetSession())
	{
		const bool bHell = Duel->GetTier() == EHWTier::Hellwalker;
		const FLinearColor C = Session->bBlind ? Ink : (bHell ? Crimson : Dim);
		const FString Label = Session->TierLabel(Duel->GetTier());
		const FString Enc = FString::Printf(TEXT("encounter %d"), Session->EncountersStarted);
		float LW = 0.f;
		float LH = 0.f;
		GetTextSize(Label, LW, LH, Large, 0.9f * S);
		Text(Label, Canvas->ClipX - 40.f * S - LW, 30.f * S, C, Large, 0.9f * S);
		GetTextSize(Enc, LW, LH, Medium, 0.8f * S);
		Text(Enc, Canvas->ClipX - 40.f * S - LW, 62.f * S, Dim, Medium, 0.8f * S);
	}
}

void AHWHUD::DrawReadMeter(UHWDuelSubsystem* Duel)
{
	HW::FReadMeterEvent RM;
	float Alpha = 0.f;
	if (!Duel->GetActiveReadMeter(RM, Alpha)) { return; }
	// Testimony, not telegraph: this appears only after the counter has already landed.
	const float S = Canvas->ClipY / 1080.f;
	const float CX = Canvas->ClipX * 0.5f;
	const float Y = Canvas->ClipY * 0.70f;
	const float A = FMath::Clamp(Alpha * 1.6f, 0.f, 1.f);
	FLinearColor Red = Crimson;
	Red.A = A;
	FLinearColor Light = Ink;
	Light.A = A;
	Text(TEXT("READ"), CX, Y, Red, GEngine->GetMediumFont(), 1.0f * S, true);
	Text(HumanSym(RM.Predicted), CX, Y + 24.f * S, Light, GEngine->GetLargeFont(), 1.2f * S, true);
	const float W = 220.f * S;
	FLinearColor Back = BackBar;
	Back.A *= A;
	Bar(CX - W * 0.5f, Y + 64.f * S, W, 6.f * S, RM.Confidence, Red, Back);
	Text(FString::Printf(TEXT("%.0f%%  -  %s"), RM.Confidence * 100.f, *ToFString(HW::Move(RM.Counter).Name)), CX, Y + 76.f * S, Light,
		GEngine->GetSmallFont(), 1.f * S, true);
}

void AHWHUD::DrawWorldMarkers(UHWDuelSubsystem* Duel)
{
	const float S = Canvas->ClipY / 1080.f;
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

	// Killer-move telegraph: a red mark over the Warden while an unblockable wind-up is running.
	const HW::FFighter& B = Duel->GetFighterState(HW::ESide::Boss);
	if (const AHWCharacterBase* Boss = Duel->GetFighterActor(HW::ESide::Boss))
	{
		if (B.State == HW::EFighterState::Acting && B.CurrentMove().bUnblockable && B.T < B.CurrentMove().Startup)
		{
			const FVector P = Project(Boss->GetActorLocation() + FVector(0.f, 0.f, 230.f));
			if (P.Z > 0.f)
			{
				const float R = (22.f + 6.f * FMath::Sin(Clock * 24.f)) * S;
				const FLinearColor Red(1.f, 0.05f, 0.03f);
				DrawLine(P.X, P.Y - R, P.X + R, P.Y, Red, 4.f * S);
				DrawLine(P.X + R, P.Y, P.X, P.Y + R, Red, 4.f * S);
				DrawLine(P.X, P.Y + R, P.X - R, P.Y, Red, 4.f * S);
				DrawLine(P.X - R, P.Y, P.X, P.Y - R, Red, 4.f * S);
				Text(TEXT("!"), P.X, P.Y - 16.f * S, Red, GEngine->GetLargeFont(), 1.1f * S, true);
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

void AHWHUD::DrawOverlay(UHWDuelSubsystem* Duel)
{
	// A2 numeric overlay — the CSV must reconcile against these numbers.
	const float S = Canvas->ClipY / 1080.f;
	UFont* Small = GEngine->GetSmallFont();
	const HW::FEncounter& E = *Duel->GetEncounter();
	const HW::FFighter& P = E.Duel.Get(HW::ESide::Player);
	const HW::FFighter& B = E.Duel.Get(HW::ESide::Boss);
	const HW::FBrainDecision& D = E.Brain.LastDecision();
	const HW::FEncounterStats& St = E.Stats;

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("frame %d   mean frame %.2f ms   %s   distance %.0f"), E.Duel.Frame, Duel->GetMeanFrameMs(),
		*ToFString(HW::BrainModeName(E.Brain.GetMode())), Duel->FighterDistance()));
	Lines.Add(FString::Printf(TEXT("player  %-10s %-14s T%-3d HP %5.0f  SC %5.1f  FA %+d"), *ToFString(HW::FighterStateName(P.State)),
		*ToFString(HW::Move(P.Move).Name), P.T, P.Health, P.ShaChi, E.Duel.FrameAdvantage(HW::ESide::Player)));
	Lines.Add(FString::Printf(TEXT("boss    %-10s %-14s T%-3d HP %5.0f  SC %5.1f"), *ToFString(HW::FighterStateName(B.State)),
		*ToFString(HW::Move(B.Move).Name), B.T, B.Health, B.ShaChi));
	Lines.Add(FString::Printf(TEXT("decision #%d  slot %s%s  %s -> %s  [%s]  prev %s  FA %+d"), E.Brain.Decisions(),
		*ToFString(HW::SlotTypeName(D.Slot)), D.bChain ? TEXT(" chain") : TEXT(""), *ToFString(HW::Move(D.Scripted).Name),
		*ToFString(HW::Move(D.Chosen).Name), *ToFString(HW::DecisionKindName(D.Kind)), *ToFString(HW::OutcomeName(D.PrevOutcome)), D.FrameAdvantage));
	Lines.Add(FString::Printf(TEXT("read %.2f bits  margin %.2f  predicted %s p=%.2f  argmax %s"), D.ReadBits, D.Margin,
		*ToFString(HW::SymName(D.Predicted)), D.PredictedP, D.bArgmaxHolds ? TEXT("ok") : TEXT("VIOLATED")));
	Lines.Add(FString::Printf(TEXT("pressure bias %.2f  floor correction %.3f  shadow swing deficit %d  swings/min %.1f  dmg/min %.1f"),
		E.Brain.GetPressureBias(), D.FloorCorrection, E.Brain.ShadowSwingDeficit(), St.SwingsPerMin(), St.DamagePerMin()));
	Lines.Add(FString::Printf(TEXT("counters landed %d  substitutions %d  decisions %d  symbols %d  player dmg taken %.0f  boss dmg taken %.0f"),
		St.CountersLanded, E.Brain.Substitutions(), St.Decisions, St.Symbols, St.PlayerDamageTaken, St.BossDamageTaken));
	for (int32 K = 0; K < 3 && K < D.NumTop; ++K)
	{
		Lines.Add(FString::Printf(TEXT("  top%d %-14s E %+5.2f  score %+5.2f  decision %+5.2f"), K + 1, *ToFString(HW::Move(D.Top[K].Move).Name),
			D.Top[K].Expected, D.Top[K].Score, D.Top[K].Decision));
	}
	Lines.Add(FString::Printf(TEXT("telemetry %s"), *Duel->GetTelemetryPath()));
	for (const FString& L : Duel->GetDecisionLog()) { Lines.Add(L); }

	const float X = 30.f * S;
	float Y = 130.f * S;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X - 10.f * S, Y - 8.f * S, Canvas->ClipX * 0.62f, (Lines.Num() * 17.f + 14.f) * S);
	for (const FString& L : Lines)
	{
		Text(L, X, Y, FLinearColor(0.75f, 1.f, 0.8f), Small, 1.f * S);
		Y += 17.f * S;
	}
}

void AHWHUD::DrawStartScreen(UHWDuelSubsystem* Duel)
{
	const float S = Canvas->ClipY / 1080.f;
	const float CX = Canvas->ClipX * 0.5f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	Text(TEXT("HELLWALKER"), CX, Canvas->ClipY * 0.2f, Crimson, GEngine->GetLargeFont(), 3.2f * S, true);
	Text(TEXT("The Ninefold Warden reads you.  It adapts.  It counterattacks."), CX, Canvas->ClipY * 0.2f + 110.f * S, Ink, GEngine->GetMediumFont(), 1.3f * S, true);

	const UHWSessionSubsystem* Session = Duel->GetSession();
	const bool bBlind = Session != nullptr && Session->bBlind;
	const float Y = Canvas->ClipY * 0.45f;
	if (bBlind)
	{
		Text(TEXT("[1]  VARIANT A"), CX, Y, Ink, GEngine->GetLargeFont(), 1.4f * S, true);
		Text(TEXT("[2]  VARIANT B"), CX, Y + 60.f * S, Ink, GEngine->GetLargeFont(), 1.4f * S, true);
	}
	else
	{
		Text(TEXT("[1]  PATHBREAKER  -  a fixed pattern. Learn it."), CX, Y, Dim, GEngine->GetLargeFont(), 1.2f * S, true);
		Text(TEXT("[2]  HELLWALKER  -  it learns you. Stay unpredictable."), CX, Y + 60.f * S, Crimson, GEngine->GetLargeFont(), 1.2f * S, true);
	}
	Text(TEXT("The Warden remembers you for the whole session.  It forgets when you quit."), CX, Y + 150.f * S, Dim, GEngine->GetMediumFont(), 1.f * S, true);
	DrawControls(1.f);
}

void AHWHUD::DrawEndScreen(UHWDuelSubsystem* Duel)
{
	const float S = Canvas->ClipY / 1080.f;
	const float CX = Canvas->ClipX * 0.5f;
	const bool bWon = Duel->GetState() == EHWEncounterState::PlayerWon;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), 0.f, Canvas->ClipY * 0.3f, Canvas->ClipX, Canvas->ClipY * 0.32f);
	Text(bWon ? TEXT("THE WARDEN FALLS") : TEXT("YOU DIED"), CX, Canvas->ClipY * 0.33f, bWon ? Gold : Crimson, GEngine->GetLargeFont(), 2.6f * S, true);

	const HW::FEncounter& E = *Duel->GetEncounter();
	const HW::FEncounterStats& St = E.Stats;
	Text(FString::Printf(TEXT("%.1f s   -   you dealt %.0f   -   you took %.0f   -   the Warden landed %d reads"),
		St.Frames / 60.f, St.BossDamageTaken, St.PlayerDamageTaken, St.CountersLanded), CX, Canvas->ClipY * 0.33f + 95.f * S, Ink,
		GEngine->GetMediumFont(), 1.1f * S, true);

	const UHWSessionSubsystem* Session = Duel->GetSession();
	if (Session != nullptr && !Session->bBlind)
	{
		// §1.1's naming test, from the boss's side: the habit it has the strongest read on.
		const HW::FPrediction Pred = Session->GetModel().PredictAfter(HW::ESym::BFast);
		Text(FString::Printf(TEXT("Your most readable habit against a fast swing:  %s  (%.0f%%)"), *HumanSym(Pred.Top), Pred.TopP * 100.f),
			CX, Canvas->ClipY * 0.33f + 130.f * S, Dim, GEngine->GetMediumFont(), 1.f * S, true);
	}
	Text(TEXT("[R] fight again - it remembers        [1] / [2] switch tier        [Esc] quit"), CX, Canvas->ClipY * 0.33f + 170.f * S, Ink,
		GEngine->GetMediumFont(), 1.f * S, true);
}

void AHWHUD::DrawControls(float Alpha)
{
	if (Alpha <= 0.f) { return; }
	const float S = Canvas->ClipY / 1080.f;
	FLinearColor C = Dim;
	C.A = Alpha;
	const TCHAR* Lines[] = {
		TEXT("LMB  light (chain)        E  heavy"),
		TEXT("RMB  block (hold)         Q  parry"),
		TEXT("Space + WASD  ghoststep   F  switch weapon"),
		TEXT("Tab  lock-on   F1  all controls   F3  debug"),
	};
	float Y = Canvas->ClipY - 110.f * S;
	for (const TCHAR* L : Lines)
	{
		Text(L, Canvas->ClipX - 460.f * S, Y, C, GEngine->GetSmallFont(), 1.1f * S);
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
		DrawExplore(GM);
		break;
	case EHWWorldPhase::Duel:
	case EHWWorldPhase::DuelOver:
		if (Duel != nullptr && Duel->GetEncounter() != nullptr)
		{
			DrawWorldMarkers(Duel);
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
	const float S = Canvas->ClipY / 1080.f;
	const float CX = Canvas->ClipX * 0.5f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	Text(TEXT("HELLWALKER"), CX, Canvas->ClipY * 0.16f, Crimson, GEngine->GetLargeFont(), 3.4f * S, true);
	Text(TEXT("An ash valley.  Three seals.  Every keeper reads you  -  and they all read the same you."), CX, Canvas->ClipY * 0.16f + 118.f * S,
		Ink, GEngine->GetMediumFont(), 1.25f * S, true);

	const float Y = Canvas->ClipY * 0.42f;
	Text(TEXT("[1]  PATHBREAKER   -   the keepers follow their patterns.  Learn them."), CX, Y, Dim, GEngine->GetLargeFont(), 1.1f * S, true);
	Text(TEXT("[2]  HELLWALKER   -   they learn you.  Stay unpredictable."), CX, Y + 56.f * S, Crimson, GEngine->GetLargeFont(), 1.1f * S, true);
	Text(TEXT("[3]  66 DAYS   -   Hellwalker, and you have 66 lives.  Then the save is gone."), CX, Y + 112.f * S, Gold, GEngine->GetLargeFont(), 1.1f * S, true);
	if (GM->HasSavedGame())
	{
		if (const UHWSaveGame* Existing = UHWSaveGame::LoadOrNull())
		{
			int32 Cleared = 0;
			for (int32 I = 0; I < 3; ++I) { Cleared += Existing->IsShrineCleared(I) ? 1 : 0; }
			FString Line = FString::Printf(TEXT("[Enter]  CONTINUE   -   %d seal%s broken,  %d death%s"), Cleared, Cleared == 1 ? TEXT("") : TEXT("s"),
				Existing->Deaths, Existing->Deaths == 1 ? TEXT("") : TEXT("s"));
			if (Existing->Mode == EHWPlayMode::SixtySixDays) { Line += FString::Printf(TEXT(",  %d days left"), Existing->DaysLeft); }
			Text(Line, CX, Y + 190.f * S, Ink, GEngine->GetLargeFont(), 1.1f * S, true);
		}
	}
	Text(TEXT("The keepers remember you for the whole session.  They forget when you quit.        [Esc]  quit"), CX, Canvas->ClipY * 0.9f,
		Dim, GEngine->GetMediumFont(), 1.f * S, true);
}

void AHWHUD::DrawCompass(AHWOpenWorldGameMode* GM)
{
	APlayerController* PCtrl = GetOwningPlayerController();
	if (PCtrl == nullptr) { return; }
	const float S = Canvas->ClipY / 1080.f;
	const float CX = Canvas->ClipX * 0.5f;
	const float W = 760.f * S;
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
	for (const AHWOpenWorldGameMode::FMarker& M : Markers)
	{
		const FVector To = M.Location - Eye;
		const float D = FMath::FindDeltaAngleDegrees(Yaw, static_cast<float>(To.Rotation().Yaw));
		const float X = CX + FMath::Clamp(D, -90.f, 90.f) / 90.f * W * 0.5f;
		const float R = (M.bPrimary ? 7.f : 5.f) * S;
		DrawLine(X, Y + 6.f * S - R, X + R, Y + 6.f * S, M.Color, 2.f * S);
		DrawLine(X + R, Y + 6.f * S, X, Y + 6.f * S + R, M.Color, 2.f * S);
		DrawLine(X, Y + 6.f * S + R, X - R, Y + 6.f * S, M.Color, 2.f * S);
		DrawLine(X - R, Y + 6.f * S, X, Y + 6.f * S - R, M.Color, 2.f * S);

		// In-world marker with the distance, for anything ahead within reason.
		const float Dist = static_cast<float>(To.Size2D()) / 100.f;
		const FVector P = Project(M.Location);
		if (P.Z > 0.f && Dist < 900.f && Dist > 25.f && P.X > 0.f && P.X < Canvas->ClipX && P.Y > 0.f && P.Y < Canvas->ClipY)
		{
			FLinearColor C = M.Color;
			C.A = FMath::Clamp(1.2f - Dist / 900.f, 0.35f, 1.f);
			Text(FString::Printf(TEXT("%s  %.0f m"), *M.Label, Dist), P.X, P.Y, C, GEngine->GetSmallFont(), 1.1f * S, true);
		}
	}
}

void AHWHUD::DrawExplore(AHWOpenWorldGameMode* GM)
{
	const float S = Canvas->ClipY / 1080.f;
	DrawCompass(GM);
	Text(GM->ObjectiveText(), 40.f * S, 34.f * S, Ink, GEngine->GetMediumFont(), 1.1f * S);
	if (const UHWSaveGame* Save = GM->GetSave())
	{
		FString Right = Save->Mode == EHWPlayMode::Pathbreaker ? TEXT("PATHBREAKER") : (Save->Mode == EHWPlayMode::SixtySixDays ? TEXT("66 DAYS") : TEXT("HELLWALKER"));
		if (Save->Mode == EHWPlayMode::SixtySixDays) { Right += FString::Printf(TEXT("   day %d  -  %d left"), UHWSaveGame::Days - Save->DaysLeft + 1, Save->DaysLeft); }
		float W = 0.f;
		float H = 0.f;
		GetTextSize(Right, W, H, GEngine->GetMediumFont(), 1.f * S);
		Text(Right, Canvas->ClipX - 40.f * S - W, 34.f * S, Save->Mode == EHWPlayMode::Pathbreaker ? Dim : Crimson, GEngine->GetMediumFont(), 1.f * S);
	}
	if (!GM->GetPrompt().IsEmpty())
	{
		const float Y = Canvas->ClipY * 0.74f;
		float W = 0.f;
		float H = 0.f;
		GetTextSize(GM->GetPrompt(), W, H, GEngine->GetLargeFont(), 1.f * S);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), Canvas->ClipX * 0.5f - W * 0.5f - 18.f * S, Y - 8.f * S, W + 36.f * S, H + 16.f * S);
		Text(GM->GetPrompt(), Canvas->ClipX * 0.5f, Y, Gold, GEngine->GetLargeFont(), 1.f * S, true);
	}
	const float A = FMath::Clamp(1.f - (GM->GetPhaseSeconds() - 20.f) / 4.f, 0.f, 1.f);
	if (A > 0.f)
	{
		FLinearColor C = Dim;
		C.A = A;
		const TCHAR* Lines[] = {
			TEXT("WASD  move    Shift  sprint    Space  jump / vault / mantle / climb"),
			TEXT("C  crouch (running: slide)    R  ragdoll (Space: get up)    Ctrl  walk"),
			TEXT("E  ring a bell / challenge a keeper    F1  all controls    Esc  quit"),
		};
		float Y = Canvas->ClipY - 92.f * S;
		for (const TCHAR* L : Lines)
		{
			Text(L, Canvas->ClipX - 660.f * S, Y, C, GEngine->GetSmallFont(), 1.1f * S);
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
	const float S = Canvas->ClipY / 1080.f;
	const float CX = Canvas->ClipX * 0.5f;
	const float Y = Canvas->ClipY * 0.2f;
	FLinearColor T = Ink;
	T.A = Alpha;
	FLinearColor U = Gold;
	U.A = Alpha;
	FLinearColor Line = Crimson;
	Line.A = Alpha * 0.8f;
	Text(Title, CX, Y, T, GEngine->GetLargeFont(), 2.f * S, true);
	DrawRect(Line, CX - 220.f * S, Y + 74.f * S, 440.f * S, 2.f * S);
	if (!Sub.IsEmpty()) { Text(Sub, CX, Y + 86.f * S, U, GEngine->GetMediumFont(), 1.2f * S, true); }
}

void AHWHUD::DrawDuelResult(AHWOpenWorldGameMode* GM, UHWDuelSubsystem* Duel)
{
	const float S = Canvas->ClipY / 1080.f;
	const float CX = Canvas->ClipX * 0.5f;
	const bool bWon = GM->LastDuelWon();
	const float A = FMath::Clamp(GM->GetPhaseSeconds() / 0.8f, 0.f, 1.f);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f * A), 0.f, Canvas->ClipY * 0.3f, Canvas->ClipX, Canvas->ClipY * 0.3f);
	FLinearColor Head = bWon ? Gold : Crimson;
	Head.A = A;
	Text(bWon ? FString::Printf(TEXT("%s FALLS"), *Duel->BossTitle.ToUpper()) : FString(TEXT("YOU DIED")), CX, Canvas->ClipY * 0.33f, Head,
		GEngine->GetLargeFont(), 2.4f * S, true);
	const HW::FEncounterStats& St = Duel->GetEncounter()->Stats;
	Text(FString::Printf(TEXT("%.1f s   -   you dealt %.0f   -   you took %.0f   -   it landed %d reads"),
		St.Frames / 60.f, St.BossDamageTaken, St.PlayerDamageTaken, St.CountersLanded), CX, Canvas->ClipY * 0.33f + 90.f * S, Ink,
		GEngine->GetMediumFont(), 1.1f * S, true);
	if (const UHWSessionSubsystem* Session = Duel->GetSession())
	{
		const HW::FPrediction Pred = Session->GetModel().PredictAfter(HW::ESym::BFast);
		Text(FString::Printf(TEXT("What they have learned: against a fast swing you %s  (%.0f%%)"), *HumanSym(Pred.Top).ToLower(), Pred.TopP * 100.f),
			CX, Canvas->ClipY * 0.33f + 125.f * S, Dim, GEngine->GetMediumFont(), 1.f * S, true);
	}
	Text(bWon ? TEXT("The seal breaks...") : TEXT("You wake again..."), CX, Canvas->ClipY * 0.33f + 165.f * S, Dim,
		GEngine->GetMediumFont(), 1.f * S, true);
}

void AHWHUD::DrawEnding(AHWOpenWorldGameMode* GM, UHWDuelSubsystem* Duel, bool bRegret)
{
	const float S = Canvas->ClipY / 1080.f;
	const float CX = Canvas->ClipX * 0.5f;
	const float A = FMath::Clamp(GM->GetPhaseSeconds() / 2.f, 0.f, 1.f);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f * A), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	FLinearColor Head = bRegret ? Crimson : Gold;
	Head.A = A;
	FLinearColor Body = Ink;
	Body.A = A;
	FLinearColor Low = Dim;
	Low.A = A;
	float Y = Canvas->ClipY * 0.22f;
	if (bRegret)
	{
		Text(TEXT("THE SIXTY-SIXTH DAY"), CX, Y, Head, GEngine->GetLargeFont(), 2.6f * S, true);
		Text(TEXT("The heart gives out on the road.  This story is over, and its save is gone."), CX, Y + 110.f * S, Body, GEngine->GetMediumFont(), 1.3f * S, true);
	}
	else
	{
		Text(TEXT("THE SEALS ARE BROKEN"), CX, Y, Head, GEngine->GetLargeFont(), 2.6f * S, true);
		Text(TEXT("The Warden read you the whole way to its gate.  You were harder to read than it was."), CX, Y + 110.f * S, Body, GEngine->GetMediumFont(), 1.3f * S, true);
		if (const UHWSaveGame* Save = GM->GetSave())
		{
			FString Line = FString::Printf(TEXT("%d min in the valley   -   %d death%s"), FMath::RoundToInt32(Save->PlaySeconds / 60.f), Save->Deaths, Save->Deaths == 1 ? TEXT("") : TEXT("s"));
			if (Save->Mode == EHWPlayMode::SixtySixDays) { Line += FString::Printf(TEXT("   -   %d of 66 days spent"), UHWSaveGame::Days - Save->DaysLeft); }
			Text(Line, CX, Y + 160.f * S, Low, GEngine->GetMediumFont(), 1.1f * S, true);
		}
	}
	if (Duel != nullptr)
	{
		if (const UHWSessionSubsystem* Session = Duel->GetSession())
		{
			Y = Canvas->ClipY * 0.52f;
			Text(TEXT("WHAT THEY LEARNED ABOUT YOU"), CX, Y, Low, GEngine->GetMediumFont(), 1.1f * S, true);
			const HW::ESym After[] = { HW::ESym::BFast, HW::ESym::BHeavy, HW::ESym::BFeint };
			const TCHAR* Names[] = { TEXT("a fast swing"), TEXT("a heavy swing"), TEXT("a feint") };
			for (int32 I = 0; I < 3; ++I)
			{
				const HW::FPrediction Pred = Session->GetModel().PredictAfter(After[I]);
				Text(FString::Printf(TEXT("against %s  -  %s  (%.0f%%)"), Names[I], *HumanSym(Pred.Top).ToLower(), Pred.TopP * 100.f),
					CX, Y + (40.f + 34.f * I) * S, Body, GEngine->GetMediumFont(), 1.1f * S, true);
			}
		}
	}
	Text(TEXT("[1] Pathbreaker     [2] Hellwalker     [3] 66 Days     -     a new walk        [Esc] quit"), CX, Canvas->ClipY * 0.86f, Low,
		GEngine->GetMediumFont(), 1.1f * S, true);
}
