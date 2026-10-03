// HellwalkerRL — player settings: difficulty, look, key bindings, graphics, audio, accessibility.
//
// UHWSettingsSave (slot "HellwalkerRL_Settings", versioned) holds an FHWSettingsData; UHWSettingsSubsystem (one per game
// instance) loads it on Initialize, saves on every change and applies what the engine owns (audio now, graphics on
// Apply; UGameUserSettings persists graphics itself). -HWNoSave (scripted checks) keeps every change in memory only.
//
// Key bindings: one keyboard/mouse key per rebindable action (FHWBindingTable — plain data, unit-tested). Gamepad
// buttons are fixed and shown read-only. AHWPlayerController rebuilds its Enhanced Input mapping contexts from the table
// whenever it changes (OnBindingsChanged).
//
// Difficulty is stored and shown here; GetKeeperSkill() is what the keeper is configured with (HW::FRLBrain::Configure
// + SetTemperature — wired by the duel, not by the settings).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "InputCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HWSettings.generated.h"

UENUM(BlueprintType)
enum class EHWDifficulty : uint8
{
	Easy,
	Normal,
	Hard,
	Hellwalker,
	Adaptive
};

/** The rebindable actions (keyboard / mouse). Movement (WASD) and Esc are fixed. New actions go last: saves store the index. */
UENUM(BlueprintType)
enum class EHWBind : uint8
{
	Light,
	Heavy,
	Parry,
	Guard,
	Step,
	Switch,
	LockOn,
	Restart,
	Help,
	Debug,
	Pause,
	Notebook,
	Interact,
	Map,
	Count UMETA(Hidden)
};

USTRUCT()
struct HELLWALKERRL_API FHWKeyBinding
{
	GENERATED_BODY()

	UPROPERTY() EHWBind Action = EHWBind::Light;
	UPROPERTY() FKey Key;
};

/** Where an action lives: the duel's mapping context, the explore one, or both. Two actions clash only when they share one. */
enum class EHWBindScope : uint8
{
	Duel = 1,
	Explore = 2,
	Both = 3
};

/**
 * One keyboard/mouse key per action, with the conflict rules. Rebinding to a key another action in the same scope already
 * uses SWAPS the two (the other action takes this one's old key) when that leaves a valid table; reserved keys, gamepad
 * buttons and axes are refused. Every outcome comes with a message for the menu.
 */
struct HELLWALKERRL_API FHWBindingTable
{
	static constexpr int32 Num = static_cast<int32>(EHWBind::Count);

	enum class EResult : uint8 { Unchanged, Bound, Swapped, Refused };

	FHWBindingTable() { ResetToDefaults(); }

	void ResetToDefaults();
	const FKey& Get(EHWBind Action) const { return Keys[static_cast<int32>(Action)]; }
	EResult Rebind(EHWBind Action, const FKey& Key, FString& OutMessage);
	/** No two actions sharing a scope on one key, no reserved or invalid key. */
	bool IsValid() const;
	bool operator==(const FHWBindingTable& O) const;

	static FKey DefaultKey(EHWBind Action);
	/** The fixed gamepad button ("X", "LB (hold)", "pause menu"). */
	static FString GamepadLabel(EHWBind Action);
	static FKey GamepadKey(EHWBind Action);
	static FString ActionLabel(EHWBind Action);
	static EHWBindScope Scope(EHWBind Action);
	/** A short on-screen name: "LMB", "Space", "E", "F1". */
	static FString KeyLabel(const FKey& Key);
	/** Keyboard keys and mouse buttons only; not WASD, Esc, Enter, 1-3, the console key, the wheel — nor, for explore actions, the explorer's own keys. */
	static bool IsBindable(const FKey& Key, EHWBindScope InScope, FString& OutWhyNot);

	TArray<FHWKeyBinding> ToArray() const;
	/**
	 * Unknown or invalid entries are ignored; a table that would clash falls back to the defaults. An older save lacks the
	 * actions added since (Map): each takes its default key, or — when the player has since put another action on that
	 * key — the first free one from a fallback list (the saved keys win).
	 */
	void FromArray(const TArray<FHWKeyBinding>& In);

private:
	/** Key is bindable for Action and no other action sharing its scope holds it. */
	bool Fits(EHWBind Action, const FKey& Key) const;

	FKey Keys[Num];
};

/** Everything the player sets, as saved. Setters clamp (pure: unit-tested). */
USTRUCT()
struct HELLWALKERRL_API FHWSettingsData
{
	GENERATED_BODY()

	static constexpr float MinSensitivity = 0.25f;
	static constexpr float MaxSensitivity = 3.f;
	static constexpr float MinHudScale = 0.75f;
	static constexpr float MaxHudScale = 1.5f;
	static constexpr float MinReadHold = 0.4f;
	static constexpr float MaxReadHold = 2.f;

	UPROPERTY() EHWDifficulty Difficulty = EHWDifficulty::Hellwalker;
	UPROPERTY() float MouseSensitivity = 1.f;
	UPROPERTY() bool bInvertY = false;
	UPROPERTY() TArray<FHWKeyBinding> Bindings;
	UPROPERTY() float MasterVolume = 1.f;
	UPROPERTY() float MusicVolume = 1.f;
	UPROPERTY() float EffectsVolume = 1.f;
	UPROPERTY() float HudScale = 1.f;
	UPROPERTY() float ReadHoldSeconds = 0.4f;
	UPROPERTY() bool bCameraShake = true;
	UPROPERTY() bool bTutorialSeen = false;

