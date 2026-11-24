/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#include "SplatAsset.h"
#include "SplatConstants.h"
#include "SplatSettings.h"

#include "RHIResources.h"
#include "StaticMeshVertexData.h"

using PICO::Splat::FPackedCovMat;
using PICO::Splat::FPackedPos;
using PICO::Splat::MetersToCentimeters;
using PICO::Splat::TSplatStaticBuffer;

void USplatAsset::BeginDestroy()
{
	Super::BeginDestroy();

	// Default Asset will have None for buffers.
	if (Positions)
	{
		BeginReleaseResource(&*Positions);
	}
	if (CovariancesCM)
	{
		BeginReleaseResource(&*CovariancesCM);
	}
	if (Colors)
	{
		BeginReleaseResource(&*Colors);
	}

	ReleaseResourcesFence.BeginFence();
}

void USplatAsset::BeginInit()
{
	check(Positions);
	check(CovariancesCM);
	check(Colors);

	FName Name = FName(GetPathName());

	Positions->SetOwnerName(Name);
	BeginInitResource(&*Positions);
	CovariancesCM->SetOwnerName(Name);
	BeginInitResource(&*CovariancesCM);
	Colors->SetOwnerName(Name);
	BeginInitResource(&*Colors);
}

bool USplatAsset::IsReadyForFinishDestroy()
{
	return ReleaseResourcesFence.IsFenceComplete();
}

void USplatAsset::PostLoad()
{
	Super::PostLoad();

	SetPositionsMetersInternal(PositionsFullPrecision);

	BeginInit();
}

namespace
{
void BuildBoxHullFromPositions(
	TConstArrayView<FVector3f> PositionsMeters,
	TArray<FVector3f>& OutVerticesCM,
	TArray<uint32>& OutIndices)
{
	if (PositionsMeters.Num() == 0)
	{
		return;
	}

	FVector3f MinMeters(std::numeric_limits<float>::max());
	FVector3f MaxMeters(std::numeric_limits<float>::lowest());
	for (const FVector3f& PositionMeters : PositionsMeters)
	{
		MinMeters = MinMeters.ComponentMin(PositionMeters);
		MaxMeters = MaxMeters.ComponentMax(PositionMeters);
	}

	const FVector3f MinCM = MetersToCentimeters * MinMeters;
	const FVector3f MaxCM = MetersToCentimeters * MaxMeters;
	OutVerticesCM = {
		FVector3f(MinCM.X, MinCM.Y, MinCM.Z),
		FVector3f(MaxCM.X, MinCM.Y, MinCM.Z),
		FVector3f(MaxCM.X, MaxCM.Y, MinCM.Z),
		FVector3f(MinCM.X, MaxCM.Y, MinCM.Z),
		FVector3f(MinCM.X, MinCM.Y, MaxCM.Z),
		FVector3f(MaxCM.X, MinCM.Y, MaxCM.Z),
		FVector3f(MaxCM.X, MaxCM.Y, MaxCM.Z),
		FVector3f(MinCM.X, MaxCM.Y, MaxCM.Z),
	};
	OutIndices = {
		0, 2, 1, 0, 3, 2,
		4, 5, 6, 4, 6, 7,
		0, 1, 5, 0, 5, 4,
		1, 2, 6, 1, 6, 5,
		2, 3, 7, 2, 7, 6,
		3, 0, 4, 3, 4, 7,
	};
}
} // namespace

USplatAsset* USplatAsset::CreateFilteredCopy(
	TConstArrayView<uint32> KeptIndices,
	UObject* OuterForFilteredAsset) const
{
	if (!Positions || !CovariancesCM || !Colors)
	{
		return nullptr;
	}

	UObject* Outer = OuterForFilteredAsset ? OuterForFilteredAsset : GetTransientPackage();
	USplatAsset* FilteredAsset = NewObject<USplatAsset>(
		Outer,
		USplatAsset::StaticClass(),
		NAME_None,
		RF_Transient);
	FilteredAsset->NumSplats = KeptIndices.Num();

	FilteredAsset->PositionsFullPrecision.Reserve(KeptIndices.Num());
	for (uint32 SourceIndex : KeptIndices)
	{
		if (!PositionsFullPrecision.IsValidIndex(SourceIndex))
		{
			return nullptr;
		}
		FilteredAsset->PositionsFullPrecision.Add(PositionsFullPrecision[SourceIndex]);
	}

	FilteredAsset->SetPositionsMetersInternal(FilteredAsset->PositionsFullPrecision);

	TStaticMeshVertexData<FPackedCovMat> CovarianceData{/*InNeedsCPUAccess=*/true};
	if (!CovariancesCM->CopyElements(KeptIndices, CovarianceData))
	{
		return nullptr;
	}
	FilteredAsset->CovariancesCM = TSplatStaticBuffer<FPackedCovMat>(std::move(CovarianceData));

	TStaticMeshVertexData<FColor> ColorData{/*InNeedsCPUAccess=*/true};
	if (!Colors->CopyElements(KeptIndices, ColorData))
	{
		return nullptr;
	}
	FilteredAsset->Colors = TSplatStaticBuffer<FColor>(std::move(ColorData));

	BuildBoxHullFromPositions(
		FilteredAsset->PositionsFullPrecision,
		FilteredAsset->ConvexHullVertices,
		FilteredAsset->ConvexHullIndices);

	FilteredAsset->BeginInit();
	return FilteredAsset;
}

