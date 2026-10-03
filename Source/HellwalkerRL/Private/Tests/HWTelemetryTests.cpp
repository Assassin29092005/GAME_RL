// HellwalkerRL — the research telemetry's pure parts (no network): web/CONTRACT.md is what they must produce.
//
//   Project.HellwalkerRL.Telemetry.Answers       player symbols -> the contract's answer columns
//   Project.HellwalkerRL.Telemetry.FightCommit   one documents:commit: the new fight + the player's increments, exactly the contract's fields
//   Project.HellwalkerRL.Telemetry.PlayerCreate  the one-time player document: nickname, zero totals, server-time createdAt
//   Project.HellwalkerRL.Telemetry.Nickname      "<Adjective> <Noun> <4 digits>", at most 24 characters
//   Project.HellwalkerRL.Telemetry.NotebookDelta per-fight changes of the session's notebook; a reset notebook starts from zero

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HWTelemetry.h"
#include "Dom/JsonObject.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr EAutomationTestFlags HWTelemetryTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> O;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(R, O) ? O : nullptr;
	}

	FHWFightRecord SampleRecord()
	{
		FHWFightRecord R;
		R.Player = TEXT("uid123");
		R.ClientTime = FDateTime(2026, 10, 3, 12, 0, 0);
		R.Session = TEXT("0123456789abcdef0123456789abcdef");
		R.FightInSession = 2;
		R.GameVersion = TEXT("1.3.0");
		R.Mode = TEXT("openworld");
		R.PlayMode = TEXT("hellwalker");
		R.Brain = TEXT("rl");
		R.Keeper = 1;
		R.KeeperName = TEXT("The Monkey Sage");
		R.Difficulty = TEXT("Normal");
		R.Skill = 0.4f;
		R.Result = TEXT("win");
		R.Seconds = 42.5f;
		R.PlayerHealth = 0.3f;
		R.KeeperHealth = 0.f;
		R.DmgDealt = 990.f;
		R.DmgTaken = 252.f;
		R.PlayerSwings = 30; R.PlayerHits = 18; R.KeeperSwings = 40; R.KeeperHits = 9; R.KeeperBlocked = 4; R.KeeperParried = 7;
		R.KeeperWhiffed = 12; R.ParryAttempts = 10; R.Dodges = 6; R.GuardBreaks = 1; R.KeeperExposed = 2; R.ReadsLanded = 3;
		R.Predictions = 25; R.PredictionsCorrect = 11; R.Confident = 8; R.ConfidentCorrect = 5;
		R.Expected.Add({ TEXT("fastSlash"), TEXT("parry"), 0.62f });
		R.Expected.Add({ TEXT("heavyCleave"), TEXT("stepL"), 0.48f });
		R.Answers[0][0] = 5;   // fast: parry
		R.Answers[1][2] = 3;   // heavy: stepL
		return R;
	}

	TArray<FString> TransformPaths(const TSharedPtr<FJsonObject>& Write)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* X = nullptr;
		if (Write.IsValid() && Write->TryGetArrayField(TEXT("updateTransforms"), X))
		{
			for (const TSharedPtr<FJsonValue>& V : *X) { Out.Add(V->AsObject()->GetStringField(TEXT("fieldPath"))); }
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWTelemetryAnswersTest, "Project.HellwalkerRL.Telemetry.Answers", HWTelemetryTestFlags)

