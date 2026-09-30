#include "HWDuelSubsystem.h"

#include "HellwalkerRL.h"
#include "HWCharacterBase.h"
#include "HWCombatComponent.h"
#include "HWPlayerCharacter.h"
#include "HWSessionSubsystem.h"
#include "HWSlashFX.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// B0's reference: the scripted boss's mean swings/min across the simulated player population. The
	// aggression-floor controller's plan-form term steers toward it (PLAN §1.2); the matched-slot shadow
	// term does the precise work.
	constexpr float ReferenceSwingsPerMin = 62.f;

	int32 BufferFramesFor(EHWPlayerAction A)
	{
		switch (A)
		{
		case EHWPlayerAction::Light:
		case EHWPlayerAction::Heavy:
		case EHWPlayerAction::Switch: return 10; // chain presses land at the cancel frame
		case EHWPlayerAction::Step:   return 6;
		case EHWPlayerAction::Parry:  return 3;  // a buffered parry must not drift far from the press
		default:                      return 0;
		}
	}
}

// =================================================================================================
// Contact oracle — PLAN §5.4
// =================================================================================================

void FHWContactOracle::BuildVolume(const HW::FFighter& F, const FVector& AttackerLocation, FVector& OutCenter, FVector& OutExtent, FQuat& OutRotation)
{
	const HW::FMoveData& M = F.CurrentMove();
	FVector Face(F.CommitFacingX, F.CommitFacingY, 0.f);
	if (!Face.Normalize()) { Face = FVector::ForwardVector; }
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Face); // UE: +Y is right

	// Same shape the simulator uses: along the facing up to the move's reach; laterally its half-width,
	// or, for a sweep, out to its reach on the side it tracks. "SweepLeft" tracks toward the DEFENDER's
	// left, which (facing each other) is the attacker's right.
	const float XMin = 20.f;
	const float XMax = FMath::Max(XMin + 20.f, M.Range - 30.f);
	float YMin = -M.HalfWidth;
	float YMax = M.HalfWidth;
	switch (M.Coverage)
	{
	case HW::ECoverage::SweepLeft:  YMax = M.SweepReach; break;
	case HW::ECoverage::SweepRight: YMin = -M.SweepReach; break;
	case HW::ECoverage::Wide:       YMin = -M.SweepReach; YMax = M.SweepReach; break;
	default: break;
	}
	OutCenter = AttackerLocation + Face * (0.5f * (XMin + XMax)) + Right * (0.5f * (YMin + YMax));
	OutExtent = FVector(0.5f * (XMax - XMin), 0.5f * (YMax - YMin), 100.f);
	OutRotation = FRotationMatrix::MakeFromXY(Face, Right).ToQuat();
}

bool FHWContactOracle::Contacts(const HW::FDuel& Duel, HW::ESide Attacker)
{
	AHWCharacterBase* A = Owner->GetFighterActor(Attacker);
	AHWCharacterBase* D = Owner->GetFighterActor(HW::Opponent(Attacker));
	UWorld* World = Owner->GetWorld();
	if (A == nullptr || D == nullptr || World == nullptr) { return false; }

	const int32 I = HW::SideIndex(Attacker);
	const FVector Loc = A->GetActorLocation();
	FVector Center;
	FVector Extent;
	FQuat Rot;
	BuildVolume(Duel.Get(Attacker), Loc, Center, Extent, Rot);

	// Sweep from where the volume was on the previous frame to where it is now (the attacker may be
	// moving). A defender already inside the volume when the window opens is a connection.
	const bool bContinuous = bHasPrev[I] && Owner->LastVolume[I].Frame == Duel.Frame - 1;
	const FVector Start = bContinuous ? Center + (PrevLocation[I] - Loc) : Center;
	PrevLocation[I] = Loc;
	bHasPrev[I] = true;

	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HWSwing), false, A);
	const FCollisionObjectQueryParams Objects(ECC_Pawn);
	World->SweepMultiByObjectType(Hits, Start, Center, Rot, Objects, FCollisionShape::MakeBox(Extent), Params);

	bool bContact = false;
	for (const FHitResult& H : Hits)
	{
		if (H.GetActor() == D) { bContact = true; break; }
	}

	UHWDuelSubsystem::FHitVolume& V = Owner->LastVolume[I];
	V.Center = Center;
	V.Extent = Extent;
	V.Rotation = Rot;
	V.Frame = Duel.Frame;
	V.bContact = bContact;
	return bContact;
}

