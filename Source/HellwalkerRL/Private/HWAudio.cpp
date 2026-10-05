#include "HWAudio.h"

#include "HellwalkerRL.h"
#include "HWCharacterBase.h"
#include "HWDuelSubsystem.h"
#include "HWSettings.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace HWAudioImpl
{
	constexpr const TCHAR* Slash = TEXT("/Game/SlashTrail_SoftTofu/Resource/Audio/");
	constexpr const TCHAR* Sevarog = TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Sounds/SoundCues/");
	constexpr const TCHAR* Wukong = TEXT("/Game/ParagonSunWukong/Audio/Cues/");

	struct FCueSpec
	{
		const TCHAR* Folder;   // the slash-trail pack, or the keeper's voice folder (filled per identity)
		const TCHAR* Asset;    // "SharpSlash/SC_Basic_Slash_Cue", or the voice line's suffix ("Effort_Attack")
		float Volume;
		float Pitch;
		bool bVoice;           // a keeper voice line: Sevarog_<Asset> or Wukong_<Asset>
	};

	const FCueSpec& Spec(EHWCue Cue)
	{
		static const FCueSpec Table[static_cast<int32>(EHWCue::Count)] = {
			{ Slash, TEXT("SharpSlash/SC_Basic_Slash_Cue"), 0.55f, 0.95f, false },      // SwingFast
			{ Slash, TEXT("Dark/SC_Slash_Dark_Cue"), 0.7f, 0.85f, false },              // SwingHeavy
			{ Slash, TEXT("Wind/SC_Wind_Slash_Cue"), 0.55f, 1.05f, false },             // SwingFeint
			{ Slash, TEXT("Lightning/SC_Lightning_Cue"), 0.75f, 0.8f, false },          // KillerWarn
			{ Slash, TEXT("SharpSlash/SC_Basic_Slash_Cue"), 0.35f, 1.25f, false },      // PlayerSwing
			{ Slash, TEXT("Hit/SC_Hit_Cue"), 0.8f, 0.9f, false },                       // HitPlayer
			{ Slash, TEXT("Hit/SC_Hit_Cue"), 0.75f, 1.05f, false },                     // HitBoss
			{ Slash, TEXT("Hit/SC_Hit_Cue"), 0.85f, 1.9f, false },                      // Parry
			{ Slash, TEXT("LightSaber/SC_LightSaber_Slash_Cue"), 0.45f, 1.6f, false },  // ParryRing
			{ Slash, TEXT("Hit/SC_Hit_Cue"), 0.5f, 0.65f, false },                      // Block
			{ Slash, TEXT("Wind/SC_Wind_Slash_Cue"), 0.3f, 1.55f, false },              // Step
			{ Slash, TEXT("Lightning/SC_Lightning_Cue"), 0.7f, 1.1f, false },           // GuardBreak
			{ nullptr, TEXT("Effort_Attack"), 0.7f, 1.f, true },                         // BossEffort
			{ nullptr, TEXT("Effort_Pain"), 0.7f, 1.f, true },                           // BossPain
			{ nullptr, TEXT("Death"), 0.9f, 1.f, true },                                 // BossDeath
			{ nullptr, TEXT("Effort_Laugh"), 0.85f, 1.f, true },                         // BossLaugh
			{ Slash, TEXT("SharpSlash/SC_Distortion_Slash"), 0.6f, 0.7f, false },       // ReadSting
		};
		return Table[static_cast<int32>(Cue)];
	}

	FString AssetPath(const FString& FolderAndName)
	{
		// "/Game/X/Y/Name" -> "/Game/X/Y/Name.Name"
		const int32 Last = FolderAndName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		return FolderAndName + TEXT(".") + FolderAndName.Mid(Last + 1);
	}

	constexpr const TCHAR* MusicPath = TEXT("/Game/Audio/Ambient/Starter_Music01.Starter_Music01");
	constexpr const TCHAR* DangerPath = TEXT("/Game/Audio/Ambient/Starter_Wind06.Starter_Wind06");
}

