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

#define HWRL_ABI_VERSION 2   /* 2: keeper skill / identity per session, learning players, style rewards, exploiters (RL-4), attack logs */

/* ---- layout (HWCore/HWRLTypes.h) ------------------------------------------------------------------------------- */
HWRL_API int32_t hwrl_abi_version(void);
HWRL_API int32_t hwrl_obs_layout_version(void);
HWRL_API int32_t hwrl_obs_dim(void);            /* 107 */
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
 * skill), 6 Habit (procedural: response tables; learning when learn_rate > 0), 7 Policy (an exploiter network, RL-4:
 * policy_id from hwrl_batch_add_player_policy).
 * The spec also carries the KEEPER's side of the session: its skill and identity (the network's inputs). */
typedef struct HWRLPlayerSpec
{
	int32_t kind;
	float   skill;                 /* 0..1: the reference profile (reaction, timing noise, reads, punish rate) */
	int32_t fights_in_session;     /* 1..4: fights against this player with the keeper's memory carried over */
	/* Habit players: [table A / B][boss swing class: BFast, BHeavy, BFeint, BKiller][response: Parry, Block, StepL,
	 * StepR, StepB, StepF, Attack, None] weights. Table B answers from boss swing #habit_switch_after of the session on. */
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
	/* v2 */
	float   keeper_skill;          /* the keeper's difficulty this session, [0, 1] (RL::SkillParams); < 0 = 1 (Hellwalker) */
	int32_t keeper_identity;       /* the keeper: 0 Warden, 1 Sage, 2 Returned (RL::EKeeper); < 0 = 0. Its health scales
	                                  with it (RL::KeeperHealthScale) unless the env is immortal */
	float   learn_rate;            /* kind 6: > 0 = a learning player (FBotProfile::LearnRate); <= 0 = a fixed habit */
	float   learn_temp;            /* kind 6 learning: softmax temperature over the learned values; <= 0 = 0.35 */
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
	/* v2 */
	float   style_scale;           /* the per-identity style rewards (DESIGN.md §7) x this; 0 = off */
	int32_t reserved;              /* 0 */
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
	/* v2 */
	int32_t* identity;      /* the keeper identity of the step's fight */
	float*   skill;         /* the keeper skill of the step's fight */
	float*   style;         /* the style-reward part of `reward` (already included in it) */
	uint8_t* style_events;  /* [n, 4]: feint bites, evasions, guard breaks, guard-pressure blocks this step */
	int32_t* hits;          /* the keeper's swings this step whose outcome was a clean Hit (blocked chip damage is not) */
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
/* The loaded policy's sizes: out[8] = obs_dim, num_actions, aux_classes, hidden, history_tokens, token_fields, side,
 * recurrent. Arrays passed to hwrl_policy_forward / _aux are strided by THESE (a player policy differs from the boss). */
HWRL_API void  hwrl_policy_dims(void* policy, int32_t* out8);
/* n decisions: obs [n, obs_dim], tokens [n, T, F] (may be null when T*F = 0), mask [n, A] (may be null), h_in / h_out
 * [n, hidden] (h_in may be null), logits [n, A], value [n], argmax [n] — every size the POLICY's (hwrl_policy_dims). */
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
	/* v2 */
	int32_t boss_moves[22];       /* the keeper's committed moves by action index (BFastSlash .. BRetreat) */
	int32_t style_events[4];      /* feint bites, evasions, guard breaks, guard-pressure blocks */
	int32_t player_swings;
	float   distance_sum;         /* fighter distance at the keeper's commits (sum; / distance_samples = mean) */
	int32_t distance_samples;
} HWRLEvalStats;
/* The spec's keeper_skill / keeper_identity apply to arm 2; temperature > 0 makes the RL keeper sample (seeded). */
HWRL_API int32_t hwrl_eval_sessions(int32_t arm, void* policy, const HWRLPlayerSpec* spec, int32_t sessions, uint64_t seed,
	int32_t immortal, int32_t fight_seconds, int32_t script_index, float temperature, HWRLEvalStats* out);
/* The simulated player's own view (FBotDiag, summed over every fight): frames it was actionable / in range (185) / saw a
 * punish window, strings it began by reason, attacks committed, walking and guarding, distance summed per frame. */
typedef struct HWRLBotDiag
{
	int32_t frames, actionable, in_range, in_range_actionable, boss_open, boss_open_in_range, boss_swinging, defence_pending;
	int32_t punish_starts, aggro_starts, response_attacks, attack_commits, walk_fwd, walk_back, guarding;
	float   distance_sum;
	int32_t reserved[4];
} HWRLBotDiag;
/* hwrl_eval_sessions plus the game's Easy breather on the RL keeper (min_swing_gap frames from one attack's start to
 * its next opener; 0 = off) and, when bot_diag is not null, the simulated player's diagnostics. */
