// HellwalkerRL — engine-free core. The RL keeper's network: .hwrl loader and forward pass (see HWRLPolicy.h).

#include "HWCore/HWRLPolicy.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace HW
{
	namespace
	{
		struct FReader
		{
			const uint8_t* P = nullptr;
			size_t Left = 0;
			bool bOk = true;

			bool Read(void* Out, size_t N)
			{
				if (!bOk || N > Left) { bOk = false; return false; }
				std::memcpy(Out, P, N);
				P += N;
				Left -= N;
				return true;
			}
			uint32_t U32()
			{
				uint32_t V = 0;
				Read(&V, sizeof(V));
				return V;
			}
		};

		inline float Sigmoid(float X) { return 1.f / (1.f + std::exp(-X)); }

		/** Out[i] = B[i] + sum_j W[i * In + j] * X[j] — fixed accumulation order. */
		inline void Affine(const float* W, const float* B, const float* X, int32_t In, int32_t OutN, float* Out)
		{
			for (int32_t I = 0; I < OutN; ++I)
			{
				const float* Row = W + static_cast<size_t>(I) * static_cast<size_t>(In);
				float S = 0.f;
				for (int32_t J = 0; J < In; ++J) { S += Row[J] * X[J]; }
				Out[I] = S + (B != nullptr ? B[I] : 0.f);
			}
		}
	}

	bool FRLPolicy::Take(const char* Name, std::initializer_list<int32_t> Dims, std::vector<float>& Out)
	{
		for (std::pair<std::string, FTensor>& T : Parsed)
		{
			if (T.first != Name) { continue; }
			if (T.second.Dims.size() != Dims.size())
			{
				Error = std::string("tensor ") + Name + ": wrong rank";
				return false;
			}
			size_t I = 0;
			for (int32_t D : Dims)
			{
				if (T.second.Dims[I++] != D)
				{
					Error = std::string("tensor ") + Name + ": wrong shape";
					return false;
				}
			}
			Out.swap(T.second.Data);
			return true;
		}
		Error = std::string("missing tensor ") + Name;
		return false;
	}

	bool FRLPolicy::LoadFromMemory(const uint8_t* Data, size_t Size)
	{
		bLoaded = false;
		Error.clear();
		Parsed.clear();
		FReader R{ Data, Size, Data != nullptr };
		char Magic[4] = {};
		R.Read(Magic, 4);
		if (!R.bOk || std::memcmp(Magic, "HWRL", 4) != 0) { Error = "not a .hwrl file"; return false; }
		const uint32_t Version = R.U32();
		if (Version != 1u) { Error = "unsupported .hwrl version"; return false; }
		const uint32_t Count = R.U32();
		if (!R.bOk || Count > 4096u) { Error = "corrupt tensor count"; return false; }
		for (uint32_t K = 0; K < Count; ++K)
		{
			const uint32_t NameLen = R.U32();
			if (!R.bOk || NameLen == 0u || NameLen > 256u) { Error = "corrupt tensor name"; return false; }
			std::string Name(NameLen, '\0');
			R.Read(&Name[0], NameLen);
			const uint32_t NDim = R.U32();
			if (!R.bOk || NDim > 4u) { Error = "corrupt tensor rank"; return false; }
			FTensor T;
			size_t Elems = 1;
			for (uint32_t D = 0; D < NDim; ++D)
			{
				const uint32_t Dim = R.U32();
				if (!R.bOk || Dim == 0u || Dim > (1u << 24)) { Error = "corrupt tensor shape"; return false; }
				T.Dims.push_back(static_cast<int32_t>(Dim));
				Elems *= Dim;
			}
			if (Elems > (size_t(1) << 26)) { Error = "tensor too large"; return false; }
			T.Data.resize(Elems);
			R.Read(T.Data.data(), Elems * sizeof(float));
			if (!R.bOk) { Error = "truncated tensor data"; return false; }
			Parsed.emplace_back(std::move(Name), std::move(T));
		}

		auto Meta = [this](const char* Name, int32_t& Out) -> bool
		{
			std::vector<float> V;
			if (!Take(Name, { 1 }, V)) { return false; }
			Out = static_cast<int32_t>(std::lround(V[0]));
			return true;
		};
		int32_t LayoutVersion = 0, Recurrent = 1;
		if (!Meta("meta.obs_layout_version", LayoutVersion) || !Meta("meta.obs_dim", NObs) || !Meta("meta.num_actions", NAct)
			|| !Meta("meta.aux_classes", NAux) || !Meta("meta.enc_hidden", NEnc) || !Meta("meta.embed_dim", NEmbed)
			|| !Meta("meta.hidden", NHidden) || !Meta("meta.recurrent", Recurrent) || !Meta("meta.history_tokens", NTok)
			|| !Meta("meta.token_fields", NFields) || !Meta("meta.side", PolicySide))
		{
			Parsed.clear();
			return false;
		}
		if (LayoutVersion != RL::ObsLayoutVersion)
		{
			char Buf[128];
			std::snprintf(Buf, sizeof(Buf), "trained with observation layout %d, this build uses %d: retrain / re-export", LayoutVersion, RL::ObsLayoutVersion);
			Error = Buf;
			Parsed.clear();
			return false;
		}
		if (NObs <= 0 || NObs > 4096 || NAct <= 0 || NAct > FRLPolicyOutput::MaxActions || NAux <= 0 || NAux > FRLPolicyOutput::MaxAux
			|| NEnc <= 0 || NEmbed <= 0 || NHidden <= 0 || NTok < 0 || NTok > 256 || NFields < 0 || NFields > 16)
		{
			Error = "bad sizes in meta";
			Parsed.clear();
			return false;
		}
		if (PolicySide != 0 && PolicySide != 1)
		{
			Error = "meta.side must be 0 (a boss policy) or 1 (a player policy)";
			Parsed.clear();
			return false;
		}
		if (PolicySide == 0 && NHidden > RL::MaxHidden)
		{
			char Buf[128];
			std::snprintf(Buf, sizeof(Buf), "a boss policy's hidden size %d exceeds the session memory (%d)", NHidden, RL::MaxHidden);
			Error = Buf;
			Parsed.clear();
			return false;
		}
		if (PolicySide == 0 && (NObs != RL::ObsDim || NAct != RL::NumActions || NAux != RL::NumAnswerClasses
			|| NTok != RL::HistoryTokens || NFields != RL::TokenFields))
		{
			Error = "a boss policy must match HWRLTypes.h (obs / actions / answers / tokens)";
			Parsed.clear();
			return false;
		}
		bRecurrent = Recurrent != 0;

		const int32_t NZ = NEnc + NEmbed;
		bool bOk = Take("enc1.weight", { NEnc, NObs }, Enc1W) && Take("enc1.bias", { NEnc }, Enc1B)
			&& Take("enc2.weight", { NEnc, NEnc }, Enc2W) && Take("enc2.bias", { NEnc }, Enc2B)
			&& Take("tok.age", { NTok > 0 ? NTok : 1, NEmbed }, Age)
			&& Take("pi.weight", { NAct, NHidden }, PiW) && Take("pi.bias", { NAct }, PiB)
			&& Take("v.weight", { 1, NHidden }, VW) && Take("v.bias", { 1 }, VB)
			&& Take("aux.weight", { NAux, NHidden }, AuxW) && Take("aux.action", { NAux, NAct }, AuxA) && Take("aux.bias", { NAux }, AuxB);
		if (bOk && bRecurrent)
		{
			bOk = Take("gru.weight_ih", { 3 * NHidden, NZ }, GruWih) && Take("gru.weight_hh", { 3 * NHidden, NHidden }, GruWhh)
				&& Take("gru.bias_ih", { 3 * NHidden }, GruBih) && Take("gru.bias_hh", { 3 * NHidden }, GruBhh);
		}
		else if (bOk)
		{
			bOk = Take("ff.weight", { NHidden, NZ }, FfW) && Take("ff.bias", { NHidden }, FfB);
		}
		Emb.assign(static_cast<size_t>(NFields), std::vector<float>());
		Vocab.assign(static_cast<size_t>(NFields), 0);
		for (int32_t F = 0; bOk && F < NFields; ++F)
		{
			char Name[32];
			std::snprintf(Name, sizeof(Name), "tok.emb%d", F);
			for (std::pair<std::string, FTensor>& T : Parsed)
			{
				if (T.first == Name && T.second.Dims.size() == 2) { Vocab[static_cast<size_t>(F)] = T.second.Dims[0]; break; }
			}
			bOk = Vocab[static_cast<size_t>(F)] > 0 && Take(Name, { Vocab[static_cast<size_t>(F)], NEmbed }, Emb[static_cast<size_t>(F)]);
			if (!bOk && Error.empty()) { Error = std::string("missing tensor ") + Name; }
		}
		Parsed.clear();
		if (!bOk) { return false; }

		Scratch.assign(static_cast<size_t>(2 * NEnc + 2 * NEmbed + NZ + 8 * NHidden + NAux), 0.f);
		bLoaded = true;
		return true;
	}

	bool FRLPolicy::LoadFromFile(const char* Path)
	{
		FILE* F = nullptr;
		if (fopen_s(&F, Path, "rb") != 0 || F == nullptr)
		{
			Error = std::string("cannot open ") + (Path != nullptr ? Path : "(null)");
			bLoaded = false;
			return false;
		}
		std::vector<uint8_t> Bytes;
		uint8_t Buf[65536];
		size_t N = 0;
		while ((N = std::fread(Buf, 1, sizeof(Buf), F)) > 0) { Bytes.insert(Bytes.end(), Buf, Buf + N); }
		std::fclose(F);
		return LoadFromMemory(Bytes.data(), Bytes.size());
	}

	void FRLPolicy::Forward(const float* Obs, const int8_t* Tokens, const uint8_t* Mask, const float* HiddenIn, float* HiddenOut,
		FRLPolicyOutput& Out) const
	{
		Forward(Obs, Tokens, Mask, HiddenIn, HiddenOut, Out, Scratch.data());
	}

	void FRLPolicy::Forward(const float* Obs, const int8_t* Tokens, const uint8_t* Mask, const float* HiddenIn, float* HiddenOut,
		FRLPolicyOutput& Out, float* ExternalScratch) const
	{
		Out = FRLPolicyOutput{};
		Out.NumActions = NAct;
		if (!bLoaded || ExternalScratch == nullptr) { return; }

		const int32_t NZ = NEnc + NEmbed;
		float* E1 = ExternalScratch;
		float* E2 = E1 + NEnc;
		float* Tok = E2 + NEnc;
		float* Pool = Tok + NEmbed;
		float* Z = Pool + NEmbed;
		float* Gi = Z + NZ;              // 3H
		float* Gh = Gi + 3 * NHidden;    // 3H
		float* H1 = Gh + 3 * NHidden;    // H
		float* H0 = H1 + NHidden;        // H (copy of the input state: HiddenIn may alias HiddenOut)

		Affine(Enc1W.data(), Enc1B.data(), Obs, NObs, NEnc, E1);
		for (int32_t I = 0; I < NEnc; ++I) { E1[I] = std::tanh(E1[I]); }
		Affine(Enc2W.data(), Enc2B.data(), E1, NEnc, NEnc, E2);
		for (int32_t I = 0; I < NEnc; ++I) { E2[I] = std::tanh(E2[I]); }

		for (int32_t D = 0; D < NEmbed; ++D) { Pool[D] = 0.f; }
		int32_t Valid = 0;
		for (int32_t K = 0; Tokens != nullptr && K < NTok; ++K)
		{
			const int8_t* T = Tokens + static_cast<size_t>(K) * static_cast<size_t>(NFields);
			bool bAny = false;
			for (int32_t F = 0; F < NFields; ++F) { bAny = bAny || T[F] != 0; }
			if (!bAny) { continue; }
			const float* A = Age.data() + static_cast<size_t>(K) * static_cast<size_t>(NEmbed);
			for (int32_t D = 0; D < NEmbed; ++D) { Tok[D] = A[D]; }
			for (int32_t F = 0; F < NFields; ++F)
			{
				int32_t V = T[F];
				if (V < 0 || V >= Vocab[static_cast<size_t>(F)]) { V = 0; }
				const float* E = Emb[static_cast<size_t>(F)].data() + static_cast<size_t>(V) * static_cast<size_t>(NEmbed);
				for (int32_t D = 0; D < NEmbed; ++D) { Tok[D] += E[D]; }
			}
			for (int32_t D = 0; D < NEmbed; ++D) { Pool[D] += Tok[D] > 0.f ? Tok[D] : 0.f; }
			++Valid;
		}
		if (Valid > 0)
		{
			const float Inv = 1.f / static_cast<float>(Valid);
			for (int32_t D = 0; D < NEmbed; ++D) { Pool[D] *= Inv; }
		}

		for (int32_t I = 0; I < NEnc; ++I) { Z[I] = E2[I]; }
		for (int32_t D = 0; D < NEmbed; ++D) { Z[NEnc + D] = Pool[D]; }

		for (int32_t I = 0; I < NHidden; ++I) { H0[I] = HiddenIn != nullptr ? HiddenIn[I] : 0.f; }
		if (bRecurrent)
		{
			Affine(GruWih.data(), GruBih.data(), Z, NZ, 3 * NHidden, Gi);
			Affine(GruWhh.data(), GruBhh.data(), H0, NHidden, 3 * NHidden, Gh);
			for (int32_t I = 0; I < NHidden; ++I)
			{
				const float Rg = Sigmoid(Gi[I] + Gh[I]);
				const float Zg = Sigmoid(Gi[NHidden + I] + Gh[NHidden + I]);
				const float Ng = std::tanh(Gi[2 * NHidden + I] + Rg * Gh[2 * NHidden + I]);
				H1[I] = (1.f - Zg) * Ng + Zg * H0[I];
			}
		}
		else
		{
			Affine(FfW.data(), FfB.data(), Z, NZ, NHidden, H1);
			for (int32_t I = 0; I < NHidden; ++I) { H1[I] = std::tanh(H1[I]); }
		}
		if (HiddenOut != nullptr) { for (int32_t I = 0; I < NHidden; ++I) { HiddenOut[I] = H1[I]; } }

		Affine(PiW.data(), PiB.data(), H1, NHidden, NAct, Out.Logits);
		float Best = -1.0e30f;
		for (int32_t A = 0; A < NAct; ++A)
		{
			if (Mask != nullptr && Mask[A] == 0) { Out.Logits[A] = -1.0e30f; continue; }
			if (Out.Argmax < 0 || Out.Logits[A] > Best) { Best = Out.Logits[A]; Out.Argmax = A; }
		}
		float Sum = 0.f;
		for (int32_t A = 0; A < NAct; ++A)
		{
			const bool bAllowed = Mask == nullptr || Mask[A] != 0;
			Out.Probs[A] = bAllowed ? std::exp(Out.Logits[A] - Best) : 0.f;
			Sum += Out.Probs[A];
		}
		if (Sum > 0.f) { for (int32_t A = 0; A < NAct; ++A) { Out.Probs[A] /= Sum; } }

		float V = VB[0];
		for (int32_t I = 0; I < NHidden; ++I) { V += VW[static_cast<size_t>(I)] * H1[I]; }
		Out.Value = V;
	}

	void FRLPolicy::Aux(const float* Hidden, int32_t Action, float* OutProbs) const
	{
		if (!bLoaded || OutProbs == nullptr) { return; }
		float Logits[FRLPolicyOutput::MaxAux];
		float Best = -1.0e30f;
		const bool bAction = Action >= 0 && Action < NAct;
		for (int32_t C = 0; C < NAux; ++C)
		{
			const float* Row = AuxW.data() + static_cast<size_t>(C) * static_cast<size_t>(NHidden);
			float S = 0.f;
			for (int32_t I = 0; I < NHidden; ++I) { S += Row[I] * Hidden[I]; }
			S += AuxB[static_cast<size_t>(C)];
			if (bAction) { S += AuxA[static_cast<size_t>(C) * static_cast<size_t>(NAct) + static_cast<size_t>(Action)]; }
			Logits[C] = S;
			if (S > Best) { Best = S; }
		}
		float Sum = 0.f;
		for (int32_t C = 0; C < NAux; ++C) { OutProbs[C] = std::exp(Logits[C] - Best); Sum += OutProbs[C]; }
		for (int32_t C = 0; C < NAux; ++C) { OutProbs[C] /= Sum; }
	}
}
