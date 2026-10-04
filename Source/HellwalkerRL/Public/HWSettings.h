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
// Difficulty is stored and shown here; PresetFor() holds its numbers. The keeper's damage scale and the parry assist's
// slow motion apply in both modes; the skill range only to Adaptive AI, where UHWSessionSubsystem::GetKeeperConfig walks
// it with the session's insight (HW::FRLInsight). The duel applies all of it at a fight's start, not the settings.
//
// The parry assist (presentation only, the rules never change): a red ring on the keeper's weapon while a parry pressed
// now would land, and — RingSlow — slow motion while it is lit. AssistFor() turns the setting and the difficulty into one
// fight's FHWAssistParams.

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
	Adaptive UMETA(Hidden)   // retired in 1.4 (the insight ramp replaced it): kept so old saves load, then sanitised to Normal
};

/** The parry assist (presentation only). Saves store the value: new ones go last. */
UENUM(BlueprintType)
enum class EHWParryAssist : uint8
{
	RingSlow,   // the red ring, and the duel slows down while it is lit (the difficulty sets how much)
	Ring,       // the ring only
	Off
};

/** One fight's parry assist (UHWSettingsSubsystem::AssistFor). */
struct FHWAssistParams
{
	/** The red ring while a parry pressed now would land. */
	bool bRing = false;
	/** The difficulty's extra tell: the keeper's attacks glow as they come (Easy). */
	bool bIncomingGlow = false;
	/** The duel's speed while the ring is lit and the player can act (1 = no slow motion). */
	float SlowScale = 1.f;
};

/** A difficulty's numbers (UHWSettingsSubsystem::PresetFor). */
struct FHWDifficultyPreset
{
	/** Adaptive AI: the keeper's skill knowing nothing of you (insight 0) .. knowing you (insight 1). */
	float SkillLo = 0.f;
	float SkillHi = 1.f;
	/** HW::FDuel::KeeperDamageScale: the keeper's health damage is multiplied by this (both modes). */
	float KeeperDamageScale = 1.f;
	/** The parry assist's slow motion (RingSlow). */
	float AssistSlowScale = 1.f;
	bool bIncomingGlow = false;
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
	/** Keyboard keys and mouse buttons only; not WASD, Esc, Enter, 1-2 (the modes), the console key, the wheel — nor, for explore actions, the explorer's own keys. */
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

	UPROPERTY() EHWDifficulty Difficulty = EHWDifficulty::Normal;
	UPROPERTY() EHWParryAssist ParryAssist = EHWParryAssist::RingSlow;   // added in 1.4: older saves load the default
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
	/** The difficulties the menu offers, Easy .. Hellwalker (in enum order; Adaptive is retired). */
	static constexpr int32 NumMenuDifficulties = 4;
	/**
	 * Every difficulty's numbers (pure, tested):
	 *   Easy        skill 0.00 .. 0.40   damage x0.60   slow motion 0.40   incoming glow
	 *   Normal      skill 0.00 .. 0.70   damage x0.75   slow motion 0.60
	 *   Hard        skill 0.15 .. 0.85   damage x0.85   slow motion 0.75
	 *   Hellwalker  skill 0.30 .. 1.00   damage x0.90   slow motion 0.85
	 * (the retired Adaptive gets Normal's).
	 */
	static FHWDifficultyPreset PresetFor(EHWDifficulty D);
	FHWDifficultyPreset GetPreset() const { return PresetFor(Data.Difficulty); }
	static FString DifficultyName(EHWDifficulty D);
	/** One or two sentences for the menu: how hard it hits, how it plays, what the assist does at this difficulty. */
	static FString DifficultyBlurb(EHWDifficulty D);

	/** The parry assist setting (or the -HWParryAssist= override). */
	EHWParryAssist GetParryAssist() const { return Data.ParryAssist; }
	void SetParryAssist(EHWParryAssist A);
	/** -HWParryAssist=RingSlow|Ring|Off was given: that value holds even in scripted launches (which otherwise play
	 *  without the assist), and nothing this session is saved (like -HWDifficulty). */
	bool HasParryAssistOverride() const { return bParryAssistOverride; }
	/** One fight's assist (pure, tested): the ring for RingSlow and Ring, the difficulty's slow motion only for RingSlow,
	 *  the difficulty's incoming glow unless the assist is Off (Off is no help at all). */
	static FHWAssistParams AssistFor(EHWParryAssist A, EHWDifficulty D);
	FHWAssistParams GetAssist() const { return AssistFor(Data.ParryAssist, Data.Difficulty); }
	/** The menu's names: "Ring + slow motion", "Ring only", "Off". */
	static FString ParryAssistName(EHWParryAssist A);
	static FString ParryAssistBlurb(EHWParryAssist A);
	/** The research record's names (web/CONTRACT.md fight v2 "assist"): "off", "ring", "ring+slowmo". */
	static FString AssistTelemetryName(const FHWAssistParams& P);
	/** "RingSlow" / "Ring" / "Off" (case-insensitive; also the telemetry names) -> the setting. False when unknown. */
	static bool ParseParryAssist(const FString& S, EHWParryAssist& Out);
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
	bool bParryAssistOverride = false;
};
