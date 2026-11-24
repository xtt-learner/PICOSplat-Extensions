/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include "GameFramework/Actor.h"

#include "PICO4DGSActor.generated.h"

class UPICO4DGSComponent;
class USplatComponent;

/**
 * Placeable actor for the minimal cooked-asset 4DGS sequence demo.
 */
UCLASS(Blueprintable, meta = (DisplayName = "PICO 4DGS Actor"))
class PICOSPLATRUNTIME_API APICO4DGSActor final : public AActor
{
	GENERATED_BODY()

public:
	APICO4DGSActor();

	UFUNCTION(BlueprintPure, Category = "PICO 4DGS")
	UPICO4DGSComponent* GetPICO4DGSComponent() const;

	UFUNCTION(BlueprintPure, Category = "PICO 4DGS")
	USplatComponent* GetSplatComponent() const;

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	void Play4DGS();

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	void Pause4DGS();

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	void Stop4DGS();

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	bool SeekFrame(int32 FrameIndex);

private:
	UPROPERTY(
		VisibleAnywhere,
		BlueprintReadOnly,
		Category = "PICO 4DGS",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USplatComponent> SplatComponent;

	UPROPERTY(
		VisibleAnywhere,
		BlueprintReadOnly,
		Category = "PICO 4DGS",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPICO4DGSComponent> PICO4DGSComponent;
};
