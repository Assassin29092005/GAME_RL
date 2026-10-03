// HellwalkerRL — sound and feel for the duel. No hand-made sound assets: the cues already in Content (the slash-trail
// pack's swings and hits, the Paragon heroes' voices, the starter music) are loaded by path, and a missing pack simply
// stays silent (the optional-asset pattern).
//
// UHWDuelSubsystem feeds it the duel's events (HandleEvents) and the READ moment; it plays swings by weight, the keeper's
// effort vocals on a share of its swings (Sevarog's voice for the Warden, Wukong's for the Sage, Sevarog's slowed and
// lowered for the Returned), hits, a bright parry, a dull block, ghoststeps, guard breaks, deaths and the READ sting (a
// distorted slash under the keeper's laugh: "it read me"). The duel music fades in for the fight and tightens as the
// keeper pulls ahead (TensionFromState: both health bars, smoothed), and
// a breathing low-health layer comes in when the player is close to death. Volumes come from UHWSettingsSubsystem.
// Counts per category are logged when a duel ends (scripted checks read them).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HWCore/HWDuel.h"
#include "HWAudio.generated.h"

class USoundBase;
class UAudioComponent;

/** What the cue table knows how to play. */
enum class EHWCue : uint8
{
	SwingFast, SwingHeavy, SwingFeint, KillerWarn, PlayerSwing,
	HitPlayer, HitBoss, Parry, ParryRing, Block, Step, GuardBreak,
	BossEffort, BossPain, BossDeath, BossLaugh, ReadSting,
	Count
};

UCLASS()
class HELLWALKERRL_API UHWAudioSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; } // the music keeps playing (quieter) under the pause menu

	/** One duel event at a world location. Identity: the RL keeper the boss is (0 Warden, 1 Sage, 2 Returned). */
	void OnDuelEvent(const HW::FDuelEvent& Ev, const FVector& Where, int32 Identity);
	/** The READ banner fired: a read counter landed. */
	void OnRead(const FVector& Where, int32 Identity);
	void OnDuelStart(int32 Identity);
	void OnDuelEnd(bool bPlayerWon);

	/** Music tension in [0, 1]: how far the keeper is ahead, from both health bars (pure, tested) — 0.5 at an even fight,
	 *  1 when the keeper has a big lead, 0 when it is the one about to fall. (Not the RL critic: its value is the
	 *  normalised REMAINING return, which falls as the keeper closes in on a kill, so it is no win estimate.) */
	static float TensionFromState(float PlayerHealthFrac, float BossHealthFrac);
	/** The current smoothed tension (HUD / tests). */
	float GetTension() const { return Tension; }
	int32 GetCount(EHWCue Cue) const { return Counts[static_cast<int32>(Cue)]; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	USoundBase* Load(EHWCue Cue);
	USoundBase* LoadPath(const TCHAR* Path);
	void Play(EHWCue Cue, const FVector& Where, bool b2D = false, float PitchScale = 1.f, float VolumeScale = 1.f);
	float EffectsVolume() const;
	float MusicVolume() const;
	void EnsureMusic();

	UPROPERTY() TMap<FString, TObjectPtr<USoundBase>> Loaded;
	UPROPERTY() TObjectPtr<UAudioComponent> Music;
	UPROPERTY() TObjectPtr<UAudioComponent> Breath;
	TSet<FString> Missing;
	int32 Counts[static_cast<int32>(EHWCue::Count)] = {};
	int32 Identity = 0;
	bool bDuel = false;
	float MusicLevel = 0.f;     // the fade
	float Tension = 0.f;
	float Danger = 0.f;
	uint32 Rand = 0x9E3779B9u;  // effort-vocal choices (presentation only)
};
