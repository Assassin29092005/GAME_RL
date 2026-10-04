#include "HWTelemetry.h"

#include "HellwalkerRL.h"
#include "HWDuelSubsystem.h"
#include "HWOpenWorldGameMode.h"
#include "HWSaveGame.h"
#include "HWSessionSubsystem.h"
#include "HWSettings.h"
#include "HWCore/HWMoves.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <shellapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// =================================================================================================
// The record -> Firestore JSON (pure)
// =================================================================================================

namespace HWTelemetry
{
	const TCHAR* const ClassKeys[4] = { TEXT("fast"), TEXT("heavy"), TEXT("feint"), TEXT("killer") };
	const TCHAR* const AnswerKeys[8] = { TEXT("parry"), TEXT("block"), TEXT("stepL"), TEXT("stepR"), TEXT("stepB"), TEXT("stepF"), TEXT("attack"), TEXT("none") };
	const TCHAR* const KeeperKeys[3] = { TEXT("warden"), TEXT("sage"), TEXT("returned") };

	int32 AnswerColumn(int32 PlayerSym)
	{
		switch (static_cast<HW::ESym>(PlayerSym))
		{
		case HW::ESym::Parry:   return 0;
		case HW::ESym::Block:   return 1;
		case HW::ESym::StepL:   return 2;
		case HW::ESym::StepR:   return 3;
		case HW::ESym::StepB:   return 4;
		case HW::ESym::StepF:   return 5;
		case HW::ESym::Light:
		case HW::ESym::Heavy:   return 6;
		case HW::ESym::Neutral:
		case HW::ESym::Advance:
		case HW::ESym::Retreat:
		case HW::ESym::Switch:  return 7;
		default:                return -1;
		}
	}

	FString AnswerKey(HW::ESym Sym)
	{
		const int32 C = AnswerColumn(static_cast<int32>(Sym));
		return C >= 0 ? FString(AnswerKeys[C]) : FString(TEXT("none"));
	}

	FString AssistKey(const FString& Name)
	{
		const FString N = Name.Replace(TEXT(" "), TEXT("")).ToLower();
		if (!N.StartsWith(TEXT("ring"))) { return TEXT("off"); }
		return N.Contains(TEXT("slow")) ? TEXT("ring+slowmo") : TEXT("ring");
	}

	void NotebookDelta(const HW::FRLNotebook& Before, const HW::FRLNotebook& Now, FHWFightRecord& R)
	{
		// The notebook is the session's (cumulative); a reset one (hw.ResetModel, a new session) starts again from zero.
		const bool bReset = Now.Predictions < Before.Predictions || Now.Fights < Before.Fights;
		static const HW::FRLNotebook Zero;
		const HW::FRLNotebook& B = bReset ? Zero : Before;
		auto D = [](int32 A, int32 Z) { return FMath::Max(0, A - Z); };
		R.Predictions = D(Now.Predictions, B.Predictions);
		R.PredictionsCorrect = FMath::Min(R.Predictions, D(Now.Correct, B.Correct));
		R.Confident = D(Now.Confident, B.Confident);
		R.ConfidentCorrect = FMath::Min(R.Confident, D(Now.ConfidentCorrect, B.ConfidentCorrect));
		R.ReadsLanded = D(Now.ReadsLanded, B.ReadsLanded);
		for (int32 C = 0; C < 4; ++C)      // notebook classes 0..3 = the boss symbols BFast, BHeavy, BFeint, BKiller
		{
			for (int32 K = 0; K < 8; ++K) { R.Answers[C][K] = 0; }
			for (int32 S = 0; S < HW::NumPlayerSymbols; ++S)
			{
				const int32 Col = AnswerColumn(S);
				if (Col >= 0) { R.Answers[C][Col] += D(Now.Answers[C][S], B.Answers[C][S]); }
			}
		}
	}