HWRL_API int32_t hwrl_eval_sessions_gap(int32_t arm, void* policy, const HWRLPlayerSpec* spec, int32_t sessions, uint64_t seed,
	int32_t immortal, int32_t fight_seconds, int32_t script_index, float temperature, int32_t min_swing_gap, HWRLEvalStats* out,
	HWRLBotDiag* bot_diag);
/* RL::EasySwingGap: the breather the game's Easy difficulty uses. */
HWRL_API int32_t hwrl_easy_swing_gap(void);

/* The reading test's raw material (habits.py, the in-training gate): `sessions` sessions of the RL keeper (greedy, the
 * game's C++ forward pass, a fresh memory each), session s against specs[s % num_specs], run on num_threads threads
 * (0 = hardware). One record per keeper attack, in session order; returns the number written (at most max_records;
 * -1 on bad arguments). Deterministic for any thread count. */
typedef struct HWRLAttackRecord
{
	int32_t session;              /* 0 .. sessions-1 */
	int32_t k;                    /* 1-based index of the attack in its session */
	int32_t action;               /* the attack's action index */
	int32_t outcome;              /* EHitOutcome: 0 unresolved, 1 hit, 2 whiff, 3 blocked, 4 parried */
	int32_t dealt;                /* 1 if the swing took health (chip damage through a block counts) */
	int32_t habit_phase;          /* the table the player planned its answer with (0 A, 1 B; -1 = it never answered) */
	int32_t fight;                /* fight index in the session */
} HWRLAttackRecord;
HWRL_API int32_t hwrl_eval_attack_log(void* policy, const HWRLPlayerSpec* specs, int32_t num_specs, int32_t sessions,
	uint64_t seed, int32_t immortal, int32_t fight_seconds, int32_t num_threads, HWRLAttackRecord* out, int32_t max_records,
	HWRLEvalStats* out_stats);

/* ---- RL-4: exploiter players (RL/native/HWRLPlayer.h) ----------------------------------------------------------- */
/* A player-side policy (a .hwrl with meta.side = 1, obs = hwrl_player_obs_dim, actions = hwrl_player_num_actions, no
 * history tokens) trained only to beat a frozen keeper. In the KEEPER's env (kind-7 specs) it plays the player: sampled
 * at temperature 1 from the env's own random stream, its recurrent state carried across the fights of a session. */
HWRL_API int32_t hwrl_player_obs_dim(void);
HWRL_API int32_t hwrl_player_num_actions(void);
HWRL_API const char* hwrl_player_action_name(int32_t a);
HWRL_API const char* hwrl_player_obs_feature_name(int32_t i);
/* Register a loaded player policy (hwrl_policy_load; it must outlive the batch) -> its policy_id, or -1. */
HWRL_API int32_t hwrl_batch_add_player_policy(void* batch, void* policy);

/* The exploiter's training env: num_envs fights of a player (decisions from Python every 4 frames) against a FROZEN
 * keeper (boss_policy, greedy, a fresh memory per session). Step results are from the PLAYER's side: reward = keeper
 * 2 x keeper damage dealt / keeper max - player damage taken / player max - 0.01 per decision out of reach (> 400 cm),
 * + 1 keeper died, - 1 player died, - 0.5 timeout; dmg_dealt = what the
 * player dealt, dmg_taken = what it took, swings = player attacks; result as for the keeper env (1 keeper won, 2 keeper
 * died, 3 timeout); cost / aux_label / read_counter / style are zero. */
HWRL_API void* hwrl_pbatch_create(int32_t num_envs, uint64_t seed, const HWRLEnvConfig* cfg, void* boss_policy);
HWRL_API void  hwrl_pbatch_destroy(void* pbatch);
HWRL_API int32_t hwrl_pbatch_size(void* pbatch);
/* The keeper for env's NEXT session (skill, identity, fights in the session, a tag echoed back). */
HWRL_API void  hwrl_pbatch_set_next_keeper(void* pbatch, int32_t env, float skill, int32_t identity, int32_t fights_in_session, int32_t tag);
HWRL_API void  hwrl_pbatch_reset(void* pbatch);
/* obs [n, player_obs_dim] f32, mask [n, player_num_actions] u8 */
HWRL_API void  hwrl_pbatch_observe(void* pbatch, float* obs, uint8_t* mask);
HWRL_API void  hwrl_pbatch_step(void* pbatch, const int32_t* actions, HWRLStepOut* out);

#ifdef __cplusplus
}
#endif