// =================================================================================================
// Lifecycle
// =================================================================================================

UHWDuelSubsystem::UHWDuelSubsystem()
{
}

bool UHWDuelSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UHWDuelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Oracle = MakeUnique<FHWContactOracle>(this);
	Encounter = MakeUnique<HW::FEncounter>();
	Encounter->bRecordRows = true;
	Bot = MakeUnique<HW::FPlayerBot>();
	RowScratch.reserve(64);
}

void UHWDuelSubsystem::Deinitialize()
{
	CloseTelemetry();
	Super::Deinitialize();
}

TStatId UHWDuelSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHWDuelSubsystem, STATGROUP_Tickables);
}

void UHWDuelSubsystem::RegisterFighters(AHWCharacterBase* InPlayer, AHWCharacterBase* InBoss)
{
	PlayerFighter = InPlayer;
	BossFighter = InBoss;
	if (InPlayer != nullptr) { InPlayer->GetCombat()->SetSide(HW::ESide::Player); }
	if (InBoss != nullptr) { InBoss->GetCombat()->SetSide(HW::ESide::Boss); }
	PlaceFighters();
}

AHWCharacterBase* UHWDuelSubsystem::GetFighterActor(HW::ESide Side) const
{
	return Side == HW::ESide::Player ? PlayerFighter.Get() : BossFighter.Get();
}

UHWSessionSubsystem* UHWDuelSubsystem::GetSession() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World != nullptr ? World->GetGameInstance() : nullptr;
	return GI != nullptr ? GI->GetSubsystem<UHWSessionSubsystem>() : nullptr;
}

EHWTier UHWDuelSubsystem::GetTier() const
{
	const UHWSessionSubsystem* S = GetSession();
	return S != nullptr ? S->Tier : EHWTier::Hellwalker;
}

const HW::FFighter& UHWDuelSubsystem::GetFighterState(HW::ESide Side) const
{
	return Encounter->Duel.Get(Side);
}

int32 UHWDuelSubsystem::GetDuelFrame() const
{
	return Encounter.IsValid() ? Encounter->Duel.Frame : 0;
}

// =================================================================================================
// Encounter control
// =================================================================================================

void UHWDuelSubsystem::StartEncounter(EHWTier Tier)
{
	if (UHWSessionSubsystem* S = GetSession())
	{
		S->Tier = Tier;
	}
	ResetEncounter(-1);
}

void UHWDuelSubsystem::ResetEncounter(int32 InSeed)
{
	UHWSessionSubsystem* S = GetSession();
	if (S == nullptr || !Encounter.IsValid()) { return; }

	CloseTelemetry();
	Seed = InSeed >= 0 ? InSeed : S->EncountersStarted * 7919 + 17;
	++S->EncountersStarted;

	const HW::EBrainMode Mode = S->Tier == EHWTier::Hellwalker ? HW::EBrainMode::Hellwalker : HW::EBrainMode::Pathbreaker;
	Encounter->Begin(&S->GetModel(), Mode, Seed, false);
	Encounter->Brain.Config().RefSwingsPerMin = ReferenceSwingsPerMin;
	{
		// Which boss (open world shrines): its own script, its own health. The model is shared — it is YOU
		// being read, whichever boss is reading.
		const HW::FScriptSlot* Slots = nullptr;
		const int32 N = HW::BossScript(BossScript, Slots);
		Encounter->Brain.SetScript(Slots, N);
		HW::ApplyScriptTuning(BossScript, Encounter->Brain.Config());
		HW::FFighter& B = Encounter->Duel.Get(HW::ESide::Boss);
		B.HealthMax *= BossHealthScale;
		B.Health = B.HealthMax;
	}

	FrameCursor = 0.0;
	HitstopFrames = 0;
	InputQueue.Reset();
	InjectedParryFrame = -1;
	ReadMeterFramesLeft = 0;
	DecisionLog.Reset();
	Flashes.Reset();
	LastVolume[0] = LastVolume[1] = FHitVolume{};
	Oracle->ResetHistory();
	if (Bot.IsValid()) { Bot->Reset(HW::MakeBotProfile(AutoplayKind, AutoplaySkill), Seed * 7919 + 17); }

	PlaceFighters();
	State = EHWEncounterState::Running;
	OpenTelemetry();
	UE_LOG(LogHellwalkerRL, Log, TEXT("Encounter %d begins: %s, seed %d, model has seen %d symbols."),
		S->EncountersStarted, *S->TierLabel(S->Tier), Seed, S->GetModel().SymbolsObserved());
}

