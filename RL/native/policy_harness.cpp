// HellwalkerRL — tools only. Policy parity harness: loads a .hwrl with the game's own forward pass (HW::FRLPolicy,
// HWCore/HWRLPolicy.cpp — the very code FRLBrain runs) and evaluates it over a batch written by
// RL/tests/test_parity.py, so the PyTorch network (RL/model.py) and the C++ one can be compared number for number.
// Not part of the game and not part of the training environment (RL/native/build.bat builds that as hwrl.dll).
//
// Usage:
//   policy_harness <policy.hwrl> <in.bin> <out.bin>     evaluate a batch
//   policy_harness <policy.hwrl> --bench [iterations]   time one decision (Forward + Aux), RL.md §8's cost budget
//   policy_harness <policy.hwrl> --info                 print the policy's sizes (exit 1 + the loader's error if bad)
//
// in.bin (little-endian): char[4] "HWPI", then int32 version (1), n, obs_dim, K (tokens), F (fields), A (actions),
//   H (hidden), flags (1 = mask present, 2 = h_in present, 4 = starts present), seq_len;
//   f32 obs[n * obs_dim], i8 tokens[n * K * F], u8 mask[n * A] (flag 1), f32 h_in[rows * H] (flag 2),
//   u8 starts[n] (flag 4), i32 actions[n] (the action the read head is conditioned on; -1 = none).
//   seq_len 0: n independent decisions, h_in per row (rows = n; absent = zeros, i.e. HiddenIn == nullptr).
//   seq_len T > 0: n = T * B rows laid out [T, B]; each column is one session played step by step exactly as
//     FRLBrain::Think does it (HiddenIn aliasing HiddenOut), h_in = the columns' initial states (rows = B), and
//     starts[t, b] zeroes that column's state BEFORE step t (a new session) — the sequence form of model.forward().
// out.bin: char[4] "HWPO", then int32 version (1), n, A, H, C (aux classes); f32 logits[n * A] (masked = -1e30),
//   f32 probs[n * A], f32 value[n], f32 h_out[n * H] (the state AFTER each row), i32 argmax[n], f32 aux[n * C]
//   (the read head's softmax for the row's action, from its new state).

#include "HWCore/HWRLPolicy.h"
#include "HWCore/HWRandom.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
	constexpr int32_t HarnessVersion = 1;

	enum EInFlags : int32_t
	{
		FlagMask = 1,
		FlagHiddenIn = 2,
		FlagStarts = 4,
	};

	struct FHeader
	{
		int32_t Version = 0, N = 0, ObsDim = 0, K = 0, F = 0, A = 0, H = 0, Flags = 0, SeqLen = 0;
	};

	bool ReadFile(const char* Path, std::vector<uint8_t>& Out)
	{
		FILE* Fp = nullptr;
		if (fopen_s(&Fp, Path, "rb") != 0 || Fp == nullptr) { return false; }
		uint8_t Buf[65536];
		size_t Got = 0;
		while ((Got = std::fread(Buf, 1, sizeof(Buf), Fp)) > 0) { Out.insert(Out.end(), Buf, Buf + Got); }
		std::fclose(Fp);
		return true;
	}

	/** Sequential reader over the input image; any overrun latches bOk = false. Copies out (the blocks after the int8
	 * tokens are not 4-byte aligned in the file). */
	struct FCursor
	{
		const uint8_t* P = nullptr;
		size_t Left = 0;
		bool bOk = true;

		template <typename T>
		bool Take(size_t Count, std::vector<T>& Out)
		{
			const size_t Bytes = Count * sizeof(T);
			if (!bOk || Bytes > Left) { bOk = false; return false; }
			Out.resize(Count);
			if (Bytes > 0) { std::memcpy(Out.data(), P, Bytes); }
			P += Bytes;
			Left -= Bytes;
			return true;
		}
	};

	int32_t Info(const HW::FRLPolicy& Policy)
	{
		std::printf("policy: obs %d, actions %d, aux %d, hidden %d, %s, side %d\n", Policy.ObsDim(), Policy.NumActions(),
			Policy.AuxClasses(), Policy.HiddenSize(), Policy.IsRecurrent() ? "recurrent" : "feed-forward", Policy.Side());
		return 0;
	}

	int32_t Bench(const HW::FRLPolicy& Policy, int32_t Iterations)
	{
		// A plausible decision: bounded features, half the history filled, every action legal. HW::FRandom keeps the
		// inputs identical from run to run.
		HW::FRandom Rng(1234);
		const int32_t NTok = HW::RL::HistoryTokens, NF = HW::RL::TokenFields;
		std::vector<float> Obs(static_cast<size_t>(Policy.ObsDim()));
		for (float& X : Obs) { X = Rng.FRandRange(-1.f, 1.f); }
		std::vector<int8_t> Tokens(static_cast<size_t>(NTok * NF), 0);
		for (int32_t K = 0; K < NTok / 2; ++K)
		{
			for (int32_t F = 0; F < NF; ++F)
			{
				Tokens[static_cast<size_t>(K * NF + F)] = static_cast<int8_t>(1 + Rng.RandHelper(HW::RL::TokenVocab[F] - 1));
			}
		}
		std::vector<uint8_t> Mask(static_cast<size_t>(Policy.NumActions()), 1);
		std::vector<float> Hidden(static_cast<size_t>(Policy.HiddenSize()), 0.f);
		float AuxProbs[HW::FRLPolicyOutput::MaxAux] = {};
		HW::FRLPolicyOutput Out;
		double Checksum = 0.0;
		const auto T0 = std::chrono::steady_clock::now();
		for (int32_t I = 0; I < Iterations; ++I)
		{
			Policy.Forward(Obs.data(), Tokens.data(), Mask.data(), Hidden.data(), Hidden.data(), Out);
			Policy.Aux(Hidden.data(), Out.Argmax, AuxProbs);
			Checksum += static_cast<double>(Out.Value) + static_cast<double>(AuxProbs[0]); // keeps the loop observable
		}
		const auto T1 = std::chrono::steady_clock::now();
		const double Us = std::chrono::duration<double, std::micro>(T1 - T0).count() / (Iterations > 0 ? Iterations : 1);
		std::printf("bench: %d decisions, %.2f us per decision (Forward + Aux, single thread), checksum %.6f\n",
			Iterations, Us, Checksum);
		return 0;
	}
}