bool FHWTelemetryAnswersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using HW::ESym;
	TestEqual(TEXT("parry"), HWTelemetry::AnswerKey(ESym::Parry), FString(TEXT("parry")));
	TestEqual(TEXT("block"), HWTelemetry::AnswerKey(ESym::Block), FString(TEXT("block")));
	TestEqual(TEXT("ghoststep left"), HWTelemetry::AnswerKey(ESym::StepL), FString(TEXT("stepL")));
	TestEqual(TEXT("ghoststep right"), HWTelemetry::AnswerKey(ESym::StepR), FString(TEXT("stepR")));
	TestEqual(TEXT("ghoststep back"), HWTelemetry::AnswerKey(ESym::StepB), FString(TEXT("stepB")));
	TestEqual(TEXT("ghoststep in"), HWTelemetry::AnswerKey(ESym::StepF), FString(TEXT("stepF")));
	TestEqual(TEXT("a light attack is swinging back"), HWTelemetry::AnswerKey(ESym::Light), FString(TEXT("attack")));
	TestEqual(TEXT("a heavy attack is swinging back"), HWTelemetry::AnswerKey(ESym::Heavy), FString(TEXT("attack")));
	TestEqual(TEXT("standing is doing nothing"), HWTelemetry::AnswerKey(ESym::Neutral), FString(TEXT("none")));
	TestEqual(TEXT("walking is doing nothing"), HWTelemetry::AnswerKey(ESym::Advance), FString(TEXT("none")));
	TestEqual(TEXT("a weapon switch is doing nothing"), HWTelemetry::AnswerKey(ESym::Switch), FString(TEXT("none")));
	for (int32 S = 0; S < HW::NumPlayerSymbols; ++S)
	{
		TestTrue(FString::Printf(TEXT("every player symbol has a column (%d)"), S), HWTelemetry::AnswerColumn(S) >= 0 && HWTelemetry::AnswerColumn(S) < 8);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWTelemetryFightCommitTest, "Project.HellwalkerRL.Telemetry.FightCommit", HWTelemetryTestFlags)

