/* HellwalkerRL — tools only. The C ABI of hwrl.dll: the RL training environment, the C++ policy forward pass and
 * quick C++ evaluation, for the Python trainer (RL/hwcore.py loads it with ctypes — no pybind11, nothing to install).
 *
 * Built by RL/native/build.bat with the same MSVC flags as Sim/build.bat, from the SAME engine-free core the game
 * compiles (Source/HellwalkerRL/Private/HWCore) plus the classic reference brain (Sim/Classic, the benchmark arm).
 *
 * Conventions: every array is caller-owned, C-contiguous, sized by the counts below; batch arrays are [num_envs, ...].
 * Deterministic: a batch created with the same seed, fed the same player specs and actions, reproduces bit for bit
 * regardless of the thread count. */

#pragma once

#include <stdint.h>

#ifdef HWRL_EXPORTS
#define HWRL_API __declspec(dllexport)
#else
#define HWRL_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define HWRL_ABI_VERSION 1

/* ---- layout (HWCore/HWRLTypes.h) ------------------------------------------------------------------------------- */
HWRL_API int32_t hwrl_abi_version(void);
HWRL_API int32_t hwrl_obs_layout_version(void);
HWRL_API int32_t hwrl_obs_dim(void);            /* 102 */
HWRL_API int32_t hwrl_num_actions(void);        /* 23 */
HWRL_API int32_t hwrl_history_tokens(void);     /* 32 */
HWRL_API int32_t hwrl_token_fields(void);       /* 6 */
HWRL_API int32_t hwrl_aux_classes(void);        /* 12 */
HWRL_API void    hwrl_token_vocab(int32_t* out_fields);  /* token_fields entries */
HWRL_API const char* hwrl_obs_feature_name(int32_t i);
HWRL_API const char* hwrl_action_name(int32_t a);
HWRL_API const char* hwrl_answer_name(int32_t c);        /* the 12 player symbols */

/* ---- simulated players (RL.md §6) ------------------------------------------------------------------------------ */
/* kind: 0 Masher, 1 Turtle, 2 Habitual, 3 Varied, 4 DodgerLeft, 5 RhythmParrier (the reference bots, profile from
 * skill), 6 Habit (procedural: response tables), 7 Policy (an exploiter network, RL-4; see hwrl_batch_set_player_policy). */
typedef struct HWRLPlayerSpec
{
	int32_t kind;
	float   skill;                 /* 0..1: the reference profile (reaction, timing noise, reads, punish rate) */
	int32_t fights_in_session;     /* 1..4: fights against this player with the keeper's memory carried over */
	/* Habit players: [table A / B][boss swing class: BFast, BHeavy, BFeint, BKiller][response: Parry, Block, StepL,
	 * StepR, StepB, StepF, Attack, None] weights. Table B replaces A after habit_switch_after boss swings in the session. */
	float   habit[2][4][8];
	int32_t habit_switch_after;    /* -1 = never */
	float   habit_noise;           /* P(uniform random response) */
	int32_t adapts;                /* 1 = the reference bots' wariness (feints, side flips) */
	/* Overrides of the skill-derived profile; a negative value keeps the default. */
	float   react_mean, react_sigma, timing_sigma, parry_aim, step_aim, feint_read, killer_read;
	float   punish_rate, aggro_rate, heavy_rate, switch_rate, preferred_range;
	int32_t chain_len;
	int32_t policy_id;             /* kind 7 */
	int32_t tag;                   /* the trainer's own label (population band), echoed back in step results */
} HWRLPlayerSpec;

/* ---- the environment (RL.md §3) --------------------------------------------------------------------------------- */
typedef struct HWRLEnvConfig
{
	int32_t max_fight_seconds;     /* 180: a fight ends on a death or this timeout */
	float   target_spm;            /* the aggression constraint's target, swings/min (also the ObsSwingDeficit feature) */
	float   start_distance_min;    /* each fight starts with the fighters this far apart (uniform in [min, max]) */
	float   start_distance_max;
	int32_t immortal;              /* 1 = nobody dies (rate measurement, B0-style); fights last max_fight_seconds */
	int32_t num_threads;           /* 0 = hardware concurrency */
} HWRLEnvConfig;