	FString NewId()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
	}

	FString MakeNickname(FRandomStream& Rng)
	{
		static const TCHAR* Adjectives[] = { TEXT("Ashen"), TEXT("Pale"), TEXT("Crimson"), TEXT("Hollow"), TEXT("Silent"),
			TEXT("Grave"), TEXT("Ember"), TEXT("Iron"), TEXT("Veiled"), TEXT("Wandering"), TEXT("Restless"), TEXT("Bitter"),
			TEXT("Scarred"), TEXT("Gilded"), TEXT("Sunken"), TEXT("Lantern") };
		static const TCHAR* Nouns[] = { TEXT("Wanderer"), TEXT("Moth"), TEXT("Shade"), TEXT("Exile"), TEXT("Vigil"),
			TEXT("Blade"), TEXT("Hound"), TEXT("Pilgrim"), TEXT("Bellringer"), TEXT("Ember"), TEXT("Walker"), TEXT("Thorn"),
			TEXT("Raven"), TEXT("Seeker"), TEXT("Ash"), TEXT("Warden") };
		const FString A = Adjectives[Rng.RandHelper(UE_ARRAY_COUNT(Adjectives))];
		const FString N = Nouns[Rng.RandHelper(UE_ARRAY_COUNT(Nouns))];
		return FString::Printf(TEXT("%s %s %04d"), *A, *N, Rng.RandRange(0, 9999)).Left(24);
	}

	FString DocumentsRoot(const FString& ProjectId)
	{
		return FString::Printf(TEXT("projects/%s/databases/(default)/documents"), *ProjectId);
	}

	namespace
	{
		using FObj = TSharedPtr<FJsonObject>;
		using FVal = TSharedPtr<FJsonValue>;

		FVal Typed(const TCHAR* Kind, FVal V)
		{
			FObj O = MakeShared<FJsonObject>();
			O->SetField(Kind, V);
			return MakeShared<FJsonValueObject>(O);
		}
		FVal I(int64 V) { return Typed(TEXT("integerValue"), MakeShared<FJsonValueString>(LexToString(V))); }
		FVal D(double V) { return Typed(TEXT("doubleValue"), MakeShared<FJsonValueNumber>(V)); }
		FVal S(const FString& V) { return Typed(TEXT("stringValue"), MakeShared<FJsonValueString>(V)); }
		FVal B(bool V) { return Typed(TEXT("booleanValue"), MakeShared<FJsonValueBoolean>(V)); }
		FVal T(const FDateTime& V) { return Typed(TEXT("timestampValue"), MakeShared<FJsonValueString>(V.ToIso8601())); }
		FVal M(FObj Fields)
		{
			FObj Inner = MakeShared<FJsonObject>();
			Inner->SetObjectField(TEXT("fields"), Fields);
			return Typed(TEXT("mapValue"), MakeShared<FJsonValueObject>(Inner));
		}
		FVal A(const TArray<FVal>& Values)
		{
			FObj Inner = MakeShared<FJsonObject>();
			Inner->SetArrayField(TEXT("values"), Values);
			return Typed(TEXT("arrayValue"), MakeShared<FJsonValueObject>(Inner));
		}
		FString Write(FObj O)
		{
			FString Out;
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(O.ToSharedRef(), W);
			return Out;
		}
		FObj ServerTime(const TCHAR* Path)
		{
			FObj X = MakeShared<FJsonObject>();
			X->SetStringField(TEXT("fieldPath"), Path);
			X->SetStringField(TEXT("setToServerValue"), TEXT("REQUEST_TIME"));
			return X;
		}
		FObj Increment(const FString& Path, FVal By)
		{
			FObj X = MakeShared<FJsonObject>();
			X->SetStringField(TEXT("fieldPath"), Path);
			X->SetField(TEXT("increment"), By);
			return X;
		}
		FObj Precondition(bool bExists)
		{
			FObj X = MakeShared<FJsonObject>();
			X->SetBoolField(TEXT("exists"), bExists);
			return X;
		}
		FVal ExpectedArray(const FHWFightRecord& R)
		{
			TArray<FVal> Rows;
			for (const FHWFightRecord::FExpected& E : R.Expected)
			{
				FObj F = MakeShared<FJsonObject>();
				F->SetField(TEXT("attack"), S(E.Attack));
				F->SetField(TEXT("answer"), S(E.Answer));
				F->SetField(TEXT("p"), D(FMath::Clamp(E.P, 0.f, 1.f)));
				Rows.Add(M(F));
			}
			return A(Rows);
		}
		FObj FightFields(const FHWFightRecord& R)
		{
			FObj F = MakeShared<FJsonObject>();
			auto Frac = [](float V) { return FMath::Clamp(static_cast<double>(V), 0.0, 1.0); };
			// A v2 number inside the rules' range (a NaN or an infinity would be refused: it becomes the neutral value).
			auto Within = [](float V, double Lo, double Hi, double IfBad) { return FMath::IsFinite(V) ? FMath::Clamp(static_cast<double>(V), Lo, Hi) : IfBad; };
			F->SetField(TEXT("v"), I(FightSchemaVersion));
			F->SetField(TEXT("player"), S(R.Player));
			F->SetField(TEXT("clientTime"), T(R.ClientTime));
			F->SetField(TEXT("session"), S(R.Session));
			F->SetField(TEXT("fightInSession"), I(FMath::Max(1, R.FightInSession)));
			F->SetField(TEXT("gameVersion"), S(R.GameVersion.Left(32)));
			F->SetField(TEXT("mode"), S(R.Mode));
			F->SetField(TEXT("playMode"), S(R.PlayMode));
			F->SetField(TEXT("brain"), S(R.Brain));
			F->SetField(TEXT("keeper"), I(FMath::Clamp(R.Keeper, 0, 2)));
			F->SetField(TEXT("keeperName"), S(R.KeeperName.Left(64)));
			F->SetField(TEXT("difficulty"), S(R.Difficulty));
			F->SetField(TEXT("skill"), D(Frac(R.Skill)));
			F->SetField(TEXT("adaptive"), B(R.bAdaptive));
			F->SetField(TEXT("result"), S(R.Result));
			F->SetField(TEXT("seconds"), D(FMath::Max(0.0, static_cast<double>(R.Seconds))));
			F->SetField(TEXT("playerHealth"), D(Frac(R.PlayerHealth)));
			F->SetField(TEXT("keeperHealth"), D(Frac(R.KeeperHealth)));
			F->SetField(TEXT("dmgDealt"), D(FMath::Max(0.0, static_cast<double>(R.DmgDealt))));
			F->SetField(TEXT("dmgTaken"), D(FMath::Max(0.0, static_cast<double>(R.DmgTaken))));
			const TPair<const TCHAR*, int32> Counts[] = {
				{ TEXT("playerSwings"), R.PlayerSwings }, { TEXT("playerHits"), R.PlayerHits }, { TEXT("keeperSwings"), R.KeeperSwings },
				{ TEXT("keeperHits"), R.KeeperHits }, { TEXT("keeperBlocked"), R.KeeperBlocked }, { TEXT("keeperParried"), R.KeeperParried },
				{ TEXT("keeperWhiffed"), R.KeeperWhiffed }, { TEXT("parryAttempts"), R.ParryAttempts }, { TEXT("dodges"), R.Dodges },
				{ TEXT("guardBreaks"), R.GuardBreaks }, { TEXT("keeperExposed"), R.KeeperExposed }, { TEXT("readsLanded"), R.ReadsLanded },
				{ TEXT("predictions"), R.Predictions }, { TEXT("predictionsCorrect"), R.PredictionsCorrect },
				{ TEXT("confident"), R.Confident }, { TEXT("confidentCorrect"), R.ConfidentCorrect },
			};
			for (const TPair<const TCHAR*, int32>& C : Counts) { F->SetField(C.Key, I(FMath::Max(0, C.Value))); }
			F->SetField(TEXT("expected"), ExpectedArray(R));
			FObj Ans = MakeShared<FJsonObject>();
			for (int32 C = 0; C < 4; ++C)
			{
				FObj Row = MakeShared<FJsonObject>();
				for (int32 K = 0; K < 8; ++K)
				{
					if (R.Answers[C][K] > 0) { Row->SetField(AnswerKeys[K], I(R.Answers[C][K])); }
				}
				Ans->SetField(ClassKeys[C], M(Row));
			}
			F->SetField(TEXT("answers"), M(Ans));
			// v2: the fight's setup. Slow motion exists only with the ring, so any other assist reports a scale of 1.
			const bool bKnownAssist = R.Assist.Equals(TEXT("ring"), ESearchCase::CaseSensitive)
				|| R.Assist.Equals(TEXT("ring+slowmo"), ESearchCase::CaseSensitive);
			const FString Assist = bKnownAssist ? R.Assist : FString(TEXT("off"));
			F->SetField(TEXT("assist"), S(Assist));
			F->SetField(TEXT("slowmoScale"), D(Assist == TEXT("ring+slowmo") ? Within(R.SlowmoScale, 0.05, 1.0, 1.0) : 1.0));
			F->SetField(TEXT("keeperDamageScale"), D(Within(R.KeeperDamageScale, 0.05, 2.0, 1.0)));
			F->SetField(TEXT("parryWindowFrames"), I(FMath::Clamp(R.ParryWindowFrames, 1, 60)));
			F->SetField(TEXT("insight"), D(Within(R.Insight, 0.0, 1.0, 0.0)));
			return F;
		}
	}

	FString FightFieldsJson(const FHWFightRecord& R)
	{
		return Write(FightFields(R));
	}

	FString FightCommitJson(const FHWFightRecord& R, const FString& ProjectId, const FString& FightId)
	{
		const FString Root = DocumentsRoot(ProjectId);
		// (1) the fight, created once (a retry of a delivered fight fails as ALREADY_EXISTS: idempotent).
		FObj FightDoc = MakeShared<FJsonObject>();
		FightDoc->SetStringField(TEXT("name"), Root / TEXT("fights") / FightId);
		FightDoc->SetObjectField(TEXT("fields"), FightFields(R));
		FObj W1 = MakeShared<FJsonObject>();
		W1->SetObjectField(TEXT("update"), FightDoc);
		W1->SetObjectField(TEXT("currentDocument"), Precondition(false));
		W1->SetArrayField(TEXT("updateTransforms"), { MakeShared<FJsonValueObject>(ServerTime(TEXT("at"))) });

		// (2) the player: the masked fields + server-side increments of everything this fight added.
		FObj PlayerFields = MakeShared<FJsonObject>();
		PlayerFields->SetField(TEXT("v"), I(1));
		PlayerFields->SetField(TEXT("gameVersion"), S(R.GameVersion.Left(32)));
		PlayerFields->SetField(TEXT("difficulty"), S(R.Difficulty));
		PlayerFields->SetField(TEXT("expected"), ExpectedArray(R));
		FObj PlayerDoc = MakeShared<FJsonObject>();
		PlayerDoc->SetStringField(TEXT("name"), Root / TEXT("players") / R.Player);
		PlayerDoc->SetObjectField(TEXT("fields"), PlayerFields);
		FObj Mask = MakeShared<FJsonObject>();
		Mask->SetArrayField(TEXT("fieldPaths"), { MakeShared<FJsonValueString>(TEXT("v")), MakeShared<FJsonValueString>(TEXT("gameVersion")),
			MakeShared<FJsonValueString>(TEXT("difficulty")), MakeShared<FJsonValueString>(TEXT("expected")) });
		TArray<FVal> X;
		X.Add(MakeShared<FJsonValueObject>(ServerTime(TEXT("lastSeen"))));
		auto Inc = [&X](const FString& Path, FVal By) { X.Add(MakeShared<FJsonValueObject>(Increment(Path, By))); };
		Inc(TEXT("totals.fights"), I(1));
		if (R.Result == TEXT("win")) { Inc(TEXT("totals.wins"), I(1)); }
		else if (R.Result == TEXT("loss")) { Inc(TEXT("totals.losses"), I(1)); }
		else if (R.Result == TEXT("timeout")) { Inc(TEXT("totals.timeouts"), I(1)); }
		Inc(TEXT("totals.seconds"), D(FMath::Max(0.0, static_cast<double>(R.Seconds))));
		Inc(TEXT("totals.dmgDealt"), D(FMath::Max(0.0, static_cast<double>(R.DmgDealt))));
		Inc(TEXT("totals.dmgTaken"), D(FMath::Max(0.0, static_cast<double>(R.DmgTaken))));
		const TPair<const TCHAR*, int32> Totals[] = {
			{ TEXT("playerSwings"), R.PlayerSwings }, { TEXT("playerHits"), R.PlayerHits }, { TEXT("keeperSwings"), R.KeeperSwings },
			{ TEXT("keeperHits"), R.KeeperHits }, { TEXT("keeperBlocked"), R.KeeperBlocked }, { TEXT("keeperParried"), R.KeeperParried },
			{ TEXT("keeperWhiffed"), R.KeeperWhiffed }, { TEXT("parryAttempts"), R.ParryAttempts }, { TEXT("dodges"), R.Dodges },
			{ TEXT("readsLanded"), R.ReadsLanded }, { TEXT("predictions"), R.Predictions },
			{ TEXT("predictionsCorrect"), R.PredictionsCorrect }, { TEXT("confident"), R.Confident },
			{ TEXT("confidentCorrect"), R.ConfidentCorrect },
		};
		for (const TPair<const TCHAR*, int32>& C : Totals)
		{
			if (C.Value > 0) { Inc(FString(TEXT("totals.")) + C.Key, I(C.Value)); }
		}
		const FString Keeper = KeeperKeys[FMath::Clamp(R.Keeper, 0, 2)];
		Inc(FString::Printf(TEXT("keepers.%s.fights"), *Keeper), I(1));
		if (R.Result == TEXT("win")) { Inc(FString::Printf(TEXT("keepers.%s.wins"), *Keeper), I(1)); }
		for (int32 C = 0; C < 4; ++C)
		{
			for (int32 K = 0; K < 8; ++K)
			{
				if (R.Answers[C][K] > 0) { Inc(FString::Printf(TEXT("answers.%s.%s"), ClassKeys[C], AnswerKeys[K]), I(R.Answers[C][K])); }
			}
		}
		FObj W2 = MakeShared<FJsonObject>();
		W2->SetObjectField(TEXT("update"), PlayerDoc);
		W2->SetObjectField(TEXT("updateMask"), Mask);
		W2->SetArrayField(TEXT("updateTransforms"), X);

		FObj Body = MakeShared<FJsonObject>();
		Body->SetArrayField(TEXT("writes"), { MakeShared<FJsonValueObject>(W1), MakeShared<FJsonValueObject>(W2) });
		return Write(Body);
	}

	FString PlayerCreateCommitJson(const FString& ProjectId, const FString& Uid, const FString& Nickname, const FString& GameVersion,
		const FString& Difficulty)
	{
		FObj Zeros = MakeShared<FJsonObject>();
		for (const TCHAR* K : { TEXT("fights"), TEXT("wins"), TEXT("losses"), TEXT("timeouts"), TEXT("playerSwings"), TEXT("playerHits"),
				TEXT("keeperSwings"), TEXT("keeperHits"), TEXT("keeperBlocked"), TEXT("keeperParried"), TEXT("keeperWhiffed"),
				TEXT("parryAttempts"), TEXT("dodges"), TEXT("readsLanded"), TEXT("predictions"), TEXT("predictionsCorrect"),
				TEXT("confident"), TEXT("confidentCorrect") })
		{
			Zeros->SetField(K, I(0));
		}
		for (const TCHAR* K : { TEXT("seconds"), TEXT("dmgDealt"), TEXT("dmgTaken") }) { Zeros->SetField(K, D(0.0)); }
		FObj KeeperZeros = MakeShared<FJsonObject>();
		for (const TCHAR* K : KeeperKeys)
		{
			FObj R = MakeShared<FJsonObject>();
			R->SetField(TEXT("fights"), I(0));
			R->SetField(TEXT("wins"), I(0));
			KeeperZeros->SetField(K, M(R));
		}
		FObj F = MakeShared<FJsonObject>();
		F->SetField(TEXT("v"), I(1));
		F->SetField(TEXT("nickname"), S(Nickname));
		F->SetField(TEXT("gameVersion"), S(GameVersion.Left(32)));
		F->SetField(TEXT("difficulty"), S(Difficulty));
		F->SetField(TEXT("totals"), M(Zeros));
		F->SetField(TEXT("keepers"), M(KeeperZeros));
		F->SetField(TEXT("answers"), M(MakeShared<FJsonObject>()));
		F->SetField(TEXT("expected"), A({}));
		F->SetField(TEXT("resets"), I(0));
		F->SetField(TEXT("resetAt"), Typed(TEXT("nullValue"), MakeShared<FJsonValueNull>()));
		FObj Doc = MakeShared<FJsonObject>();
		Doc->SetStringField(TEXT("name"), DocumentsRoot(ProjectId) / TEXT("players") / Uid);
		Doc->SetObjectField(TEXT("fields"), F);
		FObj W = MakeShared<FJsonObject>();
		W->SetObjectField(TEXT("update"), Doc);
		W->SetObjectField(TEXT("currentDocument"), Precondition(false));
		W->SetArrayField(TEXT("updateTransforms"), { MakeShared<FJsonValueObject>(ServerTime(TEXT("createdAt"))) });
		FObj Body = MakeShared<FJsonObject>();
		Body->SetArrayField(TEXT("writes"), { MakeShared<FJsonValueObject>(W) });
		return Write(Body);
	}
}

