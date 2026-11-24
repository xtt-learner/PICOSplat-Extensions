/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "GSPortalCubeCaptureActor.generated.h"

class USplatComponent;
class UTextureRenderTargetCube;
class UTextureRenderTarget2D;

UCLASS(Blueprintable, BlueprintType)
class PICOSPLATRUNTIME_API AGSPortalCubeCaptureActor final : public AActor
{
	GENERATED_BODY()

public:
	AGSPortalCubeCaptureActor();

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture",
		meta = (DisplayName = "Target Splat Actor"))
	TObjectPtr<AActor> TargetSplatActor = nullptr;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture",
		meta = (DisplayName = "Target Splat Actor Name"))
	FString TargetSplatActorName;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture",
		meta = (DisplayName = "Target Splat Component"))
	TObjectPtr<USplatComponent> TargetSplatComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Portal Cube Capture")
	TObjectPtr<UTextureRenderTargetCube> TargetRenderTargetCube = nullptr;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture|Debug",
		meta = (DisplayName = "Debug Face Render Target 2D"))
	TObjectPtr<UTextureRenderTarget2D> DebugFaceRenderTarget2D = nullptr;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture",
		meta = (ClampMin = "64", UIMin = "256", UIMax = "2048"))
	int32 CubeFaceResolution = 512;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture",
		meta = (ClampMin = "0.1", UIMin = "1.0"))
	float NearClip = 10.0f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture",
		meta = (ClampMin = "100.0", UIMin = "1000.0"))
	float FarClip = 100000.0f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture|Auto Capture",
		meta = (DisplayName = "Capture On Begin Play"))
	bool bCaptureOnBeginPlay = false;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture|Auto Capture",
		meta = (DisplayName = "Capture On Editor Map Opened"))
	bool bCaptureOnEditorMapOpened = false;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture|Auto Capture",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	float AutoCaptureDelay = 1.0f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture|Auto Capture",
		meta = (DisplayName = "Recapture On Property Changed"))
	bool bRecaptureOnPropertyChanged = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Portal Cube Capture|Auto Capture")
	bool bCaptureEveryFrame = false;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture|Auto Capture",
		meta = (ClampMin = "0.01", UIMin = "0.05", UIMax = "1.0"))
	float CaptureInterval = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Portal Cube Capture")
	FLinearColor ClearColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GS Portal Cube Capture")
	bool bClearBeforeCapture = true;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture",
		meta = (DisplayName = "Auto Find Splat Actor"))
	bool bAutoFindSplatActor = true;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture",
		meta = (DisplayName = "Log Debug Info"))
	bool bLogDebugInfo = true;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "GS Portal Cube Capture|Debug",
		meta = (DisplayName = "Force Solid Color Test"))
	bool bForceSolidColorTest = false;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "GS Portal Cube Capture")
	void CaptureOnce();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "GS Portal Cube Capture")
	void CapturePortalCube();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "GS Portal Cube Capture")
	void CaptureFace(int32 FaceIndex);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "GS Portal Cube Capture")
	void CaptureAllFacesSequential();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "GS Portal Cube Capture")
	bool ResolveTargetSplat();

	UFUNCTION(BlueprintCallable, Category = "GS Portal Cube Capture")
	void CaptureAtTransform(const FTransform& InCaptureTransform);

	UFUNCTION(BlueprintCallable, Category = "GS Portal Cube Capture")
	void SetTargetSplatComponent(USplatComponent* InComponent);

	UFUNCTION(BlueprintCallable, Category = "GS Portal Cube Capture")
	void SetTargetRenderTargetCube(UTextureRenderTargetCube* InRenderTargetCube);

	UFUNCTION(BlueprintCallable, Category = "GS Portal Cube Capture")
	bool IsCaptureValid() const;

	UFUNCTION(BlueprintCallable, Category = "GS Portal Cube Capture")
	FString GetLastCaptureStatus() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostRegisterAllComponents() override;
	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif

private:
	bool ValidateCaptureInputs(FString& OutStatus) const;
	bool CanAutoCaptureInCurrentWorld() const;
	void ScheduleAutoCapture(const TCHAR* Reason);
	void CancelScheduledAutoCapture();
	void ExecuteScheduledAutoCapture();
	void SubmitCaptureRequest(
		const FTransform& InCaptureTransform,
		int32 CaptureFaceIndex = INDEX_NONE,
		bool bForceDebugFace2DCopy = false);
	void SetLastCaptureStatus(const FString& InStatus);
	void AddPortalScreenDebugMessage(
		const FString& Message,
		bool bError = false,
		float Duration = 8.0f) const;

	UPROPERTY(VisibleAnywhere, Category = "GS Portal Cube Capture")
	FString LastCaptureStatus;

	FTimerHandle AutoCaptureTimerHandle;
#if WITH_EDITOR
	FTSTicker::FDelegateHandle EditorAutoCaptureTickerHandle;
#endif

	float TimeSinceLastCapture = 0.0f;
};
