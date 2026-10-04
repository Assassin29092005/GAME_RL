// HellwalkerRL — session state that outlives an encounter: what the keepers keep about YOU, and the RL keeper's weights.
//
// RL.md §8: one memory per game session (the RL keeper's recurrent state + the exchanges it perceived), carried from
// keeper to keeper and dropped on quit. A GameInstance subsystem lives exactly as long as the session, so "reset on
// quit" is structural. The weights (RL/Models/hellwalker_rl.hwrl, staged as Content/HellwalkerRL/RL/hellwalker_rl.hwrl)
// are loaded once; if the file is missing, the Hellwalker tier falls back to the script (the optional-asset pattern).
//
// Insight (HW::FRLInsight) is the Adaptive AI mode's learning arc, also per session: how well the read head has been
// calling YOUR answers. It sets the next fight's keeper within the difficulty's skill range (GetKeeperConfig), rises as a
// repeated trick keeps coming true and falls when you change tricks; ResetMemory and quitting drop it with the memory.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HWSettings.h"
#include "HWTypesUE.h"
#include "HWCore/HWRLBrain.h"
#include "HWCore/HWRLObserver.h"
#include "HWCore/HWRLPolicy.h"
#include "HWSessionSubsystem.generated.h"

UCLASS()
class HELLWALKERRL_API UHWSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The RL keeper's weights; null when no model file loaded (then Hellwalker plays the script). */
	const HW::FRLPolicy* GetPolicy() const { return Policy.IsLoaded() ? &Policy : nullptr; }
	/** "loaded <path> (hidden 256)" or why not. */
	const FString& GetPolicyStatus() const { return PolicyStatus; }
	/** What the keepers keep about you this session. */
	HW::FRLSession& GetMemory() { return *Memory; }
	const HW::FRLSession& GetMemory() const { return *Memory; }
	/** Forget everything the keepers learned this session (hw.ResetModel, a new walk): memory, notebook, insight. */
	void ResetMemory();
	/** What the keepers wrote down about you this session (the notebook screen). Filled by the RL keeper; never an input. */
	const HW::FRLNotebook& GetNotebook() const { return Notebook; }
	HW::FRLNotebook& GetNotebookMutable() { return Notebook; }

	// ---- the Adaptive AI keeper's strength: the difficulty's skill range, walked by this session's insight ----------
	/**
	 * The next Adaptive AI fight's skill / sampling temperature / breather (frames from one keeper attack's commit to its
	 * next opener): UHWSettingsSubsystem::PresetFor(difficulty).SkillLo..SkillHi through HW::FRLInsight::KeeperConfig.
	 * bOutAdaptive: true (the insight ramp set the skill). Parity runs (-HWParity=) get the fixed keeper parity always
	 * measured, whatever the difficulty and insight: skill 1, greedy, no breather, bOutAdaptive false.
	 */
	void GetKeeperConfig(float& OutSkill, float& OutTemperature, bool& bOutAdaptive, int32& OutSwingGap) const;
	/**
	 * After an RL fight (call once the brain's notebook has flushed it — UHWDuelSubsystem::EndEncounter does): insight
	 * moves toward how well the read head called your answers since the last record (HW::FRLInsight::AfterFight).
	 * Returns the insight change. The result and health arguments only feed the log.
	 */
	float RecordFightForAdaptive(bool bPlayerWon, float PlayerHealthFrac, float BossHealthFrac);
	/** How well the keepers know you, 0..1 (0 for a fresh session). */
	float GetInsight() const { return Insight.Insight; }
	/** How much the last recorded fight moved it. */
	float GetLastInsightChange() const { return LastInsightChange; }
	const HW::FRLInsight& GetInsightState() const { return Insight; }
	/** The skill the next Adaptive AI fight gets (GetKeeperConfig's), and how much the last recorded fight moved it (the
	 *  result screen's "the keeper adapts" line). */
	float GetAdaptiveSkill() const;
	float GetLastAdaptiveChange() const { return LastSkillChange; }
	/** Pure (tested): a difficulty's keeper at an insight — the preset's skill range through Insight.KeeperConfig. */
	static void KeeperConfigFor(EHWDifficulty Difficulty, const HW::FRLInsight& InInsight, float& OutSkill, float& OutTemperature, int32& OutSwingGap);
	/** -HWParity=<file> is on the command line (Tools\Parity.bat): the keeper plays the fixed config parity measures. */
	static bool IsParityRun();
	/** The read head, from the current memory: "if the keeper threw Move now, you would answer OutSym (OutP)". */
	bool PredictAnswer(HW::EMoveId Move, HW::ESym& OutSym, float& OutP) const;

	/** Current tier. Switching tier keeps the memory (it is you being read, whichever keeper reads). */
	UPROPERTY(BlueprintReadOnly, Category = "Hellwalker")
	EHWTier Tier = EHWTier::Hellwalker;

	/** Blind mode for B4: the HUD shows "Variant A / B" instead of tier names. */
	UPROPERTY(BlueprintReadOnly, Category = "Hellwalker")
	bool bBlind = false;

	/** Blind mapping: which tier is "A". Randomised once per session so testers cannot learn it. */
	UPROPERTY(BlueprintReadOnly, Category = "Hellwalker")
	EHWTier BlindVariantA = EHWTier::Pathbreaker;

	UPROPERTY(BlueprintReadOnly, Category = "Hellwalker")
	int32 EncountersStarted = 0;

	/** Session id used to name telemetry files (A2). */
	UPROPERTY(BlueprintReadOnly, Category = "Hellwalker")
	FString SessionId;

	/** "NORMAL" (the script) / "ADAPTIVE AI" (the RL keeper), or "VARIANT A / B" in blind mode. */
	FString TierLabel(EHWTier InTier) const;

	/** Where the game looks for the weights (overridable with -HWPolicy=<file>). */
	static FString PolicyPath();

private:
	void LoadPolicy();

	HW::FRLPolicy Policy;
	TUniquePtr<HW::FRLSession> Memory;
	HW::FRLNotebook Notebook;
	HW::FRLInsight Insight;
	float LastInsightChange = 0.f;
	float LastSkillChange = 0.f;
	/** The notebook's totals already fed to Insight: the next record takes what was added since (one fight's calls). */
	int32 InsightPredictionsSeen = 0;
	int32 InsightCorrectSeen = 0;
	FString PolicyStatus;
};