// =================================================================================================
// The subsystem
// =================================================================================================

namespace
{
	constexpr int32 MaxPending = 200;           // the queue keeps the newest; older fights fall off if the game stays offline
	constexpr double TokenLifetime = 50.0 * 60.0;
	const TCHAR* const ConfigSection = TEXT("HWTelemetry");
	const TCHAR* const UidSlot = TEXT("{uid}");

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> O;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(R, O) ? O : nullptr;
	}

	/** Google's error code in a REST error body ({"error": {"message": "INVALID_REFRESH_TOKEN", "status": ...}}). */
	FString ErrorMessage(const FString& Text)
	{
		const TSharedPtr<FJsonObject> J = ParseJson(Text);
		const TSharedPtr<FJsonObject>* Err = nullptr;
		FString M;
		if (J.IsValid() && J->TryGetObjectField(TEXT("error"), Err) && Err != nullptr)
		{
			if (!(*Err)->TryGetStringField(TEXT("message"), M)) { (*Err)->TryGetStringField(TEXT("status"), M); }
		}
		return M;
	}

	bool IsScriptedLaunch()
	{
		FString Exec;
		return FApp::IsUnattended() || FParse::Value(FCommandLine::Get(), TEXT("-HWExec="), Exec);
	}

	/** A launch that changes the opponent or the rules (another keeper model, the duel's movement, no hit-stop). */
	bool HasDevOverride(FString& OutWhich)
	{
		const TCHAR* Cmd = FCommandLine::Get();
		FString V;
		if (FParse::Value(Cmd, TEXT("-HWPolicy="), V)) { OutWhich = TEXT("another keeper model (-HWPolicy)"); return true; }
		if (FParse::Value(Cmd, TEXT("-HWMoveAccel="), V) || FParse::Value(Cmd, TEXT("-HWMoveBraking="), V)) { OutWhich = TEXT("changed duel movement (-HWMoveAccel / -HWMoveBraking)"); return true; }
		if (FParse::Param(Cmd, TEXT("HWNoHitstop"))) { OutWhich = TEXT("no hit-stop (-HWNoHitstop)"); return true; }
		return false;
	}
}