bool FHWTelemetryFightCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FHWFightRecord R = SampleRecord();
	const FString Id = HWTelemetry::NewId();
	const TSharedPtr<FJsonObject> Body = Parse(HWTelemetry::FightCommitJson(R, TEXT("proj"), Id));
	if (!TestTrue(TEXT("valid JSON"), Body.IsValid())) { return false; }
	const TArray<TSharedPtr<FJsonValue>>& Writes = Body->GetArrayField(TEXT("writes"));
	if (!TestEqual(TEXT("two writes"), Writes.Num(), 2)) { return false; }

	// (1) the fight, created once
	const TSharedPtr<FJsonObject> W1 = Writes[0]->AsObject();
	const TSharedPtr<FJsonObject> Fight = W1->GetObjectField(TEXT("update"));
	TestEqual(TEXT("fight document name"), Fight->GetStringField(TEXT("name")), FString(TEXT("projects/proj/databases/(default)/documents/fights/")) + Id);
	TestFalse(TEXT("the fight must be new (idempotent retries)"), W1->GetObjectField(TEXT("currentDocument"))->GetBoolField(TEXT("exists")));
	TestEqual(TEXT("the server sets `at`"), TransformPaths(W1), TArray<FString>{ TEXT("at") });
	const TSharedPtr<FJsonObject> F = Fight->GetObjectField(TEXT("fields"));
	static const TCHAR* Keys[] = { TEXT("v"), TEXT("player"), TEXT("clientTime"), TEXT("session"), TEXT("fightInSession"), TEXT("gameVersion"),
		TEXT("mode"), TEXT("playMode"), TEXT("brain"), TEXT("keeper"), TEXT("keeperName"), TEXT("difficulty"), TEXT("skill"), TEXT("adaptive"),
		TEXT("result"), TEXT("seconds"), TEXT("playerHealth"), TEXT("keeperHealth"), TEXT("dmgDealt"), TEXT("dmgTaken"), TEXT("playerSwings"),
		TEXT("playerHits"), TEXT("keeperSwings"), TEXT("keeperHits"), TEXT("keeperBlocked"), TEXT("keeperParried"), TEXT("keeperWhiffed"),
		TEXT("parryAttempts"), TEXT("dodges"), TEXT("guardBreaks"), TEXT("keeperExposed"), TEXT("readsLanded"), TEXT("predictions"),
		TEXT("predictionsCorrect"), TEXT("confident"), TEXT("confidentCorrect"), TEXT("expected"), TEXT("answers") };
	TestEqual(TEXT("exactly the contract's fields (all but the server's `at`)"), F->Values.Num(), static_cast<int32>(UE_ARRAY_COUNT(Keys)));
	for (const TCHAR* K : Keys) { TestTrue(FString::Printf(TEXT("field %s"), K), F->HasField(K)); }
	TestEqual(TEXT("ints are strings (integerValue)"), F->GetObjectField(TEXT("keeper"))->GetStringField(TEXT("integerValue")), FString(TEXT("1")));
	TestEqual(TEXT("parries"), F->GetObjectField(TEXT("keeperParried"))->GetStringField(TEXT("integerValue")), FString(TEXT("7")));
	TestTrue(TEXT("skill is a double"), FMath::IsNearlyEqual(F->GetObjectField(TEXT("skill"))->GetNumberField(TEXT("doubleValue")), 0.4, 1e-6));
	TestTrue(TEXT("clientTime is a timestamp"), F->GetObjectField(TEXT("clientTime"))->GetStringField(TEXT("timestampValue")).StartsWith(TEXT("2026-10-03T12:00:00")));
	const TSharedPtr<FJsonObject> Ans = F->GetObjectField(TEXT("answers"))->GetObjectField(TEXT("mapValue"))->GetObjectField(TEXT("fields"));
	TestEqual(TEXT("answers: the four swing classes"), Ans->Values.Num(), 4);
	TestEqual(TEXT("answers.fast.parry"), Ans->GetObjectField(TEXT("fast"))->GetObjectField(TEXT("mapValue"))->GetObjectField(TEXT("fields"))
		->GetObjectField(TEXT("parry"))->GetStringField(TEXT("integerValue")), FString(TEXT("5")));
	const TArray<TSharedPtr<FJsonValue>>& Exp = F->GetObjectField(TEXT("expected"))->GetObjectField(TEXT("arrayValue"))->GetArrayField(TEXT("values"));
	TestEqual(TEXT("expected rows"), Exp.Num(), 2);

	// (2) the player: masked fields + increments
	const TSharedPtr<FJsonObject> W2 = Writes[1]->AsObject();
	TestEqual(TEXT("player document name"), W2->GetObjectField(TEXT("update"))->GetStringField(TEXT("name")),
		FString(TEXT("projects/proj/databases/(default)/documents/players/uid123")));
	TArray<FString> Mask;
	W2->GetObjectField(TEXT("updateMask"))->TryGetStringArrayField(TEXT("fieldPaths"), Mask);
	TestEqual(TEXT("update mask"), Mask, TArray<FString>{ TEXT("v"), TEXT("gameVersion"), TEXT("difficulty"), TEXT("expected") });
	const TArray<FString> Paths = TransformPaths(W2);
	for (const TCHAR* P : { TEXT("lastSeen"), TEXT("totals.fights"), TEXT("totals.wins"), TEXT("totals.seconds"), TEXT("totals.keeperParried"),
			TEXT("totals.parryAttempts"), TEXT("keepers.sage.fights"), TEXT("keepers.sage.wins"), TEXT("answers.fast.parry"), TEXT("answers.heavy.stepL") })
	{
		TestTrue(FString::Printf(TEXT("increment %s"), P), Paths.Contains(P));
	}
	TestFalse(TEXT("no loss on a win"), Paths.Contains(TEXT("totals.losses")));
	TestFalse(TEXT("guard breaks are not a total (fight only)"), Paths.Contains(TEXT("totals.guardBreaks")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWTelemetryPlayerCreateTest, "Project.HellwalkerRL.Telemetry.PlayerCreate", HWTelemetryTestFlags)

bool FHWTelemetryPlayerCreateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TSharedPtr<FJsonObject> Body = Parse(HWTelemetry::PlayerCreateCommitJson(TEXT("proj"), TEXT("uid9"), TEXT("Ashen Moth 0176"), TEXT("1.3.0"), TEXT("Hard")));
	if (!TestTrue(TEXT("valid JSON"), Body.IsValid())) { return false; }
	const TSharedPtr<FJsonObject> W = Body->GetArrayField(TEXT("writes"))[0]->AsObject();
	TestFalse(TEXT("created once"), W->GetObjectField(TEXT("currentDocument"))->GetBoolField(TEXT("exists")));
	TestEqual(TEXT("server-time createdAt"), TransformPaths(W), TArray<FString>{ TEXT("createdAt") });
	const TSharedPtr<FJsonObject> F = W->GetObjectField(TEXT("update"))->GetObjectField(TEXT("fields"));
	TestEqual(TEXT("nickname"), F->GetObjectField(TEXT("nickname"))->GetStringField(TEXT("stringValue")), FString(TEXT("Ashen Moth 0176")));
	const TSharedPtr<FJsonObject> Totals = F->GetObjectField(TEXT("totals"))->GetObjectField(TEXT("mapValue"))->GetObjectField(TEXT("fields"));
	bool bZero = Totals->Values.Num() == 21;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Totals->Values)
	{
		const TSharedPtr<FJsonObject> V = KV.Value->AsObject();
		FString IntV;
		double DblV = 1.0;
		bZero = bZero && ((V->TryGetStringField(TEXT("integerValue"), IntV) && IntV == TEXT("0")) || (V->TryGetNumberField(TEXT("doubleValue"), DblV) && DblV == 0.0));
	}
	TestTrue(TEXT("all 21 totals start at zero (the rules insist)"), bZero);
	TestTrue(TEXT("resetAt is null"), F->GetObjectField(TEXT("resetAt"))->HasField(TEXT("nullValue")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWTelemetryNicknameTest, "Project.HellwalkerRL.Telemetry.Nickname", HWTelemetryTestFlags)

bool FHWTelemetryNicknameTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FRegexPattern Pattern(TEXT("^[A-Z][a-z]+ [A-Z][a-z]+ [0-9]{4}$"));
	bool bAll = true;
	for (int32 Seed = 0; Seed < 200; ++Seed)
	{
		FRandomStream Rng(Seed);
		const FString N = HWTelemetry::MakeNickname(Rng);
		FRegexMatcher M(Pattern, N);
		bAll = bAll && N.Len() <= 24 && M.FindNext();
	}
	TestTrue(TEXT("200 nicknames fit the pattern and 24 characters"), bAll);
	FRandomStream A(7), B(7);
	TestEqual(TEXT("deterministic per seed (the uid)"), HWTelemetry::MakeNickname(A), HWTelemetry::MakeNickname(B));
	TestEqual(TEXT("ids are 32 hex"), HWTelemetry::NewId().Len(), 32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWTelemetryNotebookDeltaTest, "Project.HellwalkerRL.Telemetry.NotebookDelta", HWTelemetryTestFlags)

bool FHWTelemetryNotebookDeltaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	HW::FRLNotebook Before, Now;
	Before.Predictions = 10; Before.Correct = 4; Before.Confident = 3; Before.ConfidentCorrect = 2; Before.ReadsLanded = 1; Before.Fights = 1;
	Before.Answers[0][static_cast<int32>(HW::ESym::Parry)] = 6;
	Now = Before;
	Now.Predictions = 25; Now.Correct = 12; Now.Confident = 9; Now.ConfidentCorrect = 5; Now.ReadsLanded = 4; Now.Fights = 2;
	Now.Answers[0][static_cast<int32>(HW::ESym::Parry)] = 9;      // +3 parries vs fast swings
	Now.Answers[1][static_cast<int32>(HW::ESym::Light)] = 2;      // +2 swing-backs vs heavies
	Now.Answers[1][static_cast<int32>(HW::ESym::Heavy)] = 1;      // +1
	FHWFightRecord R;
	HWTelemetry::NotebookDelta(Before, Now, R);
	TestEqual(TEXT("predictions this fight"), R.Predictions, 15);
	TestEqual(TEXT("correct this fight"), R.PredictionsCorrect, 8);
	TestEqual(TEXT("READs this fight"), R.ReadsLanded, 3);
	TestEqual(TEXT("fast: parry +3"), R.Answers[0][0], 3);
	TestEqual(TEXT("heavy: attack +3 (light and heavy)"), R.Answers[1][6], 3);
	// A reset notebook (hw.ResetModel): this fight's numbers are the new notebook's own.
	HW::FRLNotebook Fresh;
	Fresh.Predictions = 5; Fresh.Correct = 2; Fresh.Fights = 1;
	FHWFightRecord R2;
	HWTelemetry::NotebookDelta(Now, Fresh, R2);
	TestEqual(TEXT("after a reset: counted from zero"), R2.Predictions, 5);
	TestEqual(TEXT("... never negative"), R2.Answers[0][0], 0);
	return true;
}

#endif