void UHWDuelSubsystem::PlaceFighters()
{
	auto Place = [](AHWCharacterBase* C, const FVector& Where, float Yaw)
	{
		if (C == nullptr) { return; }
		C->GetCombat()->StopDisplacement();
		C->SetActorLocationAndRotation(Where, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		C->GetCharacterMovement()->StopMovementImmediately();
		if (APlayerController* PC = Cast<APlayerController>(C->GetController()))
		{
			PC->SetControlRotation(FRotator(-12.f, Yaw, 0.f));
		}
	};
	Place(PlayerFighter.Get(), PlayerSpawn, ArenaYaw);
	Place(BossFighter.Get(), BossSpawn, ArenaYaw + 180.f);
}

void UHWDuelSubsystem::EndEncounter(bool bPlayerWon)
{
	State = bPlayerWon ? EHWEncounterState::PlayerWon : EHWEncounterState::PlayerLost;
	FlushTelemetryRows();
	CloseTelemetry();
	for (int32 I = 0; I < 2; ++I)
	{
		if (AHWCharacterBase* C = GetFighterActor(static_cast<HW::ESide>(I))) { C->GetCombat()->StopDisplacement(); }
	}
	const HW::FEncounterStats& St = Encounter->Stats;
	UE_LOG(LogHellwalkerRL, Log, TEXT("Encounter over — %s. %.1f s, player took %.0f, boss took %.0f, boss swings/min %.1f, counters landed %d."),
		bPlayerWon ? TEXT("the Warden falls") : TEXT("you died"), St.Frames / 60.f, St.PlayerDamageTaken, St.BossDamageTaken,
		St.SwingsPerMin(), St.CountersLanded);
	OnEncounterEnded.Broadcast(bPlayerWon);
}

void UHWDuelSubsystem::DebugKill(HW::ESide Side)
{
	if (!Encounter.IsValid() || State != EHWEncounterState::Running) { return; }
	HW::FFighter& F = Encounter->Duel.Get(Side);
	F.ApplyDamage(F.Health + 1.f);
	// The duel ends on a Death EVENT (raised when a blow kills); a debug kill raises none, so end it here.
	EndEncounter(Side == HW::ESide::Boss);
}

void UHWDuelSubsystem::ClearFighters()
{
	for (int32 I = 0; I < 2; ++I)
	{
		if (AHWCharacterBase* C = GetFighterActor(static_cast<HW::ESide>(I))) { C->GetCombat()->StopDisplacement(); }
	}
	CloseTelemetry();
	PlayerFighter.Reset();
	BossFighter.Reset();
	State = EHWEncounterState::WaitingToStart;
	InputQueue.Reset();
	Flashes.Reset();
	ReadMeterFramesLeft = 0;
}

// =================================================================================================
// Input
// =================================================================================================

void UHWDuelSubsystem::QueuePlayerInput(EHWPlayerAction Action, HW::EDir StepDir)
{
	if (State != EHWEncounterState::Running || bAutoplay || !Encounter.IsValid()) { return; }
	// The press carries its own timestamp: the frame it will be applied on (PLAN §5.2). Tick ordering
	// between input and the frame cursor is therefore out of the correctness argument.
	FQueuedInput In;
	In.Action = Action;
	In.Dir = StepDir;
	In.Frame = Encounter->Duel.Frame;
	InputQueue.Add(In);
}

void UHWDuelSubsystem::InjectParry(int32 FramesBeforeImpact)
{
	InjectParryOffset = FramesBeforeImpact;
	UE_LOG(LogHellwalkerRL, Log, TEXT("hw.InjectParry: a parry will be pressed exactly %d frames before the next boss swing's impact."), FramesBeforeImpact);
}

void UHWDuelSubsystem::SetAutoplay(bool bEnable, HW::EBotKind Kind, float Skill)
{
	bAutoplay = bEnable;
	AutoplayKind = Kind;
	AutoplaySkill = Skill;
	AutoplayWalk = FVector2D::ZeroVector;
	if (Bot.IsValid()) { Bot->Reset(HW::MakeBotProfile(Kind, Skill), Seed * 7919 + 17); }
	UE_LOG(LogHellwalkerRL, Log, TEXT("Autoplay %s (%s, skill %.2f)."), bEnable ? TEXT("on") : TEXT("off"), *ToFString(HW::BotKindName(Kind)), Skill);
}

void UHWDuelSubsystem::ApplyDueInput()
{
	HW::FDuel& Duel = Encounter->Duel;
	const int32 Frame = Duel.Frame;
	const FVector Face = TargetDirection(HW::ESide::Player);

	if (InjectedParryFrame >= 0 && Frame >= InjectedParryFrame)
	{
		const bool bOk = Duel.Commit(HW::ESide::Player, HW::EMoveId::PParry);
		UE_LOG(LogHellwalkerRL, Log, TEXT("hw.InjectParry: parry pressed on frame %d (%s)."), Frame, bOk ? TEXT("committed") : TEXT("not actionable"));
		InjectedParryFrame = -1;
	}

	for (int32 I = 0; I < InputQueue.Num();)
	{
		const FQueuedInput In = InputQueue[I];
		if (In.Frame > Frame) { ++I; continue; }
		bool bApplied = true;
		switch (In.Action)
		{
		case EHWPlayerAction::Light:     bApplied = Duel.CommitPlayerAttack(false, Face.X, Face.Y); break;
		case EHWPlayerAction::Heavy:     bApplied = Duel.CommitPlayerAttack(true, Face.X, Face.Y); break;
		case EHWPlayerAction::Parry:     bApplied = Duel.Commit(HW::ESide::Player, HW::EMoveId::PParry); break;
		case EHWPlayerAction::Step:      bApplied = Duel.Commit(HW::ESide::Player, HW::StepFor(In.Dir), Face.X, Face.Y); break;
		case EHWPlayerAction::Switch:    bApplied = Duel.CommitSwitch(); break;
		case EHWPlayerAction::GuardDown: Duel.SetGuardHeld(HW::ESide::Player, true); break;
		case EHWPlayerAction::GuardUp:   Duel.SetGuardHeld(HW::ESide::Player, false); break;
		default: break;
		}
		if (bApplied || Frame - In.Frame > BufferFramesFor(In.Action))
		{
			InputQueue.RemoveAt(I);
		}
		else
		{
			++I;
		}
	}
}

void UHWDuelSubsystem::RunAutoplay()
{
	AHWCharacterBase* P = PlayerFighter.Get();
	AHWCharacterBase* B = BossFighter.Get();
	if (P == nullptr || B == nullptr || !Bot.IsValid()) { return; }
	// The bot reads the same 2-D abstraction it is validated in; positions come from the real actors.
	BotArena.Pos[0] = HW::FVec2{ static_cast<float>(P->GetActorLocation().X), static_cast<float>(P->GetActorLocation().Y) };
	BotArena.Pos[1] = HW::FVec2{ static_cast<float>(B->GetActorLocation().X), static_cast<float>(B->GetActorLocation().Y) };
	float Fwd = 0.f;
	float Lat = 0.f;
	Bot->Act(Encounter->Duel, BotArena, Fwd, Lat);
	AutoplayWalk = FVector2D(0.f, Fwd);
}

// =================================================================================================
// Frame loop
// =================================================================================================

void UHWDuelSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!Encounter.IsValid()) { return; }

	// A2: real frame time (the fixed frame rate makes DeltaTime a constant; real time is what matters).
	const double Now = FPlatformTime::Seconds();
	if (LastRealTime > 0.0 && State == EHWEncounterState::Running)
	{
		FrameTimeAccumMs += (Now - LastRealTime) * 1000.0;
		++FrameTimeSamples;
	}
	LastRealTime = Now;

	if (State != EHWEncounterState::Running)
	{
		for (FFlash& Fl : Flashes) { Fl.FramesLeft = FMath::Max(0, Fl.FramesLeft - 1); }
		Flashes.RemoveAll([](const FFlash& Fl) { return Fl.FramesLeft <= 0; });
		return;
	}

	// PLAN §5.2: one long frame crosses several boundaries, in order.
	FrameCursor += static_cast<double>(DeltaTime) * HW::FramesPerSecond;
	int32 Guard = 0;
	while (FrameCursor >= 1.0 && Guard++ < 8)
	{
		FrameCursor -= 1.0;
		if (HitstopFrames > 0)
		{
			--HitstopFrames; // hit-stop: the duel holds still; no frame elapses for either fighter
			continue;
		}
		StepOneFrame();
		if (State != EHWEncounterState::Running) { break; }
	}
	if (FrameCursor >= 1.0) { FrameCursor = 0.99; } // never spiral after a hitch

	if (bDebugDraw)
	{
		for (int32 I = 0; I < 2; ++I)
		{
			const FHitVolume& V = LastVolume[I];
			if (V.Frame >= 0 && GetDuelFrame() - V.Frame < 2)
			{
				DrawDebugBox(GetWorld(), V.Center, V.Extent, V.Rotation, V.bContact ? FColor::Red : FColor::Yellow, false, -1.f, 0, 2.f);
			}
		}
	}
}