const TCHAR* UHWTelemetrySubsystem::SlotName() const
{
	return Endpoint.IsEmpty() ? UHWTelemetrySave::SlotName : UHWTelemetrySave::TestSlotName;
}

void UHWTelemetrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bool bConfigOn = true;
	GConfig->GetBool(ConfigSection, TEXT("bEnabled"), bConfigOn, GGameIni);
	GConfig->GetString(ConfigSection, TEXT("ApiKey"), ApiKey, GGameIni);
	GConfig->GetString(ConfigSection, TEXT("ProjectId"), ProjectId, GGameIni);
	GConfig->GetString(ConfigSection, TEXT("SiteUrl"), SiteUrl, GGameIni);
	GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), GameVersion, GGameIni);
	if (GameVersion.IsEmpty()) { GameVersion = TEXT("1.0.0"); }
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("-HWTelemetryApiKey="), ApiKey);
	FParse::Value(Cmd, TEXT("-HWTelemetryProjectId="), ProjectId);
	FParse::Value(Cmd, TEXT("-HWTelemetrySiteUrl="), SiteUrl);
	FParse::Value(Cmd, TEXT("-HWTelemetryEndpoint="), Endpoint);
	ApiKey.TrimStartAndEndInline();
	ProjectId.TrimStartAndEndInline();
	SiteUrl.TrimStartAndEndInline();
	while (SiteUrl.EndsWith(TEXT("/"))) { SiteUrl.LeftChopInline(1); }
	while (Endpoint.EndsWith(TEXT("/"))) { Endpoint.LeftChopInline(1); }
	// Tool fights are research data only in tests against a mock, never against the real project.
	bAllowAutoplay = !Endpoint.IsEmpty() && FParse::Param(Cmd, TEXT("HWTelemetryAllowAutoplay"));
#if UE_BUILD_SHIPPING
	const bool bBuildMayUpload = true;
#else
	// A development build (the editor, Play.bat, scripted checks) is the developer's own play: never research data in the
	// real project unless asked for (-HWTelemetryDev). A mock endpoint (the tests) is always allowed.
	const bool bBuildMayUpload = !Endpoint.IsEmpty() || FParse::Param(Cmd, TEXT("HWTelemetryDev"));
	bDevBuildOff = !bBuildMayUpload;
