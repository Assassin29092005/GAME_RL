// HellwalkerRL — tools only. Simulator arms.

#include "SimArms.h"

#include "Classic/HWClassicTests.h"
#include "HWCore/HWRLBrain.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace HW
{
	namespace
	{
		class FClassicArm : public FSimArm
		{
		public:
			FClassicArm(int32_t InScript, bool bDerivedPayoffs) : ScriptIndex(InScript) { Cfg.bDerivedPayoffs = bDerivedPayoffs; }
			const char* Label() const override { return "Hellwalker (classic)"; }
			void NewSession() override { Model = std::make_unique<FPlaystyleModel>(); }
			FEncounterStats Run(const FRunConfig& Run, std::vector<FExchangeRow>* Rows) override
			{
				if (!Model) { NewSession(); }
				return RunClassic(*Model, EBrainMode::Hellwalker, Run, Rows, &Cfg, ScriptIndex);
			}
			void SetReferenceSwingsPerMin(float Spm) override { Cfg.RefSwingsPerMin = Spm; }

		private:
			int32_t ScriptIndex = 0;
			FBrainConfig Cfg;
			std::unique_ptr<FPlaystyleModel> Model;
		};

		/** The RL keeper: a frozen policy + a session memory that is reset per session (RL.md §3.2). */
		class FRLArm : public FSimArm
		{
		public:
			bool Load(const char* Path, float Skill, int32_t Identity)
			{
				if (Path == nullptr) { std::printf("--brain rl needs --policy <file.hwrl>\n"); return false; }
				if (!Policy.LoadFromFile(Path)) { std::printf("cannot load %s: %s\n", Path, Policy.GetError().c_str()); return false; }
				if (!Policy.IsBossPlayable()) { std::printf("%s is not a playable boss policy (side / layout / hidden size)\n", Path); return false; }
				Brain.Configure(Skill, Identity);
				std::snprintf(LabelBuf, sizeof(LabelBuf), "Hellwalker (RL, %s, skill %.2f)", RL::KeeperName(Identity), Skill);
				Session.Reset();
				return true;
			}
			const char* Label() const override { return LabelBuf; }
			void NewSession() override { Session.Reset(); }
			FEncounterStats Run(const FRunConfig& Run, std::vector<FExchangeRow>* Rows) override
			{
				Brain.Bind(&Policy, &Session);
				FRunConfig Cfg = Run;
				Cfg.BossHealthScale = RL::KeeperHealthScale(Brain.Observer().Config.Identity); // as in training (mortal runs)
				return RunEncounter(Brain, Cfg, Rows);
			}

		private:
			FRLPolicy Policy;
			FRLSession Session;
			FRLBrain Brain;
			char LabelBuf[64] = "Hellwalker (RL)";
		};
	}

	std::unique_ptr<FSimArm> MakeAdaptiveArm(const char* Name, const char* PolicyPath, int32_t ScriptIndex, bool bDerivedPayoffs,
		float KeeperSkill, int32_t KeeperIdentity)
	{
		if (std::strcmp(Name, "classic") == 0) { return std::make_unique<FClassicArm>(ScriptIndex, bDerivedPayoffs); }
		if (std::strcmp(Name, "rl") == 0)
		{
			// The keeper follows the script by default: script 0 is the Warden's, script 1 the Sage's (the Returned
			// Warden plays script 0 in Pathbreaker; ask for it with --identity 2).
			const int32_t Identity = KeeperIdentity >= 0 ? KeeperIdentity : (ScriptIndex == 1 ? 1 : 0);
			std::unique_ptr<FRLArm> Arm = std::make_unique<FRLArm>();
			if (!Arm->Load(PolicyPath, KeeperSkill, Identity)) { return nullptr; }
			return Arm;
		}
		std::printf("unknown --brain %s (classic | rl)\n", Name);
		return nullptr;
	}

	int32_t RunExtraTests(int32_t& InOutFailed)
	{
		const FCoreTest* Tests = nullptr;
		const int32_t N = GetRLTests(Tests);
		for (int32_t I = 0; I < N; ++I)
		{
			std::string Log;
			ResetMoveTable();
			const bool bOk = Tests[I].Fn(Log);
			std::printf("[%s] %-32s (%s)\n", bOk ? "PASS" : "FAIL", Tests[I].Name, Tests[I].Milestone);
			std::string Indented;
			for (char Ch : Log) { Indented += Ch; if (Ch == '\n') { Indented += "      "; } }
			std::printf("      %s\n", Indented.c_str());
			if (!bOk) { ++InOutFailed; }
		}
		return N;
	}
}