void UHWDuelSubsystem::StepOneFrame()
{
	HW::FEncounter& E = *Encounter;

	// 1. The brain decides first: it cannot see anything the player presses on this frame.
	if (E.ThinkBoss(FighterDistance()))
	{
		DecisionLog.Insert(FString::Printf(TEXT("f%d %s"), E.Duel.Frame, *ToFString(E.LastDecision().Reason)), 0);
		if (DecisionLog.Num() > 8) { DecisionLog.SetNum(8); }
	}

	// 2. Player input due on this frame (or the simulated player).
	if (bAutoplay) { RunAutoplay(); } else { ApplyDueInput(); }

	// 3. Latch facing / ghoststep direction for anything committed this frame.
	LatchCommits();

	// 4. Resolve the frame.
	E.StepFrame(*Oracle, PlayerMovementSymbol());
	if (bAutoplay && Bot.IsValid()) { Bot->OnEvents(E.FrameEvents(), E.Duel); }

	// 5. Fan out.
	HandleEvents();
	FlushTelemetryRows();

	for (FFlash& Fl : Flashes) { --Fl.FramesLeft; }
	Flashes.RemoveAll([](const FFlash& Fl) { return Fl.FramesLeft <= 0; });
	if (ReadMeterFramesLeft > 0) { --ReadMeterFramesLeft; }
	for (int32 I = 0; I < 2; ++I)
	{
		if (AHWCharacterBase* C = GetFighterActor(static_cast<HW::ESide>(I)))
		{
			if (C->GetCombat()->FlashFrames > 0) { --C->GetCombat()->FlashFrames; }
		}
	}
}

