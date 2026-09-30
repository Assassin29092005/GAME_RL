#include "HWWorldGen.h"

namespace
{
	double Noise(const FVector2D& P) { return static_cast<double>(FMath::PerlinNoise2D(P)); }

	double Smooth(double A, double B, double X) { return FMath::SmoothStep(A, B, X); }

	/** Distance from P to segment AB, and the parameter along it. */
	double SegmentDistance(const FVector2D& P, const FVector2D& A, const FVector2D& B, double& OutT)
	{
		const FVector2D AB = B - A;
		const double Len2 = FMath::Max(AB.SizeSquared(), 1.0);
		OutT = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / Len2, 0.0, 1.0);
		return FVector2D::Distance(P, A + AB * OutT);
	}

	constexpr double M = 100.0; // metres -> cm
}

const TArray<FHWShrineSpec>& FHWWorldGen::Shrines()
{
	static const TArray<FHWShrineSpec> All = []()
	{
		TArray<FHWShrineSpec> S;
		FHWShrineSpec W;
		W.Id = TEXT("Shrine_Warden");
		W.Title = TEXT("The Ninefold Warden");
		W.Epithet = TEXT("keeper of the first seal");
		W.Cast = TEXT("Sevarog");
		W.Script = 0;
		S.Add(W);
		FHWShrineSpec Sage;
		Sage.Id = TEXT("Shrine_Sage");
		Sage.Title = TEXT("The Monkey Sage");
		Sage.Epithet = TEXT("he has watched a thousand wanderers fall");
		Sage.Cast = TEXT("Wukong");
		Sage.Script = 1;
		Sage.HealthScale = 0.9f;
		S.Add(Sage);
		FHWShrineSpec Gate;
		Gate.Id = TEXT("Shrine_Gate");
		Gate.Title = TEXT("The Warden, Returned");
		Gate.Epithet = TEXT("reborn in stone, it remembers every blow you struck");
		Gate.Cast = TEXT("Golem");
		Gate.Script = 0;
		Gate.HealthScale = 1.25f;
		Gate.bFinal = true;
		S.Add(Gate);
		return S;
	}();
	return All;
}

FHWWorldGen::FHWWorldGen(int32 InSeed)
	: Seed(InSeed)
{
	// Perlin has no seed: the seed moves the sample window instead.
	NoiseOffset = FVector2D(37.17 * (Seed % 97), 53.91 * ((Seed * 7) % 89)) * 1000.0;
	Layout();
}

void FHWWorldGen::Layout()
{
	auto Add = [this](EHWSiteKind Kind, const TCHAR* Id, const TCHAR* Title, double Xm, double Ym, double Rm, int32 Index)
	{
		FHWSite S;
		S.Kind = Kind;
		S.Id = Id;
		S.Title = Title;
		S.Pos = FVector2D(Xm, Ym) * M;
		S.Radius = Rm * M;
		S.Index = Index;
		S.Height = LowFrequency(S.Pos.X, S.Pos.Y);
		return Sites.Add(S);
	};
	// Bells (checkpoints). Bell 0 is where a new game begins.
	const int32 B0 = Add(EHWSiteKind::Bell, TEXT("Bell_AshenGate"), TEXT("Bell of the Ashen Gate"), -860, -820, 11, 0);
	const int32 B1 = Add(EHWSiteKind::Bell, TEXT("Bell_Crossroads"), TEXT("Crossroads Bell"), -50, -120, 36, 1); // the hamlet's plaza
	const int32 B2 = Add(EHWSiteKind::Bell, TEXT("Bell_Ridge"), TEXT("Bell of the High Ridge"), -350, 620, 11, 2);
	const int32 B3 = Add(EHWSiteKind::Bell, TEXT("Bell_Summit"), TEXT("Summit Bell"), 560, 520, 11, 3);
	// Shrines (boss duels) — the order of FHWWorldGen::Shrines().
	const TArray<FHWShrineSpec>& Specs = Shrines();
	const int32 S0 = Add(EHWSiteKind::Shrine, *Specs[0].Id.ToString(), *Specs[0].Title, 380, -720, 19, 0);
	const int32 S1 = Add(EHWSiteKind::Shrine, *Specs[1].Id.ToString(), *Specs[1].Title, -780, 480, 19, 1);
	const int32 S2 = Add(EHWSiteKind::Shrine, *Specs[2].Id.ToString(), *Specs[2].Title, 820, 820, 21, 2);
	// Ruins (traversal).
	const int32 R0 = Add(EHWSiteKind::Ruin, TEXT("Ruin_Wall"), TEXT("The Broken Wall"), -450, -420, 12, 0);
	const int32 R1 = Add(EHWSiteKind::Ruin, TEXT("Ruin_Steps"), TEXT("The Drowned Steps"), 620, -150, 12, 1);
	const int32 R2 = Add(EHWSiteKind::Ruin, TEXT("Ruin_Gallery"), TEXT("The Hollow Gallery"), -120, 760, 12, 2);
	const int32 R3 = Add(EHWSiteKind::Ruin, TEXT("Ruin_Court"), TEXT("The Silent Court"), 180, 180, 12, 3);
	const int32 R4 = Add(EHWSiteKind::Ruin, TEXT("Ruin_Watch"), TEXT("The Western Watch"), -920, 20, 12, 4);
	const int32 R5 = Add(EHWSiteKind::Ruin, TEXT("Ruin_Stair"), TEXT("The Long Stair"), 900, 250, 12, 5);

	Links = {
		{ B0, R0 }, { R0, B1 }, { B1, S0 }, { S0, R1 }, { R1, R5 }, { R5, B3 },
		{ B1, R3 }, { R3, B3 }, { B3, S2 },
		{ B0, R4 }, { R4, S1 }, { S1, B2 }, { B2, R2 }, { R2, R3 } };
}

