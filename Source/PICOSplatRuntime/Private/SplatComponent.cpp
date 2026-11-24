/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#include "SplatComponent.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Logging.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AssertionMacros.h"
#include "PICOSplatCullBoxActor.h"
#include "RenderingThread.h"
#include "Rendering/SplatSceneProxy.h"

#if WITH_EDITOR
#include "Engine.h"
#endif

using PICO::Splat::FSplatSceneProxy;

// Defined here to avoid needing scene proxy to be module public.
FPrimitiveSceneProxy* USplatComponent::CreateSceneProxy()
{
	// Note: Unreal expects a new here, and will handle deletion itself.
	return Asset ? new FSplatSceneProxy{*this} : nullptr;
}

void USplatComponent::SetSplatAsset(USplatAsset* NewAsset)
{
	check(IsInGameThread());

	SourceAssetBeforeFiltering = NewAsset;
	FilteredSplatAsset = nullptr;

	if (bEnableSplatFiltering)
	{
		RebuildFilteredSplatData();
		return;
	}

	ApplyRenderAsset(NewAsset);
}

void USplatComponent::ApplyRenderAsset(USplatAsset* NewAsset)
{
	if (Asset == NewAsset)
	{
		return;
	}

	Asset = NewAsset;
	BodySetup = nullptr;

	UpdateBounds();
	RecreatePhysicsState();
	MarkRenderStateDirty();
}

USplatAsset* USplatComponent::GetSourceSplatAsset() const
{
	if (SourceAssetBeforeFiltering)
	{
		return SourceAssetBeforeFiltering;
	}

	if (Asset != FilteredSplatAsset)
	{
		return Asset;
	}

	return nullptr;
}

void USplatComponent::RebuildFilteredSplatData()
{
	check(IsInGameThread());

	USplatAsset* SourceAsset = GetSourceSplatAsset();
	if (!SourceAsset)
	{
		OriginalSplatCount = 0;
		RemovedByOpacityCount = 0;
		RemovedByCullBoxCount = 0;
		FilteredSplatCount = 0;
		return;
	}

	SourceAssetBeforeFiltering = SourceAsset;
	OriginalSplatCount = int32(SourceAsset->GetNumSplats());
	RemovedByOpacityCount = 0;
	RemovedByCullBoxCount = 0;
	FilteredSplatCount = OriginalSplatCount;

	if (!bEnableSplatFiltering)
	{
		FilteredSplatAsset = nullptr;
		ApplyRenderAsset(SourceAsset);
		return;
	}

	TArray<const APICOSplatCullBoxActor*> CullBoxes;
	bool bHasKeepBox = false;
	if (bEnableCullBoxes)
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<APICOSplatCullBoxActor> It(World); It; ++It)
			{
				const APICOSplatCullBoxActor* CullBox = *It;
				if (!CullBox || !CullBox->bEnabled)
				{
					continue;
				}
				if (!CullBox->TargetTag.IsNone() &&
				    !ComponentHasTag(CullBox->TargetTag))
				{
					continue;
				}

				CullBoxes.Add(CullBox);
				bHasKeepBox |=
					CullBox->Mode == EPICOSplatCullBoxMode::KeepInside;
			}
		}
	}

	TArray<uint32> KeptIndices;
	KeptIndices.Reserve(OriginalSplatCount);
	const TConstArrayView<FVector3f> PositionsMeters = SourceAsset->GetPositions();
	const FTransform ComponentTransform = GetComponentTransform();

	for (int32 Index = 0; Index < OriginalSplatCount; ++Index)
	{
		bool bRemove = false;
		if (SourceAsset->GetAlphaLinear(Index) < MinOpacityThreshold)
		{
			++RemovedByOpacityCount;
			bRemove = true;
		}

		if (!bRemove && CullBoxes.Num() > 0 && PositionsMeters.IsValidIndex(Index))
		{
			const FVector WorldPosition =
				ComponentTransform.TransformPosition(
					FVector(100.0f * PositionsMeters[Index]));

			bool bInsideAnyKeepBox = false;
			for (const APICOSplatCullBoxActor* CullBox : CullBoxes)
			{
				const bool bInside = CullBox->ContainsWorldPosition(WorldPosition);
				if (CullBox->Mode == EPICOSplatCullBoxMode::RemoveInside && bInside)
				{
					bRemove = true;
					break;
				}
				if (CullBox->Mode == EPICOSplatCullBoxMode::KeepInside && bInside)
				{
					bInsideAnyKeepBox = true;
				}
			}

			if (bRemove || (bHasKeepBox && !bInsideAnyKeepBox))
			{
				++RemovedByCullBoxCount;
				bRemove = true;
			}
		}

		if (!bRemove)
		{
			KeptIndices.Add(Index);
		}
	}

	FilteredSplatCount = KeptIndices.Num();
	if (KeptIndices.Num() == 0)
	{
		FilteredSplatAsset = nullptr;
		ApplyRenderAsset(nullptr);
		return;
	}

	FilteredSplatAsset = SourceAsset->CreateFilteredCopy(KeptIndices, this);
	if (!FilteredSplatAsset)
	{
		UE_LOG(
			LogPICOSplat,
			Warning,
			TEXT("Failed to build filtered splat data for %s."),
			*GetNameSafe(GetOwner()));
		ApplyRenderAsset(SourceAsset);
		return;
	}

	ApplyRenderAsset(FilteredSplatAsset);
	UE_LOG(
		LogPICOSplat,
		Log,
		TEXT(
			"Rebuilt filtered splat data for %s. Original=%d Filtered=%d RemovedByOpacity=%d RemovedByCullBox=%d"),
		*GetNameSafe(GetOwner()),
		OriginalSplatCount,
		FilteredSplatCount,
		RemovedByOpacityCount,
		RemovedByCullBoxCount);
}

