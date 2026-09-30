// Hellwalker — engine-free core. The payoff matrix, DERIVED from the combat rules.
//
// PLAN §2.4: "the payoff matrix is where a correct prediction becomes a real counter". B0 found the
// hand-authored matrix contradicting the frame data (a heavy "armoring through" a light that lands
// two frames before its armor starts; "delayed" honest moves "baiting" players who time to what
// they see). So each entry is now measured: for boss move c, player response s pressed `Lead` frames
// before c's perceived impact, run a short duel through the SAME FDuel the game uses, with the
// player punishing whatever opening it is given, and score the net exchange in fractions of each
// side's health — the quantity "harder" is actually about.
//
// The plan's authoring rule survives as a clamp: defensive actions cap at +1; only attacks reach
// +2 / +3. Deterministic, built once, no allocation after build.

#pragma once

#include "HWMoves.h"

namespace HW
{
	class FPayoffTable
	{
	public:
		static constexpr int32_t MinLead = -12;
		static constexpr int32_t MaxLead = 40;
		static constexpr int32_t NumLeads = MaxLead - MinLead + 1;
		static constexpr float   Scale = 30.f;   // payoff units per unit of (health fraction dealt - taken)

		/** Payoff of boss move Boss vs player symbol Sym pressed Lead frames before Boss's perceived impact. */
		float Get(EMoveId Boss, ESym Sym, int32_t Lead) const;
		/** With the "typical player" lead for Sym (used when the timing sidecar has no evidence). */
		float GetTypical(EMoveId Boss, ESym Sym) const { return Get(Boss, Sym, TypicalLead(Sym)); }
		static int32_t TypicalLead(ESym Sym);

		/** Rebuild from the current move table / tuning (call after config overrides). */
		void Build();
		bool IsBuilt() const { return bBuilt; }

		/** One micro-duel, exposed for tests and the simulator's --payoffs dump. Returns raw net exchange. */
		static float MeasureExchange(EMoveId Boss, ESym Sym, int32_t Lead, float* OutBossDealt = nullptr, float* OutPlayerDealt = nullptr);

	private:
		float V[NumMoves][NumPlayerSymbols][NumLeads] = {};
		bool bBuilt = false;
	};

	/** The shared table, built on first use. */
	const FPayoffTable& DerivedPayoffs();
	/** Force a rebuild (after UCombatAnimConfig / simulator overrides of the move table). */
	void RebuildDerivedPayoffs();
}