const FHWSite* FHWWorldGen::FindSite(FName Id) const
{
	return Sites.FindByPredicate([Id](const FHWSite& S) { return S.Id == Id; });
}

double FHWWorldGen::LowFrequency(double X, double Y) const
{
	const FVector2D P = FVector2D(X, Y) + NoiseOffset;
	const double Hills = Noise(P / 60000.0) * 2600.0;
	const double Ridge = FMath::Square(1.0 - FMath::Abs(Noise(P / 42000.0 + FVector2D(13.1, 7.7)))) * 3200.0;
	return Hills + Ridge * 0.7;
}

double FHWWorldGen::Raw(double X, double Y) const
{
	const FVector2D P = FVector2D(X, Y) + NoiseOffset;
	const double Detail = Noise(P / 9000.0 + FVector2D(5.5, 1.3)) * 260.0 + Noise(P / 2500.0 + FVector2D(2.1, 8.4)) * 60.0;
	// The valley is walled by mountains: nothing to walk off, and a horizon from every plaza.
	const double R = FMath::Max(FMath::Abs(X), FMath::Abs(Y)) / HalfExtent;
	const double Wall = Smooth(0.78, 1.0, R) * (26000.0 + 9000.0 * Noise(P / 20000.0 + FVector2D(3.3, 9.9)));
	return LowFrequency(X, Y) + Detail + Wall;
}

double FHWWorldGen::PathDistance(double X, double Y, double* OutPathHeight) const
{
	const FVector2D P(X, Y);
	double Best = TNumericLimits<double>::Max();
	double BestHeight = 0.0;
	for (const FIntPoint& L : Links)
	{
		const FHWSite& A = Sites[L.X];
		const FHWSite& B = Sites[L.Y];
		double T = 0.0;
		const double D = SegmentDistance(P, A.Pos, B.Pos, T);
		if (D < Best)
		{
			Best = D;
			BestHeight = 0.5 * FMath::Lerp(A.Height, B.Height, T) + 0.5 * LowFrequency(X, Y);
		}
	}
	if (OutPathHeight != nullptr) { *OutPathHeight = BestHeight; }
	return Best;
}

double FHWWorldGen::SiteDistance(double X, double Y) const
{
	double Best = TNumericLimits<double>::Max();
	for (const FHWSite& S : Sites)
	{
		Best = FMath::Min(Best, FVector2D::Distance(FVector2D(X, Y), S.Pos) - S.Radius);
	}
	return Best;
}

double FHWWorldGen::Height(double X, double Y) const
{
	double H = Raw(X, Y);
	double PathHeight = 0.0;
	const double D = PathDistance(X, Y, &PathHeight);
	if (D < 1800.0)
	{
		H = FMath::Lerp(H, PathHeight, (1.0 - Smooth(400.0, 1800.0, D)) * 0.92);
	}
	for (const FHWSite& S : Sites)
	{
		const double Ds = FVector2D::Distance(FVector2D(X, Y), S.Pos);
		if (Ds < S.Radius * 2.4)
		{
			H = FMath::Lerp(H, S.Height, 1.0 - Smooth(S.Radius, S.Radius * 2.4, Ds));
		}
	}
	return H;
}

