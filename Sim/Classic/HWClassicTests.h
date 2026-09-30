// HellwalkerRL — CLASSIC (reference) brain, tools only. Its done-tests, and a runner for classic encounters.

#pragma once

#include "HWCore/HWCoreTests.h"
#include "HWCore/HWSim.h"
#include "Classic/HWClassicBrain.h"

namespace HW
{
	/** The classic brain's done-tests (run by ThesisSim --tests after the game core's). */
	int32_t GetClassicTests(const FCoreTest*& OutTests);

	/**
	 * One encounter of the classic brain on a (session-persistent) model — the reference project's RunEncounter.
	 * Script: HW::BossScript index; BrainCfg: nullptr = defaults (the per-script tuning is applied on top).
	 */
	FEncounterStats RunClassic(FPlaystyleModel& Model, EBrainMode Mode, const FRunConfig& Cfg, std::vector<FExchangeRow>* OutRows = nullptr,
		const FBrainConfig* BrainCfg = nullptr, int32_t Script = 0);
}