float USplatAsset::GetAlphaLinear(uint32 Index) const
{
	if (!Colors)
	{
		return 1.0f;
	}

	FColor Color;
	if (!Colors->GetElement(Index, Color))
	{
		return 1.0f;
	}

	return float(Color.A) / 255.0f;
}

void USplatAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar << NumSplats;

	// We have to support the null case for `UObject::DeclareCustomVersions`,
	// which serializes the default (empty) object. If not, our checks in
	// TSplatStaticBuffer<T>::operator<< will trip.
	if (NumSplats > 0)
	{
		Ar << PositionsFullPrecision;
		Ar << CovariancesCM << Colors;
		Ar << ConvexHullVertices << ConvexHullIndices;
	}
}

#if WITH_EDITOR
void USplatAsset::SetCovariancesQuatScaleMeters(
	const TArray<FQuat4f>& Rotations, const TArray<FVector3f>& ScalesMeters)
{
	check(Rotations.Num() == NumSplats);
	check(ScalesMeters.Num() == NumSplats);

	TStaticMeshVertexData<FPackedCovMat> Data{/*InNeedsCPUAccess=*/true};
	Data.ResizeBuffer(NumSplats);
	for (int32 Index = 0; Index < Data.Num(); ++Index)
	{
		FMatrix44f R = FRotationMatrix44f::Make(Rotations[Index]);
		FMatrix44f S =
			FScaleMatrix44f::Make(MetersToCentimeters * ScalesMeters[Index]);

		// Σ = R * S * S * R^-1.
		// Note: R^-1 = R^T.
		reinterpret_cast<FPackedCovMat*>(Data.GetDataPointer())[Index] =
			FPackedCovMat(R.GetTransposed() * S * S * R);
	}

	CovariancesCM = TSplatStaticBuffer(std::move(Data));
}

void USplatAsset::SetPositionsMeters(TArray<FVector3f>&& PositionsMeters)
{
	// Do not condition this on sorting implementation. This is executed within
	// the editor at import-time, and must be present to be saved to disk and ran
	// with whatever sorting method is in use at runtime.
	PositionsFullPrecision = std::move(PositionsMeters);

	SetPositionsMetersInternal(PositionsFullPrecision);
}
#endif

void USplatAsset::SetPositionsMetersInternal(
	const TArray<FVector3f>& PositionsMeters)
{
	check(PositionsMeters.Num() == NumSplats);

	/**
	 * Find minimum and maximum values for X, Y and Z.
	 * This lets us represent splat positions as unsigned, normized integers
	 * describing a position between the min and max. This is more accurate
	 * at low bit size representations than floating-point.
	 */
	FVector3f PosMaxM(std::numeric_limits<float>::lowest());
	FVector3f PosMinM(std::numeric_limits<float>::max());
	for (const FVector3f& PosM : PositionsMeters)
	{
		PosMaxM = PosMaxM.ComponentMax(PosM);
		PosMinM = PosMinM.ComponentMin(PosM);
	}
	check(PosMaxM.GetMin() > std::numeric_limits<float>::lowest());
	check(PosMinM.GetMax() < std::numeric_limits<float>::max());

	PosScaleCM = MetersToCentimeters * (PosMaxM - PosMinM) / FPackedPos::MAX;
	PosMaxCM = MetersToCentimeters * PosMaxM;
	PosMinCM = MetersToCentimeters * PosMinM;

	TStaticMeshVertexData<FPackedPos> Data{/*InNeedsCPUAccess=*/true};
	Data.ResizeBuffer(NumSplats);
	for (int32 Index = 0; Index < Data.Num(); ++Index)
	{
		reinterpret_cast<FPackedPos*>(Data.GetDataPointer())[Index] =
			(PositionsMeters[Index] - PosMinM) / (PosMaxM - PosMinM);
	}
	Positions = TSplatStaticBuffer(std::move(Data));
}