void UHWDuelSubsystem::LatchCommits()
{
	HW::FDuel& Duel = Encounter->Duel;
	for (int32 I = 0; I < 2; ++I)
	{
		const HW::ESide Side = static_cast<HW::ESide>(I);
		HW::FFighter& F = Duel.Get(Side);
		if (F.State != HW::EFighterState::Acting || F.T != 0 || F.CommitFrame != Duel.Frame) { continue; }

		const FVector Dir = TargetDirection(Side);
		F.CommitFacingX = static_cast<float>(Dir.X);
		F.CommitFacingY = static_cast<float>(Dir.Y);

		AHWCharacterBase* C = GetFighterActor(Side);
		if (C == nullptr) { continue; }
		const HW::FMoveData& M = F.CurrentMove();
		if (M.IsAttack())
		{
			C->SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f)); // committed: faces the target, no tracking after
		}
		else if (M.Kind == HW::EMoveKind::Step)
		{
			// Ghoststep / evade direction is in TARGET space (PLAN §2.1) — the lock-on axis, not the camera.
			const FVector W = StepWorldDirection(Side, M.Dir);
			C->GetCombat()->StartDisplacement(W * M.Distance, static_cast<float>(M.Active) / static_cast<float>(HW::FramesPerSecond));
		}
	}
}

