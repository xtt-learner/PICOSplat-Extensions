/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include "GameFramework/Actor.h"
#include "PICOSplatCullBoxActor.generated.h"

class UBoxComponent;

UENUM(BlueprintType)
enum class EPICOSplatCullBoxMode : uint8
{
	RemoveInside UMETA(DisplayName = "Remove Inside"),
	KeepInside UMETA(DisplayName = "Keep Inside"),
};

UCLASS(Blueprintable, meta = (DisplayName = "PICO Splat Cull Box"))
class PICOSPLATRUNTIME_API APICOSplatCullBoxActor final : public AActor
{
	GENERATED_BODY()

public:
	APICOSplatCullBoxActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO Splat|Cull Box")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO Splat|Cull Box")
	EPICOSplatCullBoxMode Mode = EPICOSplatCullBoxMode::RemoveInside;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO Splat|Cull Box")
	FName TargetTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO Splat|Cull Box")
	bool bDrawDebug = true;

	UFUNCTION(BlueprintPure, Category = "PICO Splat|Cull Box")
	UBoxComponent* GetBoxComponent() const { return BoxComponent; }

	UFUNCTION(BlueprintPure, Category = "PICO Splat|Cull Box")
	bool ContainsWorldPosition(const FVector& WorldPosition) const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UPROPERTY(
		VisibleAnywhere,
		BlueprintReadOnly,
		Category = "PICO Splat|Cull Box",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> BoxComponent;
};