FVector FHWWorldGen::Normal(double X, double Y) const
{
	constexpr double E = 100.0;
	const double Dx = Height(X + E, Y) - Height(X - E, Y);
	const double Dy = Height(X, Y + E) - Height(X, Y - E);
	return FVector(-Dx, -Dy, 2.0 * E).GetSafeNormal();
}

FLinearColor FHWWorldGen::GroundColor(double X, double Y) const
{
	return GroundColorFrom(X, Y, Height(X, Y), Normal(X, Y));
}

FLinearColor FHWWorldGen::GroundColorFrom(double X, double Y, double H, const FVector& N) const
{
	const FVector2D P = FVector2D(X, Y) + NoiseOffset;
	const double Dry = 0.5 + 0.5 * Noise(P / 14000.0 + FVector2D(4.2, 0.7));
	FLinearColor C = FMath::Lerp(FLinearColor(0.055f, 0.065f, 0.035f), FLinearColor(0.15f, 0.105f, 0.06f), static_cast<float>(Dry));
	// Ash on the heights, rock on the slopes.
	C = FMath::Lerp(C, FLinearColor(0.2f, 0.19f, 0.19f), static_cast<float>(Smooth(4500.0, 12000.0, H)));
	C = FMath::Lerp(C, FLinearColor(0.12f, 0.11f, 0.105f), static_cast<float>(Smooth(0.12, 0.3, 1.0 - N.Z)));
	// Paths and plazas.
	const double Pd = PathDistance(X, Y);
	C = FMath::Lerp(C, FLinearColor(0.2f, 0.155f, 0.11f), static_cast<float>(1.0 - Smooth(250.0, 600.0, Pd)));
	const double Sd = SiteDistance(X, Y);
	C = FMath::Lerp(C, FLinearColor(0.13f, 0.13f, 0.14f), static_cast<float>(1.0 - Smooth(-150.0, 100.0, Sd)));
	C.A = 1.f;
	return C;
}

FLinearColor FHWWorldGen::GroundLayersFrom(double X, double Y, double H, const FVector& N) const
{
	const FVector2D P = FVector2D(X, Y) + NoiseOffset;
	const double Slope = 1.0 - N.Z;
	double Rock = Smooth(0.1, 0.26, Slope);
	const double Path = FMath::Max(1.0 - Smooth(250.0, 700.0, PathDistance(X, Y)), 1.0 - Smooth(-150.0, 150.0, SiteDistance(X, Y)));
	const double Dry = 0.5 + 0.5 * Noise(P / 14000.0 + FVector2D(4.2, 0.7));
	double Ash = FMath::Max(Smooth(4500.0, 11000.0, H), Smooth(0.55, 0.9, Dry) * 0.7);
	double Dirt = Path;
	Rock *= (1.0 - Dirt);
	Ash *= (1.0 - Rock) * (1.0 - Dirt);
	const double Grass = FMath::Max(0.0, 1.0 - Rock - Dirt - Ash);
	const double Sum = FMath::Max(Rock + Grass + Dirt + Ash, 1e-3);
	return FLinearColor(static_cast<float>(Rock / Sum), static_cast<float>(Grass / Sum), static_cast<float>(Dirt / Sum), static_cast<float>(Ash / Sum));
}

bool FHWWorldGen::IsClear(double X, double Y, double Margin) const
{
	const double Limit = HalfExtent * 0.8;
	if (FMath::Abs(X) > Limit || FMath::Abs(Y) > Limit) { return false; }
	if (PathDistance(X, Y) < 700.0 + Margin) { return false; }
	if (SiteDistance(X, Y) < 400.0 + Margin) { return false; }
	return Normal(X, Y).Z > 0.82;
}

double FHWWorldGen::Hash01(int32 A, int32 B, int32 Salt) const
{
	uint32 H = static_cast<uint32>(A) * 0x8da6b343u ^ static_cast<uint32>(B) * 0xd8163841u ^ static_cast<uint32>(Salt + Seed * 31) * 0xcb1ab31fu;
	H ^= H >> 13;
	H *= 0x5bd1e995u;
	H ^= H >> 15;
	return static_cast<double>(H & 0xFFFFFF) / static_cast<double>(0x1000000);
}