void UHWDuelSubsystem::HandleEvents()
{
	HW::FEncounter& E = *Encounter;
	for (const HW::FDuelEvent& Ev : E.FrameEvents())
	{
		AHWCharacterBase* Actor = GetFighterActor(Ev.Side);
		AHWCharacterBase* Other = GetFighterActor(HW::Opponent(Ev.Side));
		switch (Ev.Type)
		{
		case HW::EDuelEvent::Commit:
			if (Ev.Side == HW::ESide::Boss && HW::Move(Ev.Move).IsAttack() && InjectParryOffset >= 0)
			{
				InjectedParryFrame = Ev.Frame + HW::Move(Ev.Move).Startup - InjectParryOffset;
				InjectParryOffset = -1;
			}
			break;

		case HW::EDuelEvent::Outcome:
		{
			const EHWOutcome O = ToUE(Ev.Outcome);
			OnSwingOutcome.Broadcast(Actor, O, Ev.FrameAdvantage);
			if (Actor != nullptr) { Actor->GetCombat()->OnOutcome.Broadcast(Actor, O, Ev.FrameAdvantage); }
			if (Other == nullptr) { break; }
			const FVector Mid = Actor != nullptr ? (Actor->GetActorLocation() + Other->GetActorLocation()) * 0.5f : Other->GetActorLocation();
			switch (Ev.Outcome)
			{
			case HW::EHitOutcome::Hit:
				AddFlash(Other->GetActorLocation() + FVector(0.f, 0.f, 40.f), FLinearColor(1.f, 0.35f, 0.08f), 0.6f + Ev.Damage / 40.f, 10);
				Other->GetCombat()->FlashFrames = 6;
				Other->GetCombat()->FlashColor = FLinearColor(1.f, 0.9f, 0.85f);
				HitstopFrames = Ev.Damage >= 40.f ? 4 : 2;
				if (AHWPlayerCharacter* PC = Cast<AHWPlayerCharacter>(Other)) { PC->AddCameraKick(0.6f + Ev.Damage / 60.f); }
				break;
			case HW::EHitOutcome::Blocked:
				AddFlash(Mid + FVector(0.f, 0.f, 60.f), FLinearColor(0.3f, 0.6f, 1.f), 0.6f, 8);
				HitstopFrames = 1;
				break;
			case HW::EHitOutcome::Parried:
				// The parry pays a floor, always — and it should FEEL like it.
				AddFlash(Mid + FVector(0.f, 0.f, 70.f), FLinearColor(1.f, 0.95f, 0.6f), 1.6f, 14);
				Other->GetCombat()->FlashFrames = 8;
				Other->GetCombat()->FlashColor = FLinearColor(1.f, 0.9f, 0.4f);
				HitstopFrames = 6;
				if (AHWPlayerCharacter* PC = Cast<AHWPlayerCharacter>(Other)) { PC->AddCameraKick(0.4f); }
				break;
			default:
				break;
			}
			break;
		}
		case HW::EDuelEvent::FeintFake:
			if (Actor != nullptr) { AddFlash(Actor->GetBladeTip(), FLinearColor(0.85f, 0.85f, 1.f), 0.5f, 6); }
			break;
		case HW::EDuelEvent::GuardBreak:
			if (Actor != nullptr)
			{
				AddFlash(Actor->GetActorLocation() + FVector(0.f, 0.f, 90.f), FLinearColor(1.f, 0.85f, 0.1f), 1.3f, 16);
				HitstopFrames = 3;
			}
			break;
		case HW::EDuelEvent::Death:
			EndEncounter(Ev.Side == HW::ESide::Boss);
			break;
		default:
			break;
		}
	}

	HW::FReadMeterEvent RM;
	if (E.Brain.PopReadMeter(RM))
	{
		// Testimony, not telegraph: shown only AFTER a model-driven counter has landed (PLAN §2.4).
		ReadMeter = RM;
		ReadMeterFramesLeft = ReadMeterFrames;
	}
}