#endif
	bEnabled = bConfigOn && bBuildMayUpload && !ApiKey.IsEmpty() && !ProjectId.IsEmpty() && !FParse::Param(Cmd, TEXT("HWNoTelemetry"));
	Session = HWTelemetry::NewId();

	Save = Cast<UHWTelemetrySave>(UGameplayStatics::LoadGameFromSlot(SlotName(), 0));
	if (Save == nullptr) { Save = Cast<UHWTelemetrySave>(UGameplayStatics::CreateSaveGameObject(UHWTelemetrySave::StaticClass())); }
	if (Save != nullptr && !Save->ProjectId.IsEmpty() && Save->ProjectId != ProjectId && bEnabled)
	{
		// Another project's identity (the build was pointed elsewhere): start a new anonymous player there. That project's
		// queued fights stay (they are skipped, not deleted) in case the build is pointed back.
		Save->Uid.Reset();
		Save->RefreshToken.Reset();
		Save->bPlayerCreated = false;
	}
	if (!bEnabled)
	{
		UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: off (%s)."), FParse::Param(Cmd, TEXT("HWNoTelemetry")) ? TEXT("-HWNoTelemetry")
			: TEXT("no Firebase project in Config/DefaultGame.ini [HWTelemetry]; see web/README.md"));
		return;
	}
	UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: on (project %s%s), %d fight(s) waiting to upload."), *ProjectId,
		Endpoint.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" via %s"), *Endpoint), GetPendingCount());
	Flush();
}

FString UHWTelemetrySubsystem::AuthBase() const
{
	return Endpoint.IsEmpty() ? FString(TEXT("https://identitytoolkit.googleapis.com/v1")) : Endpoint + TEXT("/identitytoolkit/v1");
}

FString UHWTelemetrySubsystem::TokenBase() const
{
	return Endpoint.IsEmpty() ? FString(TEXT("https://securetoken.googleapis.com/v1")) : Endpoint + TEXT("/securetoken/v1");
}

FString UHWTelemetrySubsystem::FirestoreDocsUrl() const
{
	const FString Base = Endpoint.IsEmpty() ? FString(TEXT("https://firestore.googleapis.com/v1")) : Endpoint + TEXT("/firestore/v1");
	return Base / HWTelemetry::DocumentsRoot(ProjectId);
}

bool UHWTelemetrySubsystem::HasFreshToken() const
{
	return !IdToken.IsEmpty() && IdTokenTime >= 0.0 && FPlatformTime::Seconds() - IdTokenTime < TokenLifetime;
}

void UHWTelemetrySubsystem::PersistSave()
{
	if (Save != nullptr) { UGameplayStatics::SaveGameToSlot(Save, SlotName(), 0); }
}

int32 UHWTelemetrySubsystem::FirstPendingIndex() const
{
	if (Save == nullptr) { return INDEX_NONE; }
	return Save->Pending.IndexOfByPredicate([this](const FHWTelemetryPending& P) { return P.ProjectId == ProjectId && !P.Body.IsEmpty(); });
}

void UHWTelemetrySubsystem::BeginFight()
{
	const UGameInstance* GI = GetGameInstance();
	const UHWSessionSubsystem* SessionSub = GI != nullptr ? GI->GetSubsystem<UHWSessionSubsystem>() : nullptr;
	StartBook = SessionSub != nullptr ? SessionSub->GetNotebook() : HW::FRLNotebook{};
	bHaveStartBook = true;
}

