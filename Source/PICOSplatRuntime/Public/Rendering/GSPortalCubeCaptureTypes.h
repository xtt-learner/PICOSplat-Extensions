/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include "CoreMinimal.h"

class USplatComponent;
class UTextureRenderTargetCube;
class UTextureRenderTarget2D;
class FTextureRenderTargetCubeResource;
class FTextureRenderTargetResource;

namespace PICO::Splat
{

struct FGSPortalCubeFaceViewParams
{
	int32 FaceIndex = 0;
	FString FaceName;

	FVector ViewOrigin = FVector::ZeroVector;
	FVector ViewForward = FVector::ForwardVector;
	FVector ViewUp = FVector::UpVector;
	FVector ViewRight = FVector::RightVector;

	FMatrix ViewMatrix = FMatrix::Identity;
	FMatrix ViewRotationMatrix = FMatrix::Identity;
	FMatrix ProjectionMatrix = FMatrix::Identity;
	FMatrix ViewProjectionMatrix = FMatrix::Identity;
	FMatrix TranslatedWorldToClipMatrix = FMatrix::Identity;
	FVector3f PreViewTranslation = FVector3f::ZeroVector;

	FIntRect ViewRect = FIntRect(0, 0, 512, 512);
	FVector2f FocalLength = FVector2f(512.0f, 512.0f);

	float NearClip = 10.0f;
	float FarClip = 100000.0f;
	int32 Resolution = 512;
};

struct FGSPortalCubeCaptureRequest
{
	TWeakObjectPtr<USplatComponent> TargetSplatComponent;
	TWeakObjectPtr<UTextureRenderTargetCube> TargetRenderTargetCube;
	TWeakObjectPtr<UTextureRenderTarget2D> DebugFaceRenderTarget2D;
	FTextureRenderTargetCubeResource* TargetRenderTargetResource = nullptr;
	FTextureRenderTargetResource* DebugFaceRenderTargetResource = nullptr;

	FTransform CaptureTransform = FTransform::Identity;

	int32 CubeFaceResolution = 512;
	int32 CaptureFaceIndex = INDEX_NONE;
	float NearClip = 10.0f;
	float FarClip = 100000.0f;

	FLinearColor ClearColor = FLinearColor::Black;
	bool bClearBeforeCapture = true;
	bool bLogDebugInfo = true;
	bool bForceDebugFace2DCopy = false;
	bool bForceSolidColorTest = false;
};

} // namespace PICO::Splat