	/** Clamp everything into range (after a load, or a hand-edited save). */
	void Sanitize();
};

UCLASS()
class HELLWALKERRL_API UHWSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* SlotName = TEXT("HellwalkerRL_Settings");
	static constexpr int32 Version = 1;

	UPROPERTY() int32 SaveVersion = Version;
	UPROPERTY() FHWSettingsData Data;

	/** Null when the slot is empty or written by a newer build. Older versions load and are sanitized. */
	static UHWSettingsSave* LoadOrNull(const FString& Slot = SlotName);
	bool Write(const FString& Slot = SlotName);
};

/** The graphics choices being edited (applied only on Apply). */
struct FHWGraphicsChoice
{
	int32 Quality = 3;                 // 0 Low .. 4 Cinematic; -1 = custom (left alone by Apply)
	FIntPoint Resolution = FIntPoint(1920, 1080);
	uint8 WindowMode = 1;              // EWindowMode: 0 fullscreen, 1 windowed fullscreen, 2 windowed
	bool bVSync = false;
	int32 FrameLimit = 3;              // index into FrameLimits()
};

UCLASS()
class HELLWALKERRL_API UHWSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Apply what the engine owns (master volume) — on Initialize and again when play starts. */
	void ApplyAll();

	// ---- gameplay ---------------------------------------------------------------------------------
	EHWDifficulty GetDifficulty() const { return Data.Difficulty; }
	void SetDifficulty(EHWDifficulty D);
	/** What the keeper is configured with for the current difficulty (see KeeperSkillFor). */
	void GetKeeperSkill(float& OutSkill, float& OutTemperature, bool& bOutAdaptive) const;
	/** Easy (0.0, 1.0, no) · Normal (0.4, 0.6, no) · Hard (0.75, 0, no) · Hellwalker (1, 0, no) · Adaptive (0.6, 0, yes). */
	static void KeeperSkillFor(EHWDifficulty D, float& OutSkill, float& OutTemperature, bool& bOutAdaptive);
	/** Easy's breather: frames from one keeper attack's commit to its next opener (HW::RL::EasySwingGap); 0 otherwise. */
	static int32 KeeperSwingGapFor(EHWDifficulty D);
	static FString DifficultyName(EHWDifficulty D);
	static FString DifficultyBlurb(EHWDifficulty D);
	float GetMouseSensitivity() const { return Data.MouseSensitivity; }
	void SetMouseSensitivity(float V);
	bool GetInvertY() const { return Data.bInvertY; }
	void SetInvertY(bool b);

	// ---- controls ---------------------------------------------------------------------------------
	const FHWBindingTable& GetBindings() const { return Bindings; }
	FHWBindingTable::EResult Rebind(EHWBind Action, const FKey& Key, FString& OutMessage);
	void ResetBindings();
	DECLARE_MULTICAST_DELEGATE(FHWOnBindingsChanged);
	FHWOnBindingsChanged OnBindingsChanged;

	// ---- graphics (edited, then applied) -------------------------------------------------------------
	/** Read the engine's current graphics settings into the pending choice (when the Graphics tab opens). */
	void LoadGraphics();
	FHWGraphicsChoice& PendingGraphics() { return Graphics; }
	const TArray<FIntPoint>& GetResolutions() const { return Resolutions; }
	bool HasUnappliedGraphics() const;
	/** UGameUserSettings: scalability, resolution, window mode, VSync, frame limit — ApplySettings + SaveSettings. */
	void ApplyGraphics();
	static const TArray<float>& FrameLimits();
	static FString QualityName(int32 Level);

	// ---- audio ------------------------------------------------------------------------------------
	float GetMasterVolume() const { return Data.MasterVolume; }
	float GetMusicVolume() const { return Data.MusicVolume; }
	float GetEffectsVolume() const { return Data.EffectsVolume; }
	void SetMasterVolume(float V);
	void SetMusicVolume(float V);
	void SetEffectsVolume(float V);

	// ---- accessibility ----------------------------------------------------------------------------
	float GetHudScale() const { return Data.HudScale; }
	void SetHudScale(float V);
	float GetReadHoldSeconds() const { return Data.ReadHoldSeconds; }
	void SetReadHoldSeconds(float V);
	bool GetCameraShake() const { return Data.bCameraShake; }
	void SetCameraShake(bool b);
	bool IsTutorialSeen() const { return Data.bTutorialSeen; }
	void SetTutorialSeen(bool b);

	// ---- persistence ------------------------------------------------------------------------------
	const FHWSettingsData& GetData() const { return Data; }
	/** Write the slot (no-op with -HWNoSave). */
	bool Save();
	bool IsPersistent() const { return bPersist; }

private:
	void Changed();
	void ApplyAudio();

	FHWSettingsData Data;
	FHWBindingTable Bindings;
	FHWGraphicsChoice Graphics;
	FHWGraphicsChoice GraphicsApplied;
	TArray<FIntPoint> Resolutions;
	bool bPersist = true;
};