bool UHWTelemetrySubsystem::BuildRecord(const UHWDuelSubsystem& Duel, bool bPlayerWon, bool bTimeout, FHWFightRecord& R, FString& OutSkip)
{
	const HW::FEncounter* Enc = Duel.GetEncounter();
	const UGameInstance* GI = GetGameInstance();
	const UHWSessionSubsystem* SessionSub = GI != nullptr ? GI->GetSubsystem<UHWSessionSubsystem>() : nullptr;
	const bool bRL = Duel.GetRLBrain() != nullptr;
	// This fight's notebook numbers: now minus the notebook as the fight began (abandoned fights in between and a reset
	// memory never leak into it). Without a start snapshot the fight's own numbers cannot be told apart: none are sent.
	const HW::FRLNotebook Now = SessionSub != nullptr ? SessionSub->GetNotebook() : HW::FRLNotebook{};
	const bool bHaveBaseline = bHaveStartBook;
	const HW::FRLNotebook Before = StartBook;
	bHaveStartBook = false;
	FString Why;
	if (!bEnabled) { OutSkip = TEXT("telemetry off"); return false; }
	if (Enc == nullptr) { OutSkip = TEXT("no encounter"); return false; }
	if (!bAllowAutoplay)
	{
		if (Duel.IsAutoplay()) { OutSkip = TEXT("the autoplay bot fought it (not research data)"); return false; }
		if (Duel.IsParityRun()) { OutSkip = TEXT("a parity run"); return false; }
		if (Duel.IsNotResearch(&Why)) { OutSkip = Why + TEXT(" (not research data)"); return false; }
		if (IsScriptedLaunch()) { OutSkip = TEXT("a scripted launch (-unattended / -HWExec)"); return false; }
		if (HasDevOverride(Why)) { OutSkip = Why + TEXT(" (not research data)"); return false; }
	}
	const HW::FEncounterStats& St = Enc->Stats;
	const HW::FFighter& P = Enc->Duel.Get(HW::ESide::Player);
	const HW::FFighter& K = Enc->Duel.Get(HW::ESide::Boss);
	auto O = [&St](HW::EHitOutcome X) { return St.BossOutcomes[static_cast<int32>(X)]; };

	R = FHWFightRecord{};
	R.Player = UidSlot;              // filled in at send time, whichever identity this copy has by then
	R.ClientTime = FDateTime::UtcNow();
	R.Session = Session;
	R.FightInSession = ++FightsThisSession;
	R.GameVersion = GameVersion;
	const UWorld* World = Duel.GetWorld();
	const AHWOpenWorldGameMode* OW = World != nullptr ? World->GetAuthGameMode<AHWOpenWorldGameMode>() : nullptr;
	R.Mode = OW != nullptr ? TEXT("openworld") : TEXT("arena");
	R.PlayMode = TEXT("arena");
	if (OW != nullptr && OW->GetSave() != nullptr)
	{
		// "Normal" = pathbreaker, "Adaptive AI" = hellwalker. A save from before 1.4.0 in 66 Days plays as Adaptive AI, so it
		// reports hellwalker ("66days" only ever comes from older builds).
		R.PlayMode = OW->GetSave()->Mode == EHWPlayMode::Pathbreaker ? TEXT("pathbreaker") : TEXT("hellwalker");
	}
	R.Brain = bRL ? TEXT("rl") : TEXT("script");
	R.Keeper = Duel.GetKeeperIdentity();
	R.KeeperName = Duel.BossTitle;
	R.Difficulty = Duel.GetFightDifficulty();
	if (R.Difficulty.IsEmpty())
	{
		const UHWSettingsSubsystem* Settings = GI != nullptr ? GI->GetSubsystem<UHWSettingsSubsystem>() : nullptr;
		R.Difficulty = Settings != nullptr ? UHWSettingsSubsystem::DifficultyName(Settings->GetDifficulty()) : FString(TEXT("Hellwalker"));
	}
	R.Skill = bRL ? Duel.GetKeeperSkill() : 1.f;
	R.bAdaptive = bRL;               // v2: every RL fight's skill is set by the insight ramp
	R.Result = bTimeout ? TEXT("timeout") : (bPlayerWon ? TEXT("win") : TEXT("loss"));
	// v2: the fight's setup as it began (the duel keeps the start-of-fight values; the HUD shows the same ones).
	R.Assist = HWTelemetry::AssistKey(Duel.GetFightAssistName());
	R.SlowmoScale = Duel.GetFightSlowmoScale();
	R.KeeperDamageScale = Duel.GetFightKeeperDamageScale();
	R.ParryWindowFrames = HW::Move(HW::EMoveId::PParry).Active;
	R.Insight = bRL ? Duel.GetFightInsight() : 0.f;
	R.Seconds = static_cast<float>(St.Frames) / static_cast<float>(HW::FramesPerSecond);
	R.PlayerHealth = P.HealthMax > 0.f ? P.Health / P.HealthMax : 0.f;
	R.KeeperHealth = K.HealthMax > 0.f ? K.Health / K.HealthMax : 0.f;
	R.DmgDealt = St.BossDamageTaken;
	R.DmgTaken = St.PlayerDamageTaken;
	// The counters' sources (HW::FEncounter::StepFrame): commits and outcomes of each side, guard breaks; the player's
	// parry presses and ghoststeps are its PParry / step commits.
	R.PlayerSwings = St.PlayerSwings;
	R.PlayerHits = St.PlayerOutcomes[static_cast<int32>(HW::EHitOutcome::Hit)];
	R.KeeperSwings = St.BossSwings;
	R.KeeperHits = O(HW::EHitOutcome::Hit);
	R.KeeperBlocked = O(HW::EHitOutcome::Blocked);
	R.KeeperParried = O(HW::EHitOutcome::Parried);
	R.KeeperWhiffed = O(HW::EHitOutcome::Whiff);
	R.ParryAttempts = FMath::Max(St.PlayerParryPresses, R.KeeperParried);
	R.Dodges = St.PlayerSteps;
	R.GuardBreaks = St.PlayerGuardBreaks;
	R.KeeperExposed = St.BossExposed;
	if (bRL && bHaveBaseline)
	{
		HWTelemetry::NotebookDelta(Before, Now, R);
		// What it expects you to do against each of its eight attacks (the notebook page's rows), after this fight.
		static const TPair<HW::EMoveId, const TCHAR*> Attacks[] = {
			{ HW::EMoveId::BFastSlash, TEXT("fastSlash") }, { HW::EMoveId::BSweepLeft, TEXT("sweepLeft") },
			{ HW::EMoveId::BSweepRight, TEXT("sweepRight") }, { HW::EMoveId::BHeavyCleave, TEXT("heavyCleave") },
			{ HW::EMoveId::BDelayedHeavy, TEXT("delayedHeavy") }, { HW::EMoveId::BFeintMid, TEXT("feint") },
			{ HW::EMoveId::BGrab, TEXT("grab") }, { HW::EMoveId::BKillerThrust, TEXT("killer") },
		};
		for (const TPair<HW::EMoveId, const TCHAR*>& A : Attacks)
		{
			HW::ESym Sym = HW::ESym::Neutral;
			float Prob = 0.f;
			if (SessionSub != nullptr && SessionSub->PredictAnswer(A.Key, Sym, Prob))
			{
				R.Expected.Add({ A.Value, HWTelemetry::AnswerKey(Sym), Prob });
			}
		}
	}
	return true;
}

void UHWTelemetrySubsystem::RecordFight(const UHWDuelSubsystem& Duel, bool bPlayerWon, bool bTimeout)
{
	FHWFightRecord R;
	FString Skip;
	if (!BuildRecord(Duel, bPlayerWon, bTimeout, R, Skip))
	{
		if (bEnabled) { UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: fight not recorded (%s)."), *Skip); }
		return;
	}
	if (Save == nullptr) { return; }
	FHWTelemetryPending P;
	P.FightId = HWTelemetry::NewId();
	P.ProjectId = ProjectId;
	P.RecordedAt = R.ClientTime.ToIso8601();
	P.Body = HWTelemetry::FightCommitJson(R, ProjectId, P.FightId); // "{uid}" filled at send time
	Save->Pending.Add(P);
	while (Save->Pending.Num() > MaxPending) { Save->Pending.RemoveAt(0); }
	PersistSave();
	UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: fight %d recorded (%s vs %s, %s); %d waiting to upload."), R.FightInSession, *R.Result,
		*R.KeeperName, *R.Brain, Save->Pending.Num());
	Flush();
}

void UHWTelemetrySubsystem::Flush()
{
	if (!bEnabled || bBusy || Save == nullptr) { return; }
	if (Save->Uid.IsEmpty() || Save->RefreshToken.IsEmpty()) { SignUp(); return; }
	if (!HasFreshToken()) { RefreshIdToken(); return; }
	if (!Save->bPlayerCreated) { CreatePlayer(); return; }
	if (!bResetSynced) { SyncReset(); return; }
	const int32 Index = FirstPendingIndex();
	if (Index == INDEX_NONE) { return; }

	const FHWTelemetryPending& Head = Save->Pending[Index];
	bBusy = true;
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(FirestoreDocsUrl() + TEXT(":commit"));
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + IdToken);
	Req->SetContentAsString(Head.Body.Replace(UidSlot, *Save->Uid));
	Req->OnProcessRequestComplete().BindUObject(this, &UHWTelemetrySubsystem::OnCommitDone, Head.FightId);
	Req->ProcessRequest();
}

