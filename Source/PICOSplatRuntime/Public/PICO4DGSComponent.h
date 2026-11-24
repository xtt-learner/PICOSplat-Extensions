/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include "Components/SceneComponent.h"
#include "SplatAsset.h"

#include "PICO4DGSComponent.generated.h"

class USplatComponent;

/**
 * Minimal 4DGS-like player that flips between cooked USplatAsset frames.
 */
UCLASS(ClassGroup = (PICO), meta = (BlueprintSpawnableComponent))
class PICOSPLATRUNTIME_API UPICO4DGSComponent final : public USceneComponent
{
	GENERATED_BODY()

public:
	UPICO4DGSComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO 4DGS")
	TObjectPtr<USplatComponent> TargetSplatComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO 4DGS")
	TArray<TObjectPtr<USplatAsset>> Frames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO 4DGS")
	float FPS = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO 4DGS")
	bool bLoop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO 4DGS")
	bool bPlayOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO 4DGS")
	bool bPrewarmFramesOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO 4DGS")
	int32 StartFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO 4DGS")
	int32 EndFrame = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PICO 4DGS")
	int32 CurrentFrame = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PICO 4DGS")
	bool bIsPlaying = false;

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	void Play4DGS();

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	void Pause4DGS();

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	void Stop4DGS();

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	bool SeekFrame(int32 FrameIndex);

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	void SetPlaybackFPS(float NewFPS);

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	void SetLooping(bool bNewLoop);

	UFUNCTION(BlueprintPure, Category = "PICO 4DGS")
	int32 GetCurrentFrame() const;

	UFUNCTION(BlueprintPure, Category = "PICO 4DGS")
	int32 GetFrameCount() const;

	UFUNCTION(BlueprintPure, Category = "PICO 4DGS")
	bool IsPlaying() const;

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	bool StepForward();

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	bool StepBackward();

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	void SetFrameRange(int32 NewStartFrame, int32 NewEndFrame);

	UFUNCTION(BlueprintCallable, Category = "PICO 4DGS")
	void SetSequenceFrames(const TArray<USplatAsset*>& NewFrames);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool ResolveTargetSplatComponent();
	bool NormalizeFrameRange(int32& OutStartFrame, int32& OutEndFrame) const;
	bool SetFrameInternal(int32 FrameIndex, bool bWarnOnFailure);
	void PrewarmFrames();
	int32 GetNextFrame(int32 Direction) const;

	float AccumulatedFrameTime = 0.0f;
};