int main(int Argc, char** Argv)
{
	if (Argc < 3)
	{
		std::fprintf(stderr, "usage: policy_harness <policy.hwrl> <in.bin> <out.bin>\n"
			"       policy_harness <policy.hwrl> --bench [iterations]\n"
			"       policy_harness <policy.hwrl> --info\n");
		return 2;
	}
	HW::FRLPolicy Policy;
	if (!Policy.LoadFromFile(Argv[1]))
	{
		std::fprintf(stderr, "load failed: %s\n", Policy.GetError().c_str());
		return 1;
	}
	if (std::strcmp(Argv[2], "--info") == 0) { return Info(Policy); }
	if (std::strcmp(Argv[2], "--bench") == 0) { return Bench(Policy, Argc > 3 ? std::atoi(Argv[3]) : 20000); }
	if (Argc < 4)
	{
		std::fprintf(stderr, "missing <out.bin>\n");
		return 2;
	}

	std::vector<uint8_t> Image;
	if (!ReadFile(Argv[2], Image))
	{
		std::fprintf(stderr, "cannot read %s\n", Argv[2]);
		return 1;
	}
	FCursor In{ Image.data(), Image.size(), true };
	std::vector<char> Magic;
	std::vector<int32_t> Raw;
	In.Take(4, Magic);
	In.Take(9, Raw);
	if (!In.bOk || std::memcmp(Magic.data(), "HWPI", 4) != 0)
	{
		std::fprintf(stderr, "%s: not a harness input file\n", Argv[2]);
		return 1;
	}
	static_assert(sizeof(FHeader) == 9 * sizeof(int32_t), "FHeader mirrors the 9 header ints");
	FHeader Hd;
	std::memcpy(&Hd, Raw.data(), sizeof(FHeader));
	const int32_t C = Policy.AuxClasses();
	if (Hd.Version != HarnessVersion || Hd.N <= 0 || Hd.ObsDim != Policy.ObsDim() || Hd.A != Policy.NumActions()
		|| Hd.H != Policy.HiddenSize() || Hd.K != HW::RL::HistoryTokens || Hd.F != HW::RL::TokenFields || Hd.SeqLen < 0
		|| (Hd.SeqLen > 0 && Hd.N % Hd.SeqLen != 0) || ((Hd.Flags & FlagStarts) != 0 && Hd.SeqLen == 0))
	{
		std::fprintf(stderr, "input header does not match the policy (n %d obs %d K %d F %d A %d H %d seq %d flags %d)\n",
			Hd.N, Hd.ObsDim, Hd.K, Hd.F, Hd.A, Hd.H, Hd.SeqLen, Hd.Flags);
		return 1;
	}
	const size_t N = static_cast<size_t>(Hd.N);
	const size_t NA = static_cast<size_t>(Hd.A), NH = static_cast<size_t>(Hd.H), NC = static_cast<size_t>(C);
	const size_t TokRow = static_cast<size_t>(Hd.K) * static_cast<size_t>(Hd.F);
	const size_t Cols = Hd.SeqLen > 0 ? N / static_cast<size_t>(Hd.SeqLen) : N;
	std::vector<float> ObsBuf, HiddenInBuf;
	std::vector<int8_t> TokenBuf;
	std::vector<uint8_t> MaskBuf, StartsBuf;
	std::vector<int32_t> ActionBuf;
	In.Take(N * static_cast<size_t>(Hd.ObsDim), ObsBuf);
	In.Take(N * TokRow, TokenBuf);
	if ((Hd.Flags & FlagMask) != 0) { In.Take(N * NA, MaskBuf); }
	if ((Hd.Flags & FlagHiddenIn) != 0) { In.Take(Cols * NH, HiddenInBuf); }
	if ((Hd.Flags & FlagStarts) != 0) { In.Take(N, StartsBuf); }
	In.Take(N, ActionBuf);
	if (!In.bOk || In.Left != 0)
	{
		std::fprintf(stderr, "%s: size does not match its header\n", Argv[2]);
		return 1;
	}
	// Absent blocks stay null: the forward pass's own "no mask" / "zero state" paths are what gets tested then.
	const float* Obs = ObsBuf.data();
	const int8_t* Tokens = TokenBuf.data();
	const uint8_t* Mask = (Hd.Flags & FlagMask) != 0 ? MaskBuf.data() : nullptr;
	const float* HiddenIn = (Hd.Flags & FlagHiddenIn) != 0 ? HiddenInBuf.data() : nullptr;
	const uint8_t* Starts = (Hd.Flags & FlagStarts) != 0 ? StartsBuf.data() : nullptr;
	const int32_t* Actions = ActionBuf.data();

	std::vector<float> Logits(N * NA), Probs(N * NA), Value(N), HiddenOut(N * NH), Aux(N * NC);
	std::vector<int32_t> Argmax(N);
	HW::FRLPolicyOutput Out;
	auto Emit = [&](size_t Row, const float* NewHidden)
	{
		for (size_t J = 0; J < NA; ++J)
		{
			Logits[Row * NA + J] = Out.Logits[J];
			Probs[Row * NA + J] = Out.Probs[J];
		}
		Value[Row] = Out.Value;
		Argmax[Row] = Out.Argmax;
		std::memcpy(&HiddenOut[Row * NH], NewHidden, NH * sizeof(float));
		Policy.Aux(NewHidden, Actions[Row], &Aux[Row * NC]);
	};

	if (Hd.SeqLen == 0)
	{
		for (size_t Row = 0; Row < N; ++Row)
		{
			Policy.Forward(Obs + Row * static_cast<size_t>(Hd.ObsDim), Tokens + Row * TokRow, Mask != nullptr ? Mask + Row * NA : nullptr,
				HiddenIn != nullptr ? HiddenIn + Row * NH : nullptr, &HiddenOut[Row * NH], Out);
			Emit(Row, &HiddenOut[Row * NH]);
		}
	}
	else
	{
		// One state per column, updated in place (HiddenIn == HiddenOut), as FRLBrain carries Session->Hidden.
		std::vector<float> State(Cols * NH, 0.f);
		if (HiddenIn != nullptr) { std::memcpy(State.data(), HiddenIn, Cols * NH * sizeof(float)); }
		for (size_t T = 0; T < static_cast<size_t>(Hd.SeqLen); ++T)
		{
			for (size_t B = 0; B < Cols; ++B)
			{
				const size_t Row = T * Cols + B;
				float* S = &State[B * NH];
				if (Starts != nullptr && Starts[Row] != 0) { std::memset(S, 0, NH * sizeof(float)); }
				Policy.Forward(Obs + Row * static_cast<size_t>(Hd.ObsDim), Tokens + Row * TokRow, Mask != nullptr ? Mask + Row * NA : nullptr,
					S, S, Out);
				Emit(Row, S);
			}
		}
	}

	FILE* Fp = nullptr;
	if (fopen_s(&Fp, Argv[3], "wb") != 0 || Fp == nullptr)
	{
		std::fprintf(stderr, "cannot write %s\n", Argv[3]);
		return 1;
	}
	const int32_t OutHeader[5] = { HarnessVersion, Hd.N, Hd.A, Hd.H, C };
	bool bWrote = std::fwrite("HWPO", 1, 4, Fp) == 4;
	bWrote = bWrote && std::fwrite(OutHeader, sizeof(int32_t), 5, Fp) == 5;
	bWrote = bWrote && std::fwrite(Logits.data(), sizeof(float), Logits.size(), Fp) == Logits.size();
	bWrote = bWrote && std::fwrite(Probs.data(), sizeof(float), Probs.size(), Fp) == Probs.size();
	bWrote = bWrote && std::fwrite(Value.data(), sizeof(float), Value.size(), Fp) == Value.size();
	bWrote = bWrote && std::fwrite(HiddenOut.data(), sizeof(float), HiddenOut.size(), Fp) == HiddenOut.size();
	bWrote = bWrote && std::fwrite(Argmax.data(), sizeof(int32_t), Argmax.size(), Fp) == Argmax.size();
	bWrote = bWrote && std::fwrite(Aux.data(), sizeof(float), Aux.size(), Fp) == Aux.size();
	bWrote = (std::fclose(Fp) == 0) && bWrote;
	if (!bWrote)
	{
		std::fprintf(stderr, "short write to %s\n", Argv[3]);
		return 1;
	}
	std::printf("harness: %d rows (%s), hidden %d, %s\n", Hd.N, Hd.SeqLen > 0 ? "sequence" : "independent",
		Hd.H, Policy.IsRecurrent() ? "recurrent" : "feed-forward");
	return 0;
}