void UHWTelemetrySubsystem::OnCommitDone(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk, FString FightId)
{
	(void)Req;
	bBusy = false;
	if (Save == nullptr) { return; }
	const int32 Index = Save->Pending.IndexOfByPredicate([&FightId](const FHWTelemetryPending& P) { return P.FightId == FightId; });
	if (Index == INDEX_NONE) { return; }
	const int32 Code = bOk && Resp.IsValid() ? Resp->GetResponseCode() : 0;
	const FString Text = Resp.IsValid() ? Resp->GetContentAsString() : FString();
	if (Code == 200 || Text.Contains(TEXT("ALREADY_EXISTS")))
	{
		// ALREADY_EXISTS: a retry of a fight that already arrived (the commit requires it to be new) — delivered.
		Save->Pending.RemoveAt(Index);
		PersistSave();
		UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: fight uploaded (fights/%s)%s; %d waiting."), *FightId,
			Code == 200 ? TEXT("") : TEXT(" — it had already arrived"), Save->Pending.Num());
		Flush();
		return;
	}
	if (Code == 401)
	{
		IdToken.Reset();   // expired or revoked: refresh, then the queue goes on
		if (++Save->Pending[Index].Attempts < 5) { Flush(); }
		return;
	}
	if (Code == 400 || Code == 403)
	{
		// Refused: the rules may be refusing a retry of a fight that did arrive (the precondition and the rules both say no
		// to a second create), so look first; a public read of fights/{id} settles it.
		CheckFightDelivered(FightId, Code);
		return;
	}
	// Offline, a server hiccup, or a contended commit (409 ABORTED): it stays queued for the next fight or launch.
	PersistSave();
	UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: upload failed (HTTP %d); %d fight(s) wait for the next try."), Code, Save->Pending.Num());
}

void UHWTelemetrySubsystem::CheckFightDelivered(const FString& FightId, int32 Code)
{
	bBusy = true;
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(FirestoreDocsUrl() / TEXT("fights") / FightId);
	Req->SetVerb(TEXT("GET"));
	Req->OnProcessRequestComplete().BindWeakLambda(this, [this, FightId, Code](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		bBusy = false;
		if (Save == nullptr) { return; }
		const int32 Index = Save->Pending.IndexOfByPredicate([&FightId](const FHWTelemetryPending& P) { return P.FightId == FightId; });
		if (Index == INDEX_NONE) { return; }
		const int32 Got = bOk && Resp.IsValid() ? Resp->GetResponseCode() : 0;
		if (Got == 200)
		{
			Save->Pending.RemoveAt(Index);
			PersistSave();
			UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: fight uploaded (fights/%s) — it had already arrived; %d waiting."), *FightId, Save->Pending.Num());
			Flush();
			return;
		}
		if (Got != 404) { PersistSave(); return; }   // could not tell (offline): try again later
		FHWTelemetryPending& Head = Save->Pending[Index];
		++Head.Attempts;
		if (Head.Attempts == 1)
		{
			// Maybe the player document is missing (deleted in the console): make sure it exists, then retry.
			Save->bPlayerCreated = false;
			PersistSave();
			Flush();
			return;
		}
		if (Head.Attempts >= 3)
		{
			// Usually the project's published rules are older than this build's fight schema (v2 since 1.4.0): see web/README.md,
			// "Before distributing a new build".
			UE_LOG(LogHellwalkerRL, Warning, TEXT("telemetry: fight %s refused by the server (HTTP %d) three times; dropped. Are the "
				"Firestore rules published from web/firebase/firestore.rules (fight schema v%d)?"), *FightId, Code, HWTelemetry::FightSchemaVersion);
			Save->Pending.RemoveAt(Index);
			PersistSave();
			Flush();
			return;
		}
		PersistSave();
	});
	Req->ProcessRequest();
}

void UHWTelemetrySubsystem::SyncReset()
{
	// Once per launch, before uploading: if the player reset on the website after some fights were queued, those fights
	// belong to what they asked to forget.
	bBusy = true;
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(FirestoreDocsUrl() / TEXT("players") / Save->Uid);
	Req->SetVerb(TEXT("GET"));
	Req->OnProcessRequestComplete().BindWeakLambda(this, [this](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		bBusy = false;
		if (Save == nullptr) { return; }
		const int32 Code = bOk && Resp.IsValid() ? Resp->GetResponseCode() : 0;
		if (Code == 404)
		{
			Save->bPlayerCreated = false;   // gone: create it again
			Flush();
			return;
		}
		if (Code != 200) { return; }       // offline: the next fight tries again
		bResetSynced = true;
		const TSharedPtr<FJsonObject> Doc = ParseJson(Resp->GetContentAsString());
		const TSharedPtr<FJsonObject>* Fields = nullptr;
		const TSharedPtr<FJsonObject>* ResetAt = nullptr;
		FString When;
		FDateTime ResetTime;
		if (Doc.IsValid() && Doc->TryGetObjectField(TEXT("fields"), Fields) && Fields != nullptr && (*Fields)->TryGetObjectField(TEXT("resetAt"), ResetAt)
			&& ResetAt != nullptr && (*ResetAt)->TryGetStringField(TEXT("timestampValue"), When) && FDateTime::ParseIso8601(*When, ResetTime))
		{
			const int32 Before = Save->Pending.Num();
			Save->Pending.RemoveAll([this, &ResetTime](const FHWTelemetryPending& P)
			{
				FDateTime T;
				return P.ProjectId == ProjectId && FDateTime::ParseIso8601(*P.RecordedAt, T) && T < ResetTime;
			});
			if (Save->Pending.Num() != Before)
			{
				PersistSave();
				UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: %d queued fight(s) predate the reset on the website; dropped."), Before - Save->Pending.Num());
			}
		}
		Flush();
	});
	Req->ProcessRequest();
}

void UHWTelemetrySubsystem::SignUp()
{
	bBusy = true;
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(AuthBase() + TEXT("/accounts:signUp?key=") + FGenericPlatformHttp::UrlEncode(ApiKey));
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetContentAsString(TEXT("{\"returnSecureToken\":true}"));
	Req->OnProcessRequestComplete().BindWeakLambda(this, [this](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		bBusy = false;
		const TSharedPtr<FJsonObject> J = bOk && Resp.IsValid() && Resp->GetResponseCode() == 200 ? ParseJson(Resp->GetContentAsString()) : nullptr;
		FString Uid, Refresh, Id;
		if (J.IsValid() && J->TryGetStringField(TEXT("localId"), Uid) && J->TryGetStringField(TEXT("refreshToken"), Refresh)
			&& J->TryGetStringField(TEXT("idToken"), Id) && Save != nullptr)
		{
			Save->ProjectId = ProjectId;
			Save->Uid = Uid;
			Save->RefreshToken = Refresh;
			Save->bPlayerCreated = false;
			FRandomStream Rng(static_cast<int32>(GetTypeHash(Uid)));
			Save->Nickname = HWTelemetry::MakeNickname(Rng);
			IdToken = Id;
			IdTokenTime = FPlatformTime::Seconds();
			bResetSynced = false;
			PersistSave();
			UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: anonymous player created (%s)."), *Save->Nickname);
			Flush();
			return;
		}
		UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: anonymous sign-in failed (HTTP %d); trying again later."), Resp.IsValid() ? Resp->GetResponseCode() : 0);
	});
	Req->ProcessRequest();
}

