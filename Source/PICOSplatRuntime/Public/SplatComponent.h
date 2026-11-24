/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include "Components/PrimitiveComponent.h"
#include "Math/Color.h"
#include "PhysicsEngine/BodySetup.h"
#include "SplatAsset.h"
#include "SplatComponent.generated.h"

/**
 * Component holding a renderable 3DGS model or scene.
 *
 * @see https://dev.epicgames.com/documentation/en-us/unreal-engine/components-in-unreal-engine
 *
 * TODO(seth): I haven't figured out why the BodyInstance's Physics Actor is not
 * being created successfully on device. Until this is resolved, physics won't
 * work on device.
 */
UCLASS()
class USplatComponent final : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO Splat|Filtering")
	bool bEnableSplatFiltering = false;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "PICO Splat|Filtering",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MinOpacityThreshold = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO Splat|Filtering")
	bool bEnableCullBoxes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO Splat|Filtering")
	bool bPreviewFilterInEditor = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PICO Splat|Filtering")
	int32 OriginalSplatCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PICO Splat|Filtering")
	int32 RemovedByOpacityCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PICO Splat|Filtering")
	int32 RemovedByCullBoxCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PICO Splat|Filtering")
	int32 FilteredSplatCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO Splat|Color")
	FLinearColor GlobalTint = FLinearColor::White;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "PICO Splat|Color",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	float Exposure = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "PICO Splat|Color")
	void SetGlobalTint(FLinearColor NewTint);

	UFUNCTION(BlueprintCallable, Category = "PICO Splat|Color")
	void SetSplatExposure(float NewExposure);

	UFUNCTION(BlueprintCallable, Category = "PICO Splat|Color")
	void ApplyDayNightPreset(int32 PresetIndex);

	FLinearColor GetGlobalTint() const { return GlobalTint; }
	float GetSplatExposure() const { return Exposure; }

	//~ Begin UPrimitiveComponent Interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual UBodySetup* GetBodySetup() override;
#if WITH_EDITOR
	// Materials are only used in Editor, for mouse selection and debug views.
	virtual void GetUsedMaterials(
		TArray<UMaterialInterface*>& OutMaterials,
		bool bGetDebugMaterials = false) const override;
#endif
	//~ End UPrimitiveComponent Interface

	//~ Begin USceneComponent Interface
	virtual FBoxSphereBounds
	CalcBounds(const FTransform& LocalToWorld) const override;
#if WITH_EDITOR
	virtual bool ShouldCollideWhenPlacing() const override { return true; }
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End USceneComponent Interface

	/**
	 * Gets the asset this component is tied to, if any.
	 *
	 * @return The asset attached to this component, or nullptr.
	 */
	TObjectPtr<USplatAsset> GetAsset() const { return Asset; }

	/**
	 * Sets the asset rendered by this component and rebuilds render resources.
	 *
	 * Must be called on the game thread.
	 */
	UFUNCTION(BlueprintCallable, Category = "PICO Splat")
	void SetSplatAsset(USplatAsset* NewAsset);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "PICO Splat|Filtering")
	void RebuildFilteredSplatData();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "PICO Splat|Filtering")
	void ClearSplatFilter();

	UFUNCTION(BlueprintPure, Category = "PICO Splat|Filtering")
	int32 GetOriginalSplatCount() const { return OriginalSplatCount; }

	UFUNCTION(BlueprintPure, Category = "PICO Splat|Filtering")
	int32 GetFilteredSplatCount() const { return FilteredSplatCount; }

	/**
	 * Returns the current render proxy pointer for native GS capture requests.
	 *
	 * The pointer is only safe to dereference on the render thread after the
	 * component's render state is created.
	 */
	FPrimitiveSceneProxy* GetCurrentSceneProxyForCapture() const
	{
		return GetSceneProxy();
	}

protected:
	virtual void BeginPlay() override;

private:
	void ApplyRenderAsset(USplatAsset* NewAsset);
	USplatAsset* GetSourceSplatAsset() const;

	UPROPERTY(Category = Splat, EditAnywhere)
	TObjectPtr<USplatAsset> Asset;

	UPROPERTY(Transient)
	TObjectPtr<USplatAsset> SourceAssetBeforeFiltering;

	UPROPERTY(Transient)
	TObjectPtr<USplatAsset> FilteredSplatAsset;

	UPROPERTY()
	TObjectPtr<UBodySetup> BodySetup;

#if WITH_EDITOR
	friend class UActorFactorySplat;
#endif
};
