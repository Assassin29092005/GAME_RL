// HellwalkerRL — engine-free core. The RL keeper's network: weights loaded from a .hwrl file, and a plain C++ forward
// pass (RL.md §5.1). The trainer (RL/model.py) defines the same network in PyTorch; RL/export.py writes this format and
// a parity test checks both give the same outputs. One small float-only routine, no allocation per call, no threads,
// no randomness: the same weights and inputs give the same action in the simulator (B0 checks) and in the game.
//
// Network (all sizes read from the file):
//   e1 = tanh(W_enc1 · obs + b_enc1)                                 [EncHidden]
//   e2 = tanh(W_enc2 · e1 + b_enc2)                                  [EncHidden]
//   for each token k (newest first) whose fields are not all 0:
//       t_k = relu( sum_f Emb_f[token_k[f]] + Age[k] )               [EmbedDim]
//   pool = mean_k t_k  (zeros if no token)                          [EmbedDim]
//   z = concat(e2, pool)                                            [EncHidden + EmbedDim]
//   recurrent: h' = GRUCell(z, h)   (PyTorch gate order r, z, n)     [Hidden]
//   feed-forward (RL-1): h' = tanh(W_ff · z + b_ff), h ignored
//   logits = W_pi · h' + b_pi, masked actions -> -inf                [NumActions]
//   value  = W_v · h' + b_v                                          [1]
//   aux(a) = W_aux · h' + W_auxa[:, a] + b_aux                        [NumAnswerClasses]  (the read head, conditioned
//                                                                     on the action being taken)
//
// File format (little-endian): "HWRL", u32 version (=1), then a u32 count of named tensors, each:
//   u32 name length, name bytes (ASCII, no terminator), u32 ndim, u32 dims[ndim], f32 data[prod(dims)] (row-major)
// Scalar metadata is stored as 1-element tensors named "meta.*":
//   meta.obs_layout_version, meta.obs_dim, meta.num_actions, meta.aux_classes, meta.enc_hidden, meta.embed_dim,
//   meta.hidden, meta.recurrent (1/0), meta.history_tokens, meta.token_fields, meta.side (0 boss, 1 player)
// Tensors (PyTorch Linear layout [out, in]):
//   enc1.weight [EncHidden, ObsDim]       enc1.bias [EncHidden]
//   enc2.weight [EncHidden, EncHidden]    enc2.bias [EncHidden]
//   tok.emb{f}  [TokenVocab[f], EmbedDim] for f in 0..TokenFields-1
//   tok.age     [HistoryTokens, EmbedDim]
//   gru.weight_ih [3*Hidden, EncHidden+EmbedDim]  gru.weight_hh [3*Hidden, Hidden]  gru.bias_ih [3*Hidden]  gru.bias_hh [3*Hidden]
//     (or, feed-forward: ff.weight [Hidden, EncHidden+EmbedDim], ff.bias [Hidden])
//   pi.weight [NumActions, Hidden]  pi.bias [NumActions]
//   v.weight  [1, Hidden]           v.bias  [1]
//   aux.weight [Aux, Hidden]  aux.action [Aux, NumActions]  aux.bias [Aux]

#pragma once

#include "HWRLTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace HW
{
	struct FRLPolicyOutput
	{
		static constexpr int32_t MaxActions = 32;
		static constexpr int32_t MaxAux = 16;
		float Logits[MaxActions] = {};   // masked entries are -1e30
		float Probs[MaxActions] = {};    // softmax over the allowed actions
		float Value = 0.f;
		int32_t Argmax = -1;             // the greedy action (lowest index wins a tie)
		int32_t NumActions = 0;
	};

	class FRLPolicy
	{
	public:
		/** Parse a .hwrl image. On failure returns false and sets Error; the policy stays unloaded. */
		bool LoadFromMemory(const uint8_t* Data, size_t Size);
		/** Convenience for tools (fopen). Unreal loads the bytes itself (FFileHelper) and calls LoadFromMemory. */
		bool LoadFromFile(const char* Path);
		bool IsLoaded() const { return bLoaded; }
		const std::string& GetError() const { return Error; }

		int32_t ObsDim() const { return NObs; }
		int32_t NumActions() const { return NAct; }
		int32_t AuxClasses() const { return NAux; }
		int32_t HiddenSize() const { return NHidden; }
		bool IsRecurrent() const { return bRecurrent; }
		/** 0 = a boss policy (RL::ObsDim / NumActions), 1 = a player policy (exploiter, RL/native). */
		int32_t Side() const { return PolicySide; }
		int32_t HistoryTokens() const { return NTok; }
		int32_t TokenFields() const { return NFields; }
		/** A policy the keeper brain can run: a loaded boss policy of this build's layout whose memory fits a session. */
		bool IsBossPlayable() const
		{
			return bLoaded && PolicySide == 0 && NObs == RL::ObsDim && NAct == RL::NumActions && NAux == RL::NumAnswerClasses
				&& NTok == RL::HistoryTokens && NFields == RL::TokenFields && NHidden > 0 && NHidden <= RL::MaxHidden;
		}

		/**
		 * One decision. Obs: ObsDim floats. Tokens: HistoryTokens x TokenFields int8. Mask: NumActions bytes (null = all
		 * allowed). HiddenIn / HiddenOut: HiddenSize floats (may alias; HiddenIn null = zeros). This overload uses the
		 * policy's own scratch, so it is not thread-safe per instance — threads sharing one policy pass their own
		 * scratch (ScratchSize() floats) to the overload below.
		 */
		void Forward(const float* Obs, const int8_t* Tokens, const uint8_t* Mask, const float* HiddenIn, float* HiddenOut,
			FRLPolicyOutput& Out) const;
		/** The same, with caller-owned scratch of at least ScratchSize() floats: a shared const policy, many threads. */
		void Forward(const float* Obs, const int8_t* Tokens, const uint8_t* Mask, const float* HiddenIn, float* HiddenOut,
			FRLPolicyOutput& Out, float* ExternalScratch) const;
		int32_t ScratchSize() const { return static_cast<int32_t>(Scratch.size()); }
		/** The read head for action A, given the NEW hidden state from Forward. OutProbs: AuxClasses floats (softmax). */
		void Aux(const float* Hidden, int32_t Action, float* OutProbs) const;

	private:
		struct FTensor { std::vector<int32_t> Dims; std::vector<float> Data; };
		bool Take(const char* Name, std::initializer_list<int32_t> Dims, std::vector<float>& Out);

		bool bLoaded = false;
		std::string Error;
		int32_t NObs = 0, NAct = 0, NAux = 0, NEnc = 0, NEmbed = 0, NHidden = 0, NTok = 0, NFields = 0, PolicySide = 0;
		bool bRecurrent = true;
		std::vector<int32_t> Vocab;
		std::vector<float> Enc1W, Enc1B, Enc2W, Enc2B, Age, GruWih, GruWhh, GruBih, GruBhh, FfW, FfB, PiW, PiB, VW, VB, AuxW, AuxA, AuxB;
		std::vector<std::vector<float>> Emb;
		std::vector<std::pair<std::string, FTensor>> Parsed; // during load only
		mutable std::vector<float> Scratch;
	};
}