void UHWTelemetrySubsystem::RefreshIdToken()
{
	bBusy = true;
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(TokenBase() + TEXT("/token?key=") + FGenericPlatformHttp::UrlEncode(ApiKey));
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));
	Req->SetContentAsString(TEXT("grant_type=refresh_token&refresh_token=") + FGenericPlatformHttp::UrlEncode(Save != nullptr ? Save->RefreshToken : FString()));
	Req->OnProcessRequestComplete().BindWeakLambda(this, [this](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		bBusy = false;
		const int32 Code = bOk && Resp.IsValid() ? Resp->GetResponseCode() : 0;
		const FString Text = Resp.IsValid() ? Resp->GetContentAsString() : FString();
		const TSharedPtr<FJsonObject> J = Code == 200 ? ParseJson(Text) : nullptr;
		FString Id, Refresh;
		if (J.IsValid() && J->TryGetStringField(TEXT("id_token"), Id))
		{
			IdToken = Id;
			IdTokenTime = FPlatformTime::Seconds();
			if (J->TryGetStringField(TEXT("refresh_token"), Refresh) && Save != nullptr && !Refresh.IsEmpty() && Refresh != Save->RefreshToken)
			{
				Save->RefreshToken = Refresh;
				PersistSave();
			}
			Flush();
			return;
		}
		// Only these mean the anonymous account itself is gone; a bad or rotated API key (also a 400) must not cost the
		// player their identity.
		const FString Err = ErrorMessage(Text);
		const bool bAccountGone = Err.StartsWith(TEXT("INVALID_REFRESH_TOKEN")) || Err.StartsWith(TEXT("TOKEN_EXPIRED"))
			|| Err.StartsWith(TEXT("USER_NOT_FOUND")) || Err.StartsWith(TEXT("USER_DISABLED"));
		if (Code == 400 && bAccountGone && Save != nullptr)
		{
			UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: the saved anonymous identity was refused (%s); a new one will be created."), *Err);
			Save->Uid.Reset();
			Save->RefreshToken.Reset();
			Save->bPlayerCreated = false;
			PersistSave();
			return;
		}
		UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: token refresh failed (HTTP %d%s); trying again later."), Code,
			Err.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", %s"), *Err));
	});
	Req->ProcessRequest();
}

void UHWTelemetrySubsystem::CreatePlayer()
{
	if (Save == nullptr) { return; }
	bBusy = true;
	const UGameInstance* GI = GetGameInstance();
	const UHWSettingsSubsystem* Settings = GI != nullptr ? GI->GetSubsystem<UHWSettingsSubsystem>() : nullptr;
	const FString Difficulty = Settings != nullptr ? UHWSettingsSubsystem::DifficultyName(Settings->GetDifficulty()) : FString(TEXT("Hellwalker"));
	if (Save->Nickname.IsEmpty())
	{
		FRandomStream Rng(static_cast<int32>(GetTypeHash(Save->Uid)));
		Save->Nickname = HWTelemetry::MakeNickname(Rng);
	}
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(FirestoreDocsUrl() + TEXT(":commit"));
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + IdToken);
	Req->SetContentAsString(HWTelemetry::PlayerCreateCommitJson(ProjectId, Save->Uid, Save->Nickname, GameVersion, Difficulty));
	Req->OnProcessRequestComplete().BindWeakLambda(this, [this](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		bBusy = false;
		if (Save == nullptr) { return; }
		const int32 Code = bOk && Resp.IsValid() ? Resp->GetResponseCode() : 0;
		const bool bExists = Resp.IsValid() && Resp->GetContentAsString().Contains(TEXT("ALREADY_EXISTS"));
		if (Code == 200 || bExists)
		{
			Save->bPlayerCreated = true;
			PersistSave();
			if (Code == 200) { UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: player page created (%s)."), *Save->Nickname); }
			Flush();
			return;
		}
		if (Code == 401) { IdToken.Reset(); return; }
		if (Code == 400 || Code == 403)
		{
			// The rules refuse a second create of an existing page: look before concluding anything.
			bBusy = true;
			const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Get = FHttpModule::Get().CreateRequest();
			Get->SetURL(FirestoreDocsUrl() / TEXT("players") / Save->Uid);
			Get->SetVerb(TEXT("GET"));
			Get->OnProcessRequestComplete().BindWeakLambda(this, [this](FHttpRequestPtr, FHttpResponsePtr R2, bool bOk2)
			{
				bBusy = false;
				if (Save != nullptr && bOk2 && R2.IsValid() && R2->GetResponseCode() == 200)
				{
					Save->bPlayerCreated = true;
					PersistSave();
					Flush();
					return;
				}
				UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: creating the player page was refused; trying again later."));
			});
			Get->ProcessRequest();
			return;
		}
		UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: creating the player page failed (HTTP %d); trying again later."), Code);
	});
	Req->ProcessRequest();
}

FString UHWTelemetrySubsystem::StatsPageUnavailableReason() const
{
	if (bDevBuildOff) { return TEXT("Stats sharing is off in development builds (the packaged game shares; -HWTelemetryDev turns it on)."); }
	if (!bEnabled) { return TEXT("Stats sharing is not set up in this build."); }
	if (SiteUrl.IsEmpty()) { return TEXT("The website's address is not set in this build."); }
	if (Save == nullptr || Save->RefreshToken.IsEmpty()) { return TEXT("Your page appears once the game has reached the internet."); }
	return FString();
}

bool UHWTelemetrySubsystem::OpenStatsPage() const
{
	if (!StatsPageUnavailableReason().IsEmpty()) { return false; }
	// The site exchanges the token for this player's session, keeps it in that browser and removes it from the address.
	const FString Url = SiteUrl + TEXT("/#/me?t=") + FGenericPlatformHttp::UrlEncode(Save->RefreshToken);
	bool bOpened = false;
#if PLATFORM_WINDOWS
	// Straight to the shell: the engine's LaunchURL logs the address, and this one carries the player's token.
	bOpened = reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", *Url, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
	FString Error;
	FPlatformProcess::LaunchURL(*Url, nullptr, &Error);
	bOpened = Error.IsEmpty();
#endif
	UE_LOG(LogHellwalkerRL, Log, TEXT("telemetry: %s the stats page."), bOpened ? TEXT("opened") : TEXT("could not open"));
	return bOpened;
}