bool UHWAudioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UHWAudioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHWAudioSubsystem, STATGROUP_Tickables);
}

void UHWAudioSubsystem::Deinitialize()
{
	if (Music != nullptr) { Music->Stop(); }
	if (Breath != nullptr) { Breath->Stop(); }
	Super::Deinitialize();
}

float UHWAudioSubsystem::EffectsVolume() const
{
	const UGameInstance* GI = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
	const UHWSettingsSubsystem* S = GI != nullptr ? GI->GetSubsystem<UHWSettingsSubsystem>() : nullptr;
	return S != nullptr ? S->GetEffectsVolume() : 1.f;
}

float UHWAudioSubsystem::MusicVolume() const
{
	const UGameInstance* GI = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
	const UHWSettingsSubsystem* S = GI != nullptr ? GI->GetSubsystem<UHWSettingsSubsystem>() : nullptr;
	return S != nullptr ? S->GetMusicVolume() : 1.f;
}

USoundBase* UHWAudioSubsystem::LoadPath(const TCHAR* Path)
{
	const FString Key(Path);
	if (TObjectPtr<USoundBase>* Found = Loaded.Find(Key)) { return Found->Get(); }
	if (Missing.Contains(Key)) { return nullptr; }
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Sound == nullptr)
	{
		Missing.Add(Key);
		UE_LOG(LogHellwalkerRL, Warning, TEXT("audio: %s not found (that cue stays silent)."), Path);
		return nullptr;
	}
	Loaded.Add(Key, Sound);
	return Sound;
}

USoundBase* UHWAudioSubsystem::Load(EHWCue Cue)
{
	const HWAudioImpl::FCueSpec& C = HWAudioImpl::Spec(Cue);
	FString Base;
	if (C.bVoice)
	{
		// The keeper's voice: Sevarog for the Warden and the Returned (lowered), Wukong for the Sage.
		const bool bWukong = Identity == 1;
		Base = FString(bWukong ? HWAudioImpl::Wukong : HWAudioImpl::Sevarog) + (bWukong ? TEXT("Wukong_") : TEXT("Sevarog_")) + C.Asset;
	}
	else
	{
		Base = FString(C.Folder) + C.Asset;
	}
	return LoadPath(*HWAudioImpl::AssetPath(Base));
}

void UHWAudioSubsystem::Play(EHWCue Cue, const FVector& Where, bool b2D, float PitchScale, float VolumeScale)
{
	UWorld* World = GetWorld();
	USoundBase* Sound = World != nullptr ? Load(Cue) : nullptr;
	++Counts[static_cast<int32>(Cue)];
	if (Sound == nullptr) { return; }
	const HWAudioImpl::FCueSpec& C = HWAudioImpl::Spec(Cue);
	const float Volume = C.Volume * VolumeScale * EffectsVolume();
	float Pitch = C.Pitch * PitchScale;
	if (C.bVoice && Identity == 2) { Pitch *= 0.72f; } // the Returned: stone, slower, deeper
	if (Volume <= 0.001f) { return; }
	if (b2D) { UGameplayStatics::PlaySound2D(World, Sound, Volume, Pitch); }
	else { UGameplayStatics::PlaySoundAtLocation(World, Sound, Where, Volume, Pitch); }
}