void USplatComponent::ClearSplatFilter()
{
	check(IsInGameThread());

	USplatAsset* SourceAsset = GetSourceSplatAsset();
	FilteredSplatAsset = nullptr;
	bEnableSplatFiltering = false;

	OriginalSplatCount = SourceAsset ? int32(SourceAsset->GetNumSplats()) : 0;
	RemovedByOpacityCount = 0;
	RemovedByCullBoxCount = 0;
	FilteredSplatCount = OriginalSplatCount;

	ApplyRenderAsset(SourceAsset);
}

void USplatComponent::SetGlobalTint(FLinearColor NewTint)
{
	GlobalTint = NewTint;
	MarkRenderStateDirty();
}

void USplatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bEnableSplatFiltering)
	{
		SourceAssetBeforeFiltering = GetSourceSplatAsset();
		RebuildFilteredSplatData();
	}
}

void USplatComponent::SetSplatExposure(float NewExposure)
{
	Exposure = FMath::Max(0.0f, NewExposure);
	MarkRenderStateDirty();
}

void USplatComponent::ApplyDayNightPreset(int32 PresetIndex)
{
	switch (PresetIndex)
	{
	case 1:
		GlobalTint = FLinearColor(1.0f, 0.72f, 0.45f, 1.0f);
		Exposure = 0.8f;
		break;
	case 2:
		GlobalTint = FLinearColor(0.22f, 0.28f, 0.48f, 1.0f);
		Exposure = 0.45f;
		break;
	case 3:
		GlobalTint = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);
		Exposure = 1.0f;
		break;
	case 4:
		GlobalTint = FLinearColor(0.0f, 0.0f, 1.0f, 1.0f);
		Exposure = 1.0f;
		break;
	case 0:
	default:
		GlobalTint = FLinearColor::White;
		Exposure = 1.0f;
		break;
	}

	MarkRenderStateDirty();
}

UBodySetup* USplatComponent::GetBodySetup()
{
	if (!Asset)
	{
		return nullptr;
	}
	else if (!BodySetup)
	{
		BodySetup = NewObject<UBodySetup>();

		TConstArrayView<FVector3f> ConvexHullVertices =
			Asset->GetConvexHullVertices();

		FKConvexElem Convex;
		Convex.VertexData.AddUninitialized(ConvexHullVertices.Num());
		for (int32 Index = 0; Index < ConvexHullVertices.Num(); ++Index)
		{
			Convex.VertexData[Index] = FVector(ConvexHullVertices[Index]);
		}
		Convex.UpdateElemBox();

		FKAggregateGeom AggGeom;
		AggGeom.ConvexElems.Add(Convex);
		BodySetup->AddCollisionFrom(AggGeom);
	}

	return BodySetup;
}

#if WITH_EDITOR
void USplatComponent::GetUsedMaterials(
	TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (bGetDebugMaterials)
	{
		check(GEngine);
		OutMaterials.Add(GEngine->GeomMaterial);
		OutMaterials.Add(GEngine->ShadedLevelColorationUnlitMaterial);
		OutMaterials.Add(GEngine->WireframeMaterial);
	}
}
#endif

FBoxSphereBounds
USplatComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (BodySetup)
	{
		FBoxSphereBounds NewBounds;
		BodySetup->AggGeom.CalcBoxSphereBounds(NewBounds, LocalToWorld);
		return NewBounds;
	}
	else
	{
		return FBoxSphereBounds(
			LocalToWorld.GetLocation(), FVector::ZeroVector, 0.f);
	}
}

#if WITH_EDITOR
void USplatComponent::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	Exposure = FMath::Max(0.0f, Exposure);

	const FName PropertyName =
		PropertyChangedEvent.Property
			? PropertyChangedEvent.Property->GetFName()
			: NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(USplatComponent, Asset))
	{
		SourceAssetBeforeFiltering = Asset;
		FilteredSplatAsset = nullptr;
	}

	const bool bFilteringPropertyChanged =
		PropertyName == GET_MEMBER_NAME_CHECKED(
		                    USplatComponent, bEnableSplatFiltering) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(
		                    USplatComponent, MinOpacityThreshold) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(
		                    USplatComponent, bEnableCullBoxes);
	if (bPreviewFilterInEditor && bFilteringPropertyChanged)
	{
		RebuildFilteredSplatData();
	}
	else
	{
		MarkRenderStateDirty();
	}
}
#endif
