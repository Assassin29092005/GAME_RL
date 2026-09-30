// HellwalkerRL — tools only. hwrl.dll: the C ABI over the training environment, the C++ forward pass and C++-side
// evaluation (see hwrl_capi.h). Thin wrappers — every behaviour lives in HWRLEnv.cpp and the engine-free core.

#include "hwrl_capi.h"

#include "HWRLEnv.h"
#include "HWCore/HWRLPolicy.h"
#include "HWCore/HWRLTypes.h"

#include <cstdio>
#include <cstring>
#include <memory>

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
		HWRLEnvConfig C{ 180, 66.f, 450.f, 800.f, 0, 0 };
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

	HWRL_API void hwrl_policy_forward(void* policy, int32_t n, const float* obs, const int8_t* tokens, const uint8_t* mask,
		const float* h_in, float* h_out, float* logits, float* value, int32_t* argmax)
	{
		const FRLPolicy* P = static_cast<const FRLPolicy*>(policy);
		if (P == nullptr || !P->IsLoaded() || obs == nullptr) { return; }
		const int32_t O = P->ObsDim();
		const int32_t A = P->NumActions();
		const int32_t H = P->HiddenSize();
		const int32_t TF = RL::HistoryTokens * RL::TokenFields;
		FRLPolicyOutput Out;
		for (int32_t I = 0; I < n; ++I)
		{
			const size_t K = static_cast<size_t>(I);
			P->Forward(obs + K * O, tokens != nullptr ? tokens + K * TF : nullptr, mask != nullptr ? mask + K * A : nullptr,
				h_in != nullptr ? h_in + K * H : nullptr, h_out != nullptr ? h_out + K * H : nullptr, Out);
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
		int32_t immortal, int32_t fight_seconds, int32_t script_index, HWRLEvalStats* out)
	{
		if (spec == nullptr || out == nullptr || arm < 0 || arm > 2) { return 0; }
		if (arm == 2 && policy == nullptr) { return 0; }
		*out = EvalSessions(arm, static_cast<const FRLPolicy*>(policy), *spec, sessions, seed, immortal != 0, fight_seconds, script_index);
		return 1;
	}
}