void UHWAudioSubsystem::OnDuelEvent(const HW::FDuelEvent& Ev, const FVector& Where, int32 InIdentity)
{
	Identity = InIdentity;
	auto Roll = [this](float P)
	{
		Rand = Rand * 1664525u + 1013904223u;
		return static_cast<float>(Rand >> 8) / static_cast<float>(1u << 24) < P;
	};
	switch (Ev.Type)
	{
	case HW::EDuelEvent::Commit:
	{
		if (Ev.Move == HW::EMoveId::None) { break; } // a guard raise
		const HW::FMoveData& M = HW::Move(Ev.Move);
		if (Ev.Side == HW::ESide::Boss)
		{
			if (M.IsAttack())
			{
				if (M.bUnblockable) { Play(EHWCue::KillerWarn, Where); }
				else if (M.FakeImpactFrame >= 0 && M.Symbol == HW::ESym::BFeint) { Play(EHWCue::SwingFeint, Where); }
				else { Play(M.Symbol == HW::ESym::BHeavy ? EHWCue::SwingHeavy : EHWCue::SwingFast, Where); }
				if (Roll(M.Symbol == HW::ESym::BHeavy || M.bUnblockable ? 0.5f : 0.22f)) { Play(EHWCue::BossEffort, Where); }
			}
			else if (M.Kind == HW::EMoveKind::Step) { Play(EHWCue::Step, Where, false, 0.8f); }
		}
		else
		{
			if (M.IsAttack()) { Play(EHWCue::PlayerSwing, Where, false, M.Symbol == HW::ESym::Heavy ? 0.8f : 1.f); }
			else if (M.Kind == HW::EMoveKind::Step) { Play(EHWCue::Step, Where); }
		}
		break;
	}
	case HW::EDuelEvent::Outcome:
		switch (Ev.Outcome)
		{
		case HW::EHitOutcome::Hit:
			if (Ev.Side == HW::ESide::Boss) { Play(EHWCue::HitPlayer, Where, false, 1.f, 0.8f + FMath::Min(Ev.Damage / 60.f, 0.6f)); }
			else
			{
				Play(EHWCue::HitBoss, Where);
				if (Roll(Ev.Damage >= 40.f ? 0.7f : 0.3f)) { Play(EHWCue::BossPain, Where, false, Ev.Damage >= 40.f ? 0.9f : 1.f); }
			}
			break;
		case HW::EHitOutcome::Parried:
			Play(EHWCue::Parry, Where);
			Play(EHWCue::ParryRing, Where);
			break;
		case HW::EHitOutcome::Blocked:
			Play(EHWCue::Block, Where);
			break;
		default:
			break;
		}
		break;
	case HW::EDuelEvent::GuardBreak:
		Play(EHWCue::GuardBreak, Where);
		break;
	case HW::EDuelEvent::Death:
		if (Ev.Side == HW::ESide::Boss) { Play(EHWCue::BossDeath, Where); }
		else { Play(EHWCue::BossLaugh, Where, false, 0.95f); } // the keeper has the last word
		break;
	default:
		break;
	}
}

void UHWAudioSubsystem::OnRead(const FVector& Where, int32 InIdentity)
{
	// The moment the player should feel: "it read me".
	Identity = InIdentity;
	Play(EHWCue::ReadSting, Where, true);
	Play(EHWCue::BossLaugh, Where, false, 1.05f, 0.8f);
}

void UHWAudioSubsystem::OnDuelStart(int32 InIdentity)
{
	Identity = InIdentity;
	bDuel = true;
	for (int32& C : Counts) { C = 0; }
	// Every cue of this keeper now, at the start: loaded on first play, each one froze the game at a first hit / parry /
	// voice line (a synchronous load on the game thread). Cached after the first fight.
	for (int32 C = 0; C < static_cast<int32>(EHWCue::Count); ++C) { Load(static_cast<EHWCue>(C)); }
	EnsureMusic();
}

void UHWAudioSubsystem::OnDuelEnd(bool bPlayerWon)
{
	bDuel = false;
	UE_LOG(LogHellwalkerRL, Log, TEXT("audio (%s): %d keeper swings (%d fast, %d heavy, %d feints, %d killer warnings), %d player swings, %d hits on you, %d on the keeper, %d parries, %d blocks, %d ghoststeps, %d guard breaks, %d keeper grunts, %d pain, %d READ stings; %d cue(s) missing."),
		bPlayerWon ? TEXT("you won") : TEXT("you lost"),
		Counts[(int32)EHWCue::SwingFast] + Counts[(int32)EHWCue::SwingHeavy] + Counts[(int32)EHWCue::SwingFeint] + Counts[(int32)EHWCue::KillerWarn],
		Counts[(int32)EHWCue::SwingFast], Counts[(int32)EHWCue::SwingHeavy], Counts[(int32)EHWCue::SwingFeint], Counts[(int32)EHWCue::KillerWarn],
		Counts[(int32)EHWCue::PlayerSwing], Counts[(int32)EHWCue::HitPlayer], Counts[(int32)EHWCue::HitBoss], Counts[(int32)EHWCue::Parry],
		Counts[(int32)EHWCue::Block], Counts[(int32)EHWCue::Step], Counts[(int32)EHWCue::GuardBreak], Counts[(int32)EHWCue::BossEffort],
		Counts[(int32)EHWCue::BossPain], Counts[(int32)EHWCue::ReadSting], Missing.Num());
}