/* Step results, one entry per env (caller-owned arrays of length num_envs). */
typedef struct HWRLStepOut
{
	float*   reward;        /* (player damage taken)/player max - (boss damage taken)/boss max, + terminal: +1 player died,
	                           -1 keeper died, 0 timeout */
	float*   cost;          /* aggression floor, a per-window hinge: max(0, target_spm - swings/min over the last 30 s)/60
	                           * the step's seconds (0 in the first 15 s of a fight) */
	uint8_t* fight_done;    /* the fight ended during this step (the env already started the next one) */
	uint8_t* session_done;  /* ... and it was the session's last fight: the next observation is a NEW player (reset memory) */
	uint8_t* result;        /* when fight_done: 1 keeper won (player died), 2 keeper died, 3 timeout; else 0 */
	int8_t*  aux_label;     /* the player's answer to the action just taken (0..11), -1 if none */
	int32_t* frames;        /* frames simulated in this step */
	int32_t* swings;        /* keeper attacks committed in this step */
	float*   dmg_dealt;     /* raw health the keeper took from the player this step */
	float*   dmg_taken;     /* raw health the keeper lost this step */
	uint8_t* read_counter;  /* a READ counter landed this step (needs aux_top / aux_top_p in hwrl_batch_step) */
	int32_t* tag;           /* the spec's tag of the player this step was played against */
	int32_t* fight_index;   /* fight index in the session of the step's fight */
	int32_t* habit_phase;   /* the habit table the player answered with (0 A, 1 B) */
} HWRLStepOut;

HWRL_API void* hwrl_batch_create(int32_t num_envs, uint64_t seed, const HWRLEnvConfig* cfg);
HWRL_API void  hwrl_batch_destroy(void* batch);
HWRL_API int32_t hwrl_batch_size(void* batch);
/* The player for env's NEXT session (sessions start on reset and after session_done). */
HWRL_API void  hwrl_batch_set_next_player(void* batch, int32_t env, const HWRLPlayerSpec* spec);
/* Every env starts a new session with its pending player and runs to its first decision. */
HWRL_API void  hwrl_batch_reset(void* batch);
/* The current decision of every env: obs [n, obs_dim] f32, tokens [n, history_tokens, token_fields] i8, mask [n, num_actions] u8. */
HWRL_API void  hwrl_batch_observe(void* batch, float* obs, int8_t* tokens, uint8_t* mask);
/* Apply one action per env and simulate each to its next decision (fights / sessions roll over automatically).
 * aux_top / aux_top_p (may be null): the read head's top answer and its probability for each action (READ banner stats). */
HWRL_API void  hwrl_batch_step(void* batch, const int32_t* actions, const int8_t* aux_top, const float* aux_top_p, HWRLStepOut* out);

/* ---- the C++ forward pass (HWCore/HWRLPolicy.h): parity tests and C++-side evaluation --------------------------- */
HWRL_API void* hwrl_policy_load(const char* path, char* err, int32_t err_len);
HWRL_API void  hwrl_policy_free(void* policy);
HWRL_API int32_t hwrl_policy_hidden(void* policy);
/* n decisions: obs [n, obs_dim], tokens [n, T, F], mask [n, A] (may be null), h_in / h_out [n, hidden] (h_in may be null),
 * logits [n, A], value [n], argmax [n]. */
HWRL_API void  hwrl_policy_forward(void* policy, int32_t n, const float* obs, const int8_t* tokens, const uint8_t* mask,
	const float* h_in, float* h_out, float* logits, float* value, int32_t* argmax);
/* The read head: h [n, hidden] (the NEW hidden state), actions [n] -> probs [n, aux_classes]. */
HWRL_API void  hwrl_policy_aux(void* policy, int32_t n, const float* h, const int32_t* actions, float* probs);

/* ---- evaluation with the brains in C++ (RL.md §7: identical seeded players for every arm) ----------------------- */
/* arm: 0 Pathbreaker (script), 1 classic Hellwalker (reference tally brain), 2 RL keeper (policy required). */
typedef struct HWRLEvalStats
{
	int32_t fights;
	int32_t player_deaths, boss_deaths, timeouts;
	float   seconds;              /* total fight time */
	float   player_dmg_taken;     /* health the keeper took from the players */
	float   boss_dmg_taken;
	int32_t boss_swings;
	int32_t boss_hits, boss_whiffs, boss_blocked, boss_parried;
	int32_t decisions;
	int32_t read_counters;
} HWRLEvalStats;
HWRL_API int32_t hwrl_eval_sessions(int32_t arm, void* policy, const HWRLPlayerSpec* spec, int32_t sessions, uint64_t seed,
	int32_t immortal, int32_t fight_seconds, int32_t script_index, HWRLEvalStats* out);

#ifdef __cplusplus
}
#endif