// =================================================================================================
// Geometry helpers
// =================================================================================================

FVector UHWDuelSubsystem::TargetDirection(HW::ESide From) const
{
	const AHWCharacterBase* A = GetFighterActor(From);
	const AHWCharacterBase* B = GetFighterActor(HW::Opponent(From));
	if (A == nullptr) { return FVector::ForwardVector; }
	if (B == nullptr) { return A->GetActorForwardVector(); }
	FVector D = B->GetActorLocation() - A->GetActorLocation();
	D.Z = 0.f;
	return D.Normalize() ? D : A->GetActorForwardVector();
}

float UHWDuelSubsystem::FighterDistance() const
{
	const AHWCharacterBase* A = PlayerFighter.Get();
	const AHWCharacterBase* B = BossFighter.Get();
	if (A == nullptr || B == nullptr) { return 1000.f; }
	return static_cast<float>(FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()));
}

FVector UHWDuelSubsystem::StepWorldDirection(HW::ESide Side, HW::EDir Dir) const
{
	const FVector F = TargetDirection(Side);
	const FVector R = FVector::CrossProduct(FVector::UpVector, F);
	switch (Dir)
	{
	case HW::EDir::Forward: return F;
	case HW::EDir::Back:    return -F;
	case HW::EDir::Right:   return R;
	case HW::EDir::Left:    return -R;
	default:                return FVector::ZeroVector;
	}
}

HW::ESym UHWDuelSubsystem::PlayerMovementSymbol() const
{
	const AHWCharacterBase* P = PlayerFighter.Get();
	if (P == nullptr) { return HW::ESym::Neutral; }
	const float Along = static_cast<float>(FVector::DotProduct(P->GetVelocity(), TargetDirection(HW::ESide::Player)));
	if (Along > 150.f) { return HW::ESym::Advance; }
	if (Along < -150.f) { return HW::ESym::Retreat; }
	return HW::ESym::Neutral;
}

bool UHWDuelSubsystem::CanPlayerMove() const
{
	if (State == EHWEncounterState::WaitingToStart) { return true; } // walk the arena on the start screen
	return State == EHWEncounterState::Running && Encounter.IsValid() && Encounter->Duel.Get(HW::ESide::Player).CanMove();
}

float UHWDuelSubsystem::PlayerMoveScale() const
{
	if (!CanPlayerMove()) { return 0.f; }
	return Encounter->Duel.Get(HW::ESide::Player).bGuardHeld ? 0.45f : 1.f;
}

bool UHWDuelSubsystem::GetActiveReadMeter(HW::FReadMeterEvent& Out, float& OutAlpha) const
{
	if (ReadMeterFramesLeft <= 0) { return false; }
	Out = ReadMeter;
	OutAlpha = static_cast<float>(ReadMeterFramesLeft) / static_cast<float>(ReadMeterFrames);
	return true;
}

void UHWDuelSubsystem::AddFlash(const FVector& Where, const FLinearColor& Color, float Size, int32 Frames)
{
	FFlash Fl;
	Fl.Location = Where;
	Fl.Color = Color;
	Fl.Size = Size;
	Fl.FramesLeft = Frames;
	Fl.FramesTotal = Frames;
	Flashes.Add(Fl);
	if (Size >= 0.6f) { HWSpawnImpactFX(GetWorld(), Where, Color, Size); } // C2: the pack's burst, same colour language
}

