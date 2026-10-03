// HellwalkerRL — tools only. hwrl.dll: the C ABI over the training environment, the C++ forward pass and C++-side
// evaluation (see hwrl_capi.h). Thin wrappers — every behaviour lives in HWRLEnv.cpp / HWRLPlayer.cpp and the core.

#include "hwrl_capi.h"

#include "HWRLEnv.h"
#include "HWRLPlayer.h"
#include "HWCore/HWRLPolicy.h"
#include "HWCore/HWRLTypes.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace HW;

extern "C"
{
	HWRL_API int32_t hwrl_abi_version(void) { return HWRL_ABI_VERSION; }
	HWRL_API int32_t hwrl_obs_layout_version(void) { return RL::ObsLayoutVersion; }
	HWRL_API int32_t hwrl_obs_dim(void) { return RL::ObsDim; }
	HWRL_API int32_t hwrl_num_actions(void) { return RL::NumActions; }
	HWRL_API int32_t hwrl_history_tokens(void) { return RL::HistoryTokens; }
	HWRL_API int32_t hwrl_token_fields(void) { return RL::TokenFields; }
	HWRL_API int32_t hwrl_aux_classes(void) { return RL::NumAnswerClasses; }

	HWRL_API void hwrl_token_vocab(int32_t* out_fields)
	{
		if (out_fields == nullptr) { return; }
		for (int32_t F = 0; F < RL::TokenFields; ++F) { out_fields[F] = RL::TokenVocab[F]; }
	}

	HWRL_API const char* hwrl_obs_feature_name(int32_t i) { return RL::ObsFeatureName(i); }
	HWRL_API const char* hwrl_action_name(int32_t a) { return RL::ActionName(a); }
	HWRL_API const char* hwrl_answer_name(int32_t c)
	{
		return (c >= 0 && c < RL::NumAnswerClasses) ? SymName(static_cast<ESym>(c)) : "?";
	}

	// ---- the environment ---------------------------------------------------------------------------------------

	HWRL_API void* hwrl_batch_create(int32_t num_envs, uint64_t seed, const HWRLEnvConfig* cfg)
	{
		HWRLEnvConfig C{ 180, 66.f, 450.f, 800.f, 0, 0, 1.f, 0 };
		if (cfg != nullptr) { C = *cfg; }
		return new FRLEnvBatch(num_envs, seed, C);
	}

	HWRL_API void hwrl_batch_destroy(void* batch) { delete static_cast<FRLEnvBatch*>(batch); }
	HWRL_API int32_t hwrl_batch_size(void* batch) { return batch != nullptr ? static_cast<FRLEnvBatch*>(batch)->Size() : 0; }

	HWRL_API void hwrl_batch_set_next_player(void* batch, int32_t env, const HWRLPlayerSpec* spec)
	{
		if (batch != nullptr && spec != nullptr) { static_cast<FRLEnvBatch*>(batch)->SetNextPlayer(env, *spec); }
	}

	HWRL_API void hwrl_batch_reset(void* batch)
	{
		if (batch != nullptr) { static_cast<FRLEnvBatch*>(batch)->Reset(); }
	}

	HWRL_API void hwrl_batch_observe(void* batch, float* obs, int8_t* tokens, uint8_t* mask)
	{
		if (batch != nullptr && obs != nullptr && tokens != nullptr && mask != nullptr) { static_cast<FRLEnvBatch*>(batch)->Observe(obs, tokens, mask); }
	}

	HWRL_API void hwrl_batch_step(void* batch, const int32_t* actions, const int8_t* aux_top, const float* aux_top_p, HWRLStepOut* out)
	{
		if (batch == nullptr || actions == nullptr || out == nullptr) { return; }
		static_cast<FRLEnvBatch*>(batch)->Step(actions, aux_top, aux_top_p, *out);
	}

	HWRL_API int32_t hwrl_batch_add_player_policy(void* batch, void* policy)
	{
		if (batch == nullptr || policy == nullptr) { return -1; }
		return static_cast<FRLEnvBatch*>(batch)->AddPlayerPolicy(static_cast<const FRLPolicy*>(policy));
	}

	// ---- the C++ forward pass -----------------------------------------------------------------------------------

	HWRL_API void* hwrl_policy_load(const char* path, char* err, int32_t err_len)
	{
		std::unique_ptr<FRLPolicy> P = std::make_unique<FRLPolicy>();
		if (!P->LoadFromFile(path))
		{
			if (err != nullptr && err_len > 0) { std::snprintf(err, static_cast<size_t>(err_len), "%s", P->GetError().c_str()); }
			return nullptr;
		}
		if (err != nullptr && err_len > 0) { err[0] = '\0'; }
		return P.release();
	}

	HWRL_API void hwrl_policy_free(void* policy) { delete static_cast<FRLPolicy*>(policy); }
	HWRL_API int32_t hwrl_policy_hidden(void* policy) { return policy != nullptr ? static_cast<FRLPolicy*>(policy)->HiddenSize() : 0; }

	HWRL_API void hwrl_policy_dims(void* policy, int32_t* out8)
	{
		if (out8 == nullptr) { return; }
		for (int32_t I = 0; I < 8; ++I) { out8[I] = 0; }
		const FRLPolicy* P = static_cast<const FRLPolicy*>(policy);
		if (P == nullptr || !P->IsLoaded()) { return; }
		out8[0] = P->ObsDim(); out8[1] = P->NumActions(); out8[2] = P->AuxClasses(); out8[3] = P->HiddenSize();
		out8[4] = P->HistoryTokens(); out8[5] = P->TokenFields(); out8[6] = P->Side(); out8[7] = P->IsRecurrent() ? 1 : 0;
	}

	HWRL_API void hwrl_policy_forward(void* policy, int32_t n, const float* obs, const int8_t* tokens, const uint8_t* mask,
		const float* h_in, float* h_out, float* logits, float* value, int32_t* argmax)
	{
		const FRLPolicy* P = static_cast<const FRLPolicy*>(policy);
		if (P == nullptr || !P->IsLoaded() || obs == nullptr) { return; }
		// Every stride is the policy's own (review finding: a player policy is not the boss's shape).
		const int32_t O = P->ObsDim();
		const int32_t A = P->NumActions();
		const int32_t H = P->HiddenSize();
		const int32_t TF = P->HistoryTokens() * P->TokenFields();
		std::vector<float> Scratch(static_cast<size_t>(P->ScratchSize()), 0.f);
		FRLPolicyOutput Out;
		for (int32_t I = 0; I < n; ++I)
		{
			const size_t K = static_cast<size_t>(I);
			P->Forward(obs + K * O, (tokens != nullptr && TF > 0) ? tokens + K * TF : nullptr, mask != nullptr ? mask + K * A : nullptr,
				h_in != nullptr ? h_in + K * H : nullptr, h_out != nullptr ? h_out + K * H : nullptr, Out, Scratch.data());
			if (logits != nullptr) { std::memcpy(logits + K * A, Out.Logits, sizeof(float) * static_cast<size_t>(A)); }
			if (value != nullptr) { value[K] = Out.Value; }
			if (argmax != nullptr) { argmax[K] = Out.Argmax; }
		}
	}

	HWRL_API void hwrl_policy_aux(void* policy, int32_t n, const float* h, const int32_t* actions, float* probs)
	{
		const FRLPolicy* P = static_cast<const FRLPolicy*>(policy);
		if (P == nullptr || !P->IsLoaded() || h == nullptr || actions == nullptr || probs == nullptr) { return; }
		const int32_t H = P->HiddenSize();
		const int32_t C = P->AuxClasses();
		for (int32_t I = 0; I < n; ++I)
		{
			const size_t K = static_cast<size_t>(I);
			P->Aux(h + K * H, actions[K], probs + K * C);
		}
	}

	// ---- evaluation ---------------------------------------------------------------------------------------------

	HWRL_API int32_t hwrl_eval_sessions(int32_t arm, void* policy, const HWRLPlayerSpec* spec, int32_t sessions, uint64_t seed,
		int32_t immortal, int32_t fight_seconds, int32_t script_index, float temperature, HWRLEvalStats* out)
	{
		if (spec == nullptr || out == nullptr || arm < 0 || arm > 2) { return 0; }
		if (arm == 2 && (policy == nullptr || !static_cast<const FRLPolicy*>(policy)->IsBossPlayable())) { return -2; }
		*out = EvalSessions(arm, static_cast<const FRLPolicy*>(policy), *spec, sessions, seed, immortal != 0, fight_seconds, script_index, temperature);
		return 1;
	}

	HWRL_API int32_t hwrl_eval_sessions_gap(int32_t arm, void* policy, const HWRLPlayerSpec* spec, int32_t sessions, uint64_t seed,
		int32_t immortal, int32_t fight_seconds, int32_t script_index, float temperature, int32_t min_swing_gap, HWRLEvalStats* out,
		HWRLBotDiag* bot_diag)
	{
		if (spec == nullptr || out == nullptr || arm < 0 || arm > 2) { return 0; }
		if (arm == 2 && (policy == nullptr || !static_cast<const FRLPolicy*>(policy)->IsBossPlayable())) { return -2; }
		FBotDiag Diag;
		*out = EvalSessions(arm, static_cast<const FRLPolicy*>(policy), *spec, sessions, seed, immortal != 0, fight_seconds, script_index,
			temperature, min_swing_gap, bot_diag != nullptr ? &Diag : nullptr);
		if (bot_diag != nullptr)
		{
			*bot_diag = HWRLBotDiag{};
			bot_diag->frames = Diag.Frames; bot_diag->actionable = Diag.Actionable; bot_diag->in_range = Diag.InRange;
			bot_diag->in_range_actionable = Diag.InRangeActionable; bot_diag->boss_open = Diag.BossOpen;
			bot_diag->boss_open_in_range = Diag.BossOpenInRange; bot_diag->boss_swinging = Diag.BossSwinging;
			bot_diag->defence_pending = Diag.DefencePending; bot_diag->punish_starts = Diag.PunishStarts;
			bot_diag->aggro_starts = Diag.AggroStarts; bot_diag->response_attacks = Diag.ResponseAttacks;
			bot_diag->attack_commits = Diag.AttackCommits; bot_diag->walk_fwd = Diag.WalkFwd; bot_diag->walk_back = Diag.WalkBack;
			bot_diag->guarding = Diag.Guarding; bot_diag->distance_sum = Diag.DistanceSum;
		}
		return 1;
	}

	HWRL_API int32_t hwrl_easy_swing_gap(void) { return HW::RL::EasySwingGap; }

	HWRL_API int32_t hwrl_eval_attack_log(void* policy, const HWRLPlayerSpec* specs, int32_t num_specs, int32_t sessions,
		uint64_t seed, int32_t immortal, int32_t fight_seconds, int32_t num_threads, HWRLAttackRecord* out, int32_t max_records,
		HWRLEvalStats* out_stats)
	{
		if (policy == nullptr) { return -1; }
		return EvalAttackLog(*static_cast<const FRLPolicy*>(policy), specs, num_specs, sessions, seed, immortal != 0, fight_seconds,
			num_threads, out, max_records, out_stats);
	}

	// ---- RL-4: exploiter players --------------------------------------------------------------------------------

	HWRL_API int32_t hwrl_player_obs_dim(void) { return RLPlayer::ObsDim; }
	HWRL_API int32_t hwrl_player_num_actions(void) { return RLPlayer::NumActions; }
	HWRL_API const char* hwrl_player_action_name(int32_t a) { return RLPlayer::ActionName(a); }
	HWRL_API const char* hwrl_player_obs_feature_name(int32_t i) { return RLPlayer::ObsFeatureName(i); }

	HWRL_API void* hwrl_pbatch_create(int32_t num_envs, uint64_t seed, const HWRLEnvConfig* cfg, void* boss_policy)
	{
		const FRLPolicy* Boss = static_cast<const FRLPolicy*>(boss_policy);
		if (Boss == nullptr || !Boss->IsBossPlayable()) { return nullptr; }
		HWRLEnvConfig C{ 180, 66.f, 450.f, 800.f, 0, 0, 0.f, 0 };
		if (cfg != nullptr) { C = *cfg; }
		return new FRLPlayerEnvBatch(num_envs, seed, C, Boss);
	}

	HWRL_API void hwrl_pbatch_destroy(void* pbatch) { delete static_cast<FRLPlayerEnvBatch*>(pbatch); }
	HWRL_API int32_t hwrl_pbatch_size(void* pbatch) { return pbatch != nullptr ? static_cast<FRLPlayerEnvBatch*>(pbatch)->Size() : 0; }

	HWRL_API void hwrl_pbatch_set_next_keeper(void* pbatch, int32_t env, float skill, int32_t identity, int32_t fights_in_session, int32_t tag)
	{
		if (pbatch == nullptr) { return; }
		FRLKeeperSpec S;
		S.Skill = skill;
		S.Identity = identity;
		S.Fights = fights_in_session;
		S.Tag = tag;
		static_cast<FRLPlayerEnvBatch*>(pbatch)->SetNextKeeper(env, S);
	}

	HWRL_API void hwrl_pbatch_reset(void* pbatch)
	{
		if (pbatch != nullptr) { static_cast<FRLPlayerEnvBatch*>(pbatch)->Reset(); }
	}

	HWRL_API void hwrl_pbatch_observe(void* pbatch, float* obs, uint8_t* mask)
	{
		if (pbatch != nullptr && obs != nullptr && mask != nullptr) { static_cast<FRLPlayerEnvBatch*>(pbatch)->Observe(obs, mask); }
	}

	HWRL_API void hwrl_pbatch_step(void* pbatch, const int32_t* actions, HWRLStepOut* out)
	{
		if (pbatch == nullptr || actions == nullptr || out == nullptr) { return; }
		static_cast<FRLPlayerEnvBatch*>(pbatch)->Step(actions, *out);
	}
}
