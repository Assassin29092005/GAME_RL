// Hellwalker — engine-free core. The duel: both fighters stepped in lock-step, one frame at a time.
//
// Geometry is NOT in here. "Does this active hitbox touch the defender this frame?" is asked of an
// IContactOracle — per-tick sweeps in Unreal (PLAN §5.4), a 2-D abstraction in the simulator.
// Everything else — windows, outcomes, stun, sha-chi, frame advantage — is decided here, once,
// for both builds.

#pragma once

#include "HWFighter.h"

#include <vector>

namespace HW
{
	class FDuel;

	class IContactOracle
	{
	public:
		virtual ~IContactOracle() = default;
		/** Geometry only: does Attacker's currently-active hitbox touch its opponent on this frame? */
		virtual bool Contacts(const FDuel& Duel, ESide Attacker) = 0;
	};

	enum class EDuelEvent : uint8_t
	{
		Commit,       // a fighter committed to a move (or raised guard: Move=None, Sym=Block)
		Outcome,      // a swing resolved — exactly once per swing
		ParryWhiff,   // a parry / counter window closed with nothing caught
		FeintFake,    // a feint's fake impact frame (presentation + observer)
		GuardBreak,   // sha-chi broken (player guard break / boss exposed)
		Death
	};

	struct FDuelEvent
	{
		EDuelEvent  Type = EDuelEvent::Commit;
		int32_t     Frame = 0;
		ESide       Side = ESide::Player;       // the actor: committer / attacker / broken / dead
		EMoveId     Move = EMoveId::None;
		ESym        Sym = ESym::Neutral;
		EHitOutcome Outcome = EHitOutcome::None;
		float       Damage = 0.f;               // health damage dealt (Outcome)
		int32_t     FrameAdvantage = 0;         // attacker's advantage after resolution (Outcome)
		bool        bCounterHit = false;        // outcome caught the defender in startup / recovery
		uint8_t     DefenderPhase = 0;          // EDefenderPhase at contact (telemetry: where damage comes from)
	};

	/** What the defender was doing when a swing resolved. */
	enum class EDefenderPhase : uint8_t { Idle, Startup, Active, Recovery, Stunned, GuardBroken, Stepping, Guarding, Moving, Other, Count };
	const char* DefenderPhaseName(EDefenderPhase P);
	EDefenderPhase DefenderPhaseOf(const FFighter& F);

	class FDuel
	{
	public:
		FFighter Fighters[2];
		int32_t  Frame = 0;
		/** Configuration, not state (Reset keeps it): the keeper's attacks deal this fraction of their HEALTH damage (a hit
		 *  and the chip through a guard; never sha-chi drain, never the player's damage). The game's difficulty sets it
		 *  per fight; training, hwrl.dll and ThesisSim's defaults stay at 1, which is bit-identical to no scale at all. */
		float    KeeperDamageScale = 1.f;

		void Reset();

		FFighter& Get(ESide S) { return Fighters[SideIndex(S)]; }
		const FFighter& Get(ESide S) const { return Fighters[SideIndex(S)]; }
		bool IsOver() const { return Fighters[0].IsDead() || Fighters[1].IsDead(); }

		/** Commands — valid between Step() calls; the move's frame 0 is the next stepped frame. */
		bool CanCommit(ESide S, EMoveId Id) const;
		bool Commit(ESide S, EMoveId Id, float FacingX = 1.f, float FacingY = 0.f);
		/** Player attack buttons: resolves light-chain position and weapon. */
		bool CommitPlayerAttack(bool bHeavy, float FacingX = 1.f, float FacingY = 0.f);
		/** Player block is held (Enhanced Input). Raising guard emits a Block commitment. */
		void SetGuardHeld(ESide S, bool bHeld);
		/** Player weapon switch (C1). */
		bool CommitSwitch();

		/** Resolve one frame: contacts -> outcomes -> advance. */
		void Step(IContactOracle& Oracle);

		/** Positive = Side can act that many frames before its opponent (PLAN §6: one line in the brain). */
		int32_t FrameAdvantage(ESide S) const;

		/** Events raised since the last TakeEvents(). Commits land here too. */
		std::vector<FDuelEvent>& PendingEvents() { return Events; }
		void TakeEvents(std::vector<FDuelEvent>& Out) { Out.swap(Events); Events.clear(); }

	private:
		EHitOutcome Classify(ESide Attacker) const;
		void ApplyOutcome(ESide Attacker, EMoveId MoveId, EHitOutcome Outcome);
		void Push(const FDuelEvent& E) { Events.push_back(E); }
		void RaiseWhiffIfPending(ESide S);

		std::vector<FDuelEvent> Events;
		bool bResolvingThisFrame[2] = { false, false };
	};
}