float UHWDuelSubsystem::GetMeanFrameMs() const
{
	return FrameTimeSamples > 0 ? static_cast<float>(FrameTimeAccumMs / FrameTimeSamples) : 0.f;
}

// =================================================================================================
// Telemetry — PLAN A2: one CSV row per exchange; a run with mean frame time > 20 ms is discarded.
// =================================================================================================

void UHWDuelSubsystem::OpenTelemetry()
{
	const UHWSessionSubsystem* S = GetSession();
	if (S == nullptr) { return; }
	FrameTimeAccumMs = 0.0;
	FrameTimeSamples = 0;
	TelemetryRows = 0;
	TelemetryPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("HellwalkerRL") / TEXT("Telemetry")
		/ FString::Printf(TEXT("%s_e%02d_%s.csv"), *S->SessionId, S->EncountersStarted,
			S->Tier == EHWTier::Hellwalker ? TEXT("hellwalker") : TEXT("pathbreaker"));
	FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(HW::ExchangeCsvHeader())) + LINE_TERMINATOR, *TelemetryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UHWDuelSubsystem::FlushTelemetryRows()
{
	if (!Encounter.IsValid()) { return; }
	Encounter->TakeRows(RowScratch);
	if (RowScratch.empty() || TelemetryPath.IsEmpty()) { return; }
	const UHWSessionSubsystem* S = GetSession();
	const FTCHARToUTF8 RunId(S != nullptr ? *FString::Printf(TEXT("%s-e%02d"), *S->SessionId, S->EncountersStarted) : TEXT("run"));
	FString Out;
	char Line[1024];
	for (const HW::FExchangeRow& R : RowScratch)
	{
		HW::FormatExchangeRow(R, RunId.Get(), Encounter->Brain.GetMode(), GetMeanFrameMs(), Line, sizeof(Line));
		Out += UTF8_TO_TCHAR(Line);
		Out += LINE_TERMINATOR;
		++TelemetryRows;
	}
	FFileHelper::SaveStringToFile(Out, *TelemetryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

void UHWDuelSubsystem::CloseTelemetry()
{
	if (TelemetryPath.IsEmpty()) { return; }
	const float MeanMs = GetMeanFrameMs();
	if (MeanMs > 20.f)
	{
		// A slow session silently produces bad frame data that Phase B would treat as evidence (A2).
		const FString Discarded = TelemetryPath.Replace(TEXT(".csv"), TEXT(".DISCARDED.csv"));
		IFileManager::Get().Move(*Discarded, *TelemetryPath);
		UE_LOG(LogHellwalkerRL, Warning, TEXT("Telemetry DISCARDED: mean frame time %.2f ms > 20 ms (%s)."), MeanMs, *Discarded);
	}
	else if (Encounter.IsValid())
	{
		// B4: per-tier move-selection frequencies alongside the HP / damage numbers.
		const HW::FEncounterStats& St = Encounter->Stats;
		FString Summary = FString::Printf(TEXT("# mode=%s frames=%d player_damage_taken=%.1f boss_damage_taken=%.1f swings_per_min=%.2f damage_per_min=%.2f counters=%d decisions=%d argmax_violations=%d mean_frame_ms=%.2f\n# move_selection"),
			*ToFString(HW::BrainModeName(Encounter->Brain.GetMode())), St.Frames, St.PlayerDamageTaken, St.BossDamageTaken,
			St.SwingsPerMin(), St.DamagePerMin(), St.CountersLanded, St.Decisions, St.ArgmaxViolations, MeanMs);
		for (int32 M = 1; M < HW::NumMoves; ++M)
		{
			if (St.MoveSelect[M] > 0)
			{
				Summary += FString::Printf(TEXT(" %s=%d"), *ToFString(HW::Move(static_cast<HW::EMoveId>(M)).Name), St.MoveSelect[M]);
			}
		}
		Summary += LINE_TERMINATOR;
		FFileHelper::SaveStringToFile(Summary, *TelemetryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
		UE_LOG(LogHellwalkerRL, Log, TEXT("Telemetry: %d exchange rows -> %s"), TelemetryRows, *TelemetryPath);
	}
	TelemetryPath.Reset();
}