void UHWAudioSubsystem::EnsureMusic()
{
	UWorld* World = GetWorld();
	if (World == nullptr) { return; }
	if (Music == nullptr)
	{
		if (USoundBase* S = LoadPath(HWAudioImpl::MusicPath))
		{
			Music = UGameplayStatics::CreateSound2D(World, S, 1.f, 1.f, 0.f, nullptr, true, false);
			if (Music != nullptr) { Music->SetVolumeMultiplier(0.001f); Music->Play(); }
		}
	}
	if (Breath == nullptr)
	{
		if (USoundBase* S = LoadPath(HWAudioImpl::DangerPath))
		{
			Breath = UGameplayStatics::CreateSound2D(World, S, 1.f, 1.f, 0.f, nullptr, true, false);
			if (Breath != nullptr) { Breath->SetVolumeMultiplier(0.001f); Breath->Play(); }
		}
	}
}

float UHWAudioSubsystem::TensionFromState(float PlayerHealthFrac, float BossHealthFrac)
{
	// The keeper's lead in health fractions (-1 .. 1), centred: the music tightens as it pulls ahead.
	return FMath::Clamp(0.5f + 0.75f * (BossHealthFrac - PlayerHealthFrac), 0.f, 1.f);
}

void UHWAudioSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (World == nullptr) { return; }
	const UHWDuelSubsystem* Duel = World->GetSubsystem<UHWDuelSubsystem>();
	const bool bRunning = Duel != nullptr && Duel->GetState() == EHWEncounterState::Running && Duel->GetEncounter() != nullptr;
	float TargetTension = 0.f;
	float TargetDanger = 0.f;
	if (bRunning)
	{
		const HW::FEncounter& E = *Duel->GetEncounter();
		const HW::FFighter& P = E.Duel.Get(HW::ESide::Player);
		const HW::FFighter& B = E.Duel.Get(HW::ESide::Boss);
		const float PH = P.HealthMax > 0.f ? P.Health / P.HealthMax : 1.f;
		const float BH = B.HealthMax > 0.f ? B.Health / B.HealthMax : 1.f;
		TargetTension = TensionFromState(PH, BH);
		TargetDanger = PH < 0.3f ? (0.3f - PH) / 0.3f : 0.f;
	}
	const float Smooth = 1.f - FMath::Exp(-DeltaTime / 1.2f);
	Tension += (TargetTension - Tension) * Smooth;
	Danger += (TargetDanger - Danger) * Smooth;
	const float Fade = bRunning || bDuel ? 1.f : 0.f;
	MusicLevel += (Fade - MusicLevel) * (1.f - FMath::Exp(-DeltaTime / (Fade > MusicLevel ? 1.5f : 3.f)));
	const float PauseDuck = World->IsPaused() ? 0.4f : 1.f;
	if (Music != nullptr)
	{
		Music->SetVolumeMultiplier(FMath::Max(0.001f, MusicVolume() * MusicLevel * (0.5f + 0.5f * Tension) * PauseDuck * 0.8f));
		Music->SetPitchMultiplier(1.f + 0.05f * Tension);
	}
	if (Breath != nullptr)
	{
		Breath->SetVolumeMultiplier(FMath::Max(0.001f, EffectsVolume() * MusicLevel * Danger * PauseDuck * 0.9f));
	}
}
