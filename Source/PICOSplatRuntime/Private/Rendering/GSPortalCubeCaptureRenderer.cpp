/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#include "Rendering/GSPortalCubeCaptureRenderer.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTargetCube.h"
#include "GPUSort.h"
#include "HAL/IConsoleManager.h"
#include "Logging.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHI.h"
#include "SplatComponent.h"
#include "SplatConstants.h"
#include "SplatRenderingUtilities.h"
#include "SplatSceneProxy.h"
#include "SplatShaders.h"
#include "TextureResource.h"
#include "SceneView.h"

static TAutoConsoleVariable<int32> CVarGSPortalCubeDebugSolidColor(
	TEXT("pico.GSPortalCube.DebugSolidColor"),
	0,
	TEXT(
		"Debug GSPortalCubeCapture by skipping splat draw and copying a solid cleared face color. 0=off, 1=on."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGSPortalCubeDebugDisablePortalDraw(
	TEXT("pico.GSPortalCube.DebugDisablePortalDraw"),
	0,
	TEXT(
		"Debug GSPortalCubeCapture by disabling the portal splat draw while keeping clear and CopyTexture. 0=off, 1=on."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGSPortalCubeDebugOpaqueBlend(
	TEXT("pico.GSPortalCube.DebugOpaqueBlend"),
	0,
	TEXT(
		"Debug GSPortalCubeCapture by using opaque overwrite blend instead of the normal GS alpha blend. 0=normal alpha blend, 1=opaque overwrite."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGSPortalCubeDebugSingleFaceIndex(
	TEXT("pico.GSPortalCube.DebugSingleFaceIndex"),
	-1,
	TEXT(
		"Debug GSPortalCubeCapture by rendering only one cube face. -1=all faces, 0..5=single face."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGSPortalCubeDebugDisableSorting(
	TEXT("pico.GSPortalCube.DebugDisableSorting"),
	0,
	TEXT(
		"Debug GSPortalCubeCapture by skipping GPU sorting and using the identity indices written by the distance pass. 0=sort, 1=disable sorting."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarGSPortalCubeDebugFocalScale(
	TEXT("pico.GSPortalCube.DebugFocalScale"),
	1.0f,
	TEXT(
		"Debug GSPortalCubeCapture by scaling the focal length passed to the transform compute shader."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGSPortalCubeDebugProjectionPoints(
	TEXT("pico.GSPortalCube.DebugProjectionPoints"),
	0,
	TEXT(
		"Debug GSPortalCubeCapture by logging projected NDC coordinates for forward/right/up test points."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGSPortalCubeDebugCentersOnly(
	TEXT("pico.GSPortalCube.DebugCentersOnly"),
	0,
	TEXT(
		"Debug GSPortalCubeCapture by drawing each splat center as a tiny fixed point, bypassing computed ellipse transforms. 0=off, 1=on."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarGSPortalCubeDebugFixedSplatRadius(
	TEXT("pico.GSPortalCube.DebugFixedSplatRadius"),
	0.0f,
	TEXT(
		"Debug GSPortalCubeCapture by drawing fixed screen-radius splats in pixels, bypassing computed ellipse transforms. 0=off."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGSPortalCubeDebugTransformStats(
	TEXT("pico.GSPortalCube.DebugTransformStats"),
	0,
	TEXT(
		"Debug GSPortalCubeCapture by reading back PortalTransforms and logging transform scale statistics after CaptureOnce. 0=off, 1=on."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarGSPortalCubeDebugSplatOffsetScale(
	TEXT("pico.GSPortalCube.DebugSplatOffsetScale"),
	1.0f,
	TEXT(
		"Debug GSPortalCubeCapture by scaling the final vertex shader splat offset. 1=normal."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGSPortalCubeDebugVisibilityStats(
	TEXT("pico.GSPortalCube.DebugVisibilityStats"),
	0,
	TEXT(
		"Debug GSPortalCubeCapture by logging per-face view-space and clip-space visibility stats. 0=off, 1=on."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGSPortalCubeDebugBranchColor(
	TEXT("pico.GSPortalCube.DebugBranchColor"),
	0,
	TEXT(
		"Debug GSPortalCubeCapture vertex shader branches. 0=off, 1=CentersOnly red, FixedRadius green, Transform blue, culled black."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarGSPortalCubeDebugFrustumMargin(
	TEXT("pico.GSPortalCube.DebugFrustumMargin"),
	0.25f,
	TEXT(
		"Extra NDC margin used by GSPortalCubeCapture face culling. 0.25 keeps edge splats from popping too early."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarGSPortalCubeMaxSplatScale(
	TEXT("pico.GSPortalCube.MaxSplatScale"),
	1024.0f,
	TEXT(
		"Maximum per-axis splat transform scale in pixels for GSPortalCubeCapture. Larger / non-finite splats are culled."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGSPortalCubeDebugFace2DIndex(
	TEXT("pico.GSPortalCube.DebugFace2DIndex"),
	-1,
	TEXT(
		"When DebugFaceRenderTarget2D is assigned, copy this cube face into the 2D render target for direct face inspection. -1=off, 0..5=face index."),
	ECVF_RenderThreadSafe);

namespace PICO::Splat
{
FString GetGSPortalCubeCaptureDebugState_GameThread()
{
	return FString::Printf(
		TEXT("DebugSingleFaceIndex=%d DebugSolidColor=%d DebugDisablePortalDraw=%d DebugCentersOnly=%d DebugFixedSplatRadius=%.3f DebugBranchColor=%d DebugOpaqueBlend=%d DebugDisableSorting=%d DebugSplatOffsetScale=%.3f DebugFocalScale=%.3f DebugFace2DIndex=%d"),
		CVarGSPortalCubeDebugSingleFaceIndex.GetValueOnGameThread(),
		CVarGSPortalCubeDebugSolidColor.GetValueOnGameThread(),
		CVarGSPortalCubeDebugDisablePortalDraw.GetValueOnGameThread(),
		CVarGSPortalCubeDebugCentersOnly.GetValueOnGameThread(),
		CVarGSPortalCubeDebugFixedSplatRadius.GetValueOnGameThread(),
		CVarGSPortalCubeDebugBranchColor.GetValueOnGameThread(),
		CVarGSPortalCubeDebugOpaqueBlend.GetValueOnGameThread(),
		CVarGSPortalCubeDebugDisableSorting.GetValueOnGameThread(),
		CVarGSPortalCubeDebugSplatOffsetScale.GetValueOnGameThread(),
		CVarGSPortalCubeDebugFocalScale.GetValueOnGameThread(),
		CVarGSPortalCubeDebugFace2DIndex.GetValueOnGameThread());
}

namespace
{
BEGIN_SHADER_PARAMETER_STRUCT(FGSPortalSortProducerParameters, )
SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, IndicesUAV)
SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Indices2UAV)
SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Distances2UAV)
END_SHADER_PARAMETER_STRUCT()

void PortalScreenMessageAnyThread(
	const FString& Message,
	bool bError = false,
	float Duration = 8.0f)
{
	AsyncTask(
		ENamedThreads::GameThread,
		[Message, bError, Duration]()
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(
					-1,
					Duration,
					bError ? FColor::Red : FColor::Cyan,
					FString::Printf(TEXT("[GSPortalCube RT] %s"), *Message));
			}
		});
}

BEGIN_SHADER_PARAMETER_STRUCT(FGSPortalSortParameters, )
SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, IndicesSRV)
SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, IndicesUAV)
SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, Indices2SRV)
SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Indices2UAV)
SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DistancesSRV)
SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DistancesUAV)
SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, Distances2SRV)
SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Distances2UAV)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FGSPortalRenderSplatDeps, )
SHADER_PARAMETER_STRUCT_INCLUDE(
	PICO::Splat::Shaders::FGSPortalRenderSplatVS::FParameters,
	VS)
SHADER_PARAMETER_STRUCT_INCLUDE(
	PICO::Splat::Shaders::FRenderSplatPS::FParameters,
	PS)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FGSPortalReadbackParameters, )
END_SHADER_PARAMETER_STRUCT()

enum EGSPortalVisibilityFlags : uint32
{
	GSPortalVis_ZLEZero = 1u << 0,
	GSPortalVis_ZLENear = 1u << 1,
	GSPortalVis_ZGTFar = 1u << 2,
	GSPortalVis_PosNaNInf = 1u << 3,
	GSPortalVis_InsideClip = 1u << 4,
	GSPortalVis_OutsideClip = 1u << 5,
	GSPortalVis_NDCValid = 1u << 6,
	GSPortalVis_ScaleNaNInf = 1u << 7,
	GSPortalVis_ScaleGT512 = 1u << 8,
	GSPortalVis_ScaleGT2048 = 1u << 9,
	GSPortalVis_ScaleGT10000 = 1u << 10,
	GSPortalVis_ScaleGTMax = 1u << 11
};

struct FGSPortalCubeSplatBuffers
{
	TUniquePtr<FSplatGPUToGPUBuffer> PortalTransforms;
	TUniquePtr<FSplatGPUToGPUBuffer> PortalIndices;

	void Init(FRHICommandListBase& RHICmdList, uint32 NumSplats)
	{
		PortalTransforms =
			MakeUnique<FSplatGPUToGPUBuffer>(NumSplats, PF_FloatRGBA);
		PortalIndices =
			MakeUnique<FSplatGPUToGPUBuffer>(NumSplats, PF_R32_UINT);

		PortalTransforms->InitResource(RHICmdList);
		PortalIndices->InitResource(RHICmdList);
	}

	void Release()
	{
		if (PortalTransforms)
		{
			PortalTransforms->ReleaseResource();
			PortalTransforms.Reset();
		}
		if (PortalIndices)
		{
			PortalIndices->ReleaseResource();
			PortalIndices.Reset();
		}
	}
};

struct FGSPortalCubeFaceBasis
{
	const TCHAR* FaceName = TEXT("Unknown");
	FVector Direction = FVector::ForwardVector;
	FVector Up = FVector::UpVector;
};

struct FGSPortalTransformStatsReadback
{
	TUniquePtr<FRHIGPUBufferReadback> Readback;
	uint32 NumSplats = 0;
	int32 FaceIndex = 0;
};

struct FGSPortalVisibilityStatsReadback
{
	TUniquePtr<FRHIGPUBufferReadback> ViewZReadback;
	TUniquePtr<FRHIGPUBufferReadback> FlagsReadback;
	uint32 NumSplats = 0;
	int32 FaceIndex = 0;
};

uint32 NumThreadGroups(uint32 NumElements)
{
	return (NumElements + (Shaders::THREAD_GROUP_SIZE_X - 1)) /
	       Shaders::THREAD_GROUP_SIZE_X;
}

FString VecToString(const FVector& Vector)
{
	return FString::Printf(
		TEXT("(%.3f, %.3f, %.3f)"),
		Vector.X,
		Vector.Y,
		Vector.Z);
}

FGSPortalCubeFaceBasis GetCubeFaceBasis(int32 FaceIndex)
{
	static const FVector XAxis(1.0f, 0.0f, 0.0f);
	static const FVector YAxis(0.0f, 1.0f, 0.0f);
	static const FVector ZAxis(0.0f, 0.0f, 1.0f);

	switch (FaceIndex)
	{
	case CubeFace_PosX:
		return {TEXT("+X"), XAxis, YAxis};
	case CubeFace_NegX:
		return {TEXT("-X"), -XAxis, YAxis};
	case CubeFace_PosY:
		return {TEXT("+Y"), YAxis, -ZAxis};
	case CubeFace_NegY:
		return {TEXT("-Y"), -YAxis, ZAxis};
	case CubeFace_PosZ:
		return {TEXT("+Z"), ZAxis, YAxis};
	case CubeFace_NegZ:
		return {TEXT("-Z"), -ZAxis, YAxis};
	default:
		return {};
	}
}

int32 GetCubeSliceIndexForFace(int32 FaceIndex)
{
	// Keep the mapping centralized so UE cubemap orientation can be calibrated
	// later without touching render/copy code.
	switch (FaceIndex)
	{
	case CubeFace_PosX:
		return 0;
	case CubeFace_NegX:
		return 1;
	case CubeFace_PosY:
		return 2;
	case CubeFace_NegY:
		return 3;
	case CubeFace_PosZ:
		return 4;
	case CubeFace_NegZ:
		return 5;
	default:
		return FaceIndex;
	}
}

FLinearColor GetDebugSolidColorForFace(int32 FaceIndex)
{
	switch (FaceIndex)
	{
	case CubeFace_PosX:
		return FLinearColor::Red;
	case CubeFace_NegX:
		return FLinearColor(0.0f, 1.0f, 1.0f, 1.0f);
	case CubeFace_PosY:
		return FLinearColor::Green;
	case CubeFace_NegY:
		return FLinearColor(1.0f, 0.0f, 1.0f, 1.0f);
	case CubeFace_PosZ:
		return FLinearColor::Blue;
	case CubeFace_NegZ:
		return FLinearColor::Yellow;
	default:
		return FLinearColor::White;
	}
}

FMatrix BuildPortalPerspectiveMatrix(float HalfFOVRadians, float NearClip)
{
	FMatrix ProjectionMatrix;
	if ((int32)ERHIZBuffer::IsInverted)
	{
		ProjectionMatrix = FReversedZPerspectiveMatrix(
			HalfFOVRadians,
			HalfFOVRadians,
			1.0f,
			1.0f,
			NearClip,
			NearClip);
	}
	else
	{
		ProjectionMatrix = FPerspectiveMatrix(
			HalfFOVRadians,
			HalfFOVRadians,
			1.0f,
			1.0f,
			NearClip,
			NearClip);
	}

	return AdjustProjectionMatrixForRHI(ProjectionMatrix);
}

FGSPortalCubeFaceViewParams BuildFaceViewParams(
	const FGSPortalCubeCaptureRequest& Request,
	int32 FaceIndex)
{
	const FGSPortalCubeFaceBasis Basis = GetCubeFaceBasis(FaceIndex);
	const FQuat CaptureRotation = Request.CaptureTransform.GetRotation();

	FGSPortalCubeFaceViewParams FaceView;
	FaceView.FaceIndex = FaceIndex;
	FaceView.FaceName = Basis.FaceName;
	FaceView.ViewOrigin = Request.CaptureTransform.GetLocation();
	FaceView.ViewForward =
		CaptureRotation.RotateVector(Basis.Direction).GetSafeNormal();
	FaceView.ViewUp = CaptureRotation.RotateVector(Basis.Up).GetSafeNormal();
	FaceView.ViewRight =
		(FaceView.ViewUp ^ FaceView.ViewForward).GetSafeNormal();
	FaceView.ViewMatrix =
		FLookFromMatrix(FaceView.ViewOrigin, FaceView.ViewForward, FaceView.ViewUp);
	FaceView.ViewRotationMatrix = FaceView.ViewMatrix;
	FaceView.ViewRotationMatrix.SetOrigin(FVector::ZeroVector);
	FaceView.ProjectionMatrix =
		BuildPortalPerspectiveMatrix(PI * 0.25f, Request.NearClip);
	FaceView.ViewProjectionMatrix =
		FaceView.ViewMatrix * FaceView.ProjectionMatrix;
	FaceView.PreViewTranslation = FVector3f(-FaceView.ViewOrigin);
	FaceView.TranslatedWorldToClipMatrix =
		FaceView.ViewRotationMatrix * FaceView.ProjectionMatrix;
	FaceView.ViewRect = FIntRect(
		0,
		0,
		Request.CubeFaceResolution,
		Request.CubeFaceResolution);
	FaceView.FocalLength =
		FVector2f(float(Request.CubeFaceResolution) * 0.5f);
	FaceView.NearClip = Request.NearClip;
	FaceView.FarClip = Request.FarClip;
	FaceView.Resolution = Request.CubeFaceResolution;

	return FaceView;
}

FString MatrixToCompactString(const FMatrix& Matrix)
{
	return FString::Printf(
		TEXT("[[%0.3f,%0.3f,%0.3f,%0.3f],[%0.3f,%0.3f,%0.3f,%0.3f],[%0.3f,%0.3f,%0.3f,%0.3f],[%0.3f,%0.3f,%0.3f,%0.3f]]"),
		Matrix.M[0][0],
		Matrix.M[0][1],
		Matrix.M[0][2],
		Matrix.M[0][3],
		Matrix.M[1][0],
		Matrix.M[1][1],
		Matrix.M[1][2],
		Matrix.M[1][3],
		Matrix.M[2][0],
		Matrix.M[2][1],
		Matrix.M[2][2],
		Matrix.M[2][3],
		Matrix.M[3][0],
		Matrix.M[3][1],
		Matrix.M[3][2],
		Matrix.M[3][3]);
}

FString ProjectPointToString(
	const FVector& Point,
	const FMatrix& ViewMatrix,
	const FMatrix& ViewProjectionMatrix,
	const FMatrix& TranslatedWorldToClipMatrix,
	const FVector3f& PreViewTranslation)
{
	const FVector4 View = ViewMatrix.TransformFVector4(FVector4(Point, 1.0));
	const FVector4 Clip =
		ViewProjectionMatrix.TransformFVector4(FVector4(Point, 1.0));
	const FVector TranslatedPoint = Point + FVector(PreViewTranslation);
	const FVector4 TranslatedClip =
		TranslatedWorldToClipMatrix.TransformFVector4(
			FVector4(TranslatedPoint, 1.0));
	const float InvW = FMath::IsNearlyZero(Clip.W) ? 0.0f : 1.0f / Clip.W;
	const float TranslatedInvW =
		FMath::IsNearlyZero(TranslatedClip.W) ? 0.0f : 1.0f / TranslatedClip.W;
	const FVector NDC(Clip.X * InvW, Clip.Y * InvW, Clip.Z * InvW);
	const FVector TranslatedNDC(
		TranslatedClip.X * TranslatedInvW,
		TranslatedClip.Y * TranslatedInvW,
		TranslatedClip.Z * TranslatedInvW);
	return FString::Printf(
		TEXT("World=%s View=(%.3f, %.3f, %.3f, %.3f) Clip=(%.3f, %.3f, %.3f, %.3f) NDC=%s TranslatedClip=(%.3f, %.3f, %.3f, %.3f) TranslatedNDC=%s"),
		*VecToString(Point),
		View.X,
		View.Y,
		View.Z,
		View.W,
		Clip.X,
		Clip.Y,
		Clip.Z,
		Clip.W,
		*VecToString(NDC),
		TranslatedClip.X,
		TranslatedClip.Y,
		TranslatedClip.Z,
		TranslatedClip.W,
		*VecToString(TranslatedNDC));
}

void LogFaceViewDebug(const FGSPortalCubeFaceViewParams& FaceView)
{
	if (FaceView.FaceIndex != CubeFace_PosX)
	{
		return;
	}

	PICO_LOGD(
		"GSPortalCube Face0 ViewOrigin=%s Forward=%s Right=%s Up=%s",
		*VecToString(FaceView.ViewOrigin),
		*VecToString(FaceView.ViewForward),
		*VecToString(FaceView.ViewRight),
		*VecToString(FaceView.ViewUp));
	PICO_LOGD(
		"GSPortalCube Face0 ViewMatrix=%s",
		*MatrixToCompactString(FaceView.ViewMatrix));
	PICO_LOGD(
		"GSPortalCube Face0 ProjectionMatrix=%s",
		*MatrixToCompactString(FaceView.ProjectionMatrix));
	PICO_LOGD(
		"GSPortalCube Face0 ViewProjectionMatrix=%s",
		*MatrixToCompactString(FaceView.ViewProjectionMatrix));
	PICO_LOGD(
		"GSPortalCube Face0 TranslatedWorldToClipMatrix=%s PreViewTranslation=%s",
		*MatrixToCompactString(FaceView.TranslatedWorldToClipMatrix),
		*VecToString(FVector(FaceView.PreViewTranslation)));

	const FVector P0 =
		FaceView.ViewOrigin + FaceView.ViewForward * 100.0;
	const FVector P1 = P0 + FaceView.ViewRight * 50.0;
	const FVector P2 = P0 + FaceView.ViewUp * 50.0;
	PICO_LOGD(
		"GSPortalCube Face0 P0 %s",
		*ProjectPointToString(
			P0,
			FaceView.ViewMatrix,
			FaceView.ViewProjectionMatrix,
			FaceView.TranslatedWorldToClipMatrix,
			FaceView.PreViewTranslation));
	PICO_LOGD(
		"GSPortalCube Face0 P1 %s",
		*ProjectPointToString(
			P1,
			FaceView.ViewMatrix,
			FaceView.ViewProjectionMatrix,
			FaceView.TranslatedWorldToClipMatrix,
			FaceView.PreViewTranslation));
	PICO_LOGD(
		"GSPortalCube Face0 P2 %s",
		*ProjectPointToString(
			P2,
			FaceView.ViewMatrix,
			FaceView.ViewProjectionMatrix,
			FaceView.TranslatedWorldToClipMatrix,
			FaceView.PreViewTranslation));
}

void LogPortalTransformStats_RenderThread(
	FRHIGPUBufferReadback& Readback,
	uint32 NumSplats,
	int32 FaceIndex)
{
	const uint32 NumBytes = NumSplats * sizeof(FFloat16Color);
	const FFloat16Color* TransformData =
		static_cast<const FFloat16Color*>(Readback.Lock(NumBytes));
	if (!TransformData)
	{
		PICO_LOGE(
			"GSPortalCube TransformStats failed: readback lock returned null.");
		return;
	}

	double SumScaleX = 0.0;
	double SumScaleY = 0.0;
	float MinScaleX = TNumericLimits<float>::Max();
	float MinScaleY = TNumericLimits<float>::Max();
	float MaxScaleX = 0.0f;
	float MaxScaleY = 0.0f;
	uint32 NaNOrInfCount = 0;
	uint32 ScaleOver512 = 0;
	uint32 ScaleOver2048 = 0;
	uint32 ScaleOver10000 = 0;

	const uint32 SampleCount = FMath::Min<uint32>(NumSplats, 5);
	FString Samples;
	for (uint32 Index = 0; Index < NumSplats; ++Index)
	{
		const FVector4f T(
			TransformData[Index].R.GetFloat(),
			TransformData[Index].G.GetFloat(),
			TransformData[Index].B.GetFloat(),
			TransformData[Index].A.GetFloat());
		const float ScaleX = FVector2f(T.X, T.Z).Length();
		const float ScaleY = FVector2f(T.Y, T.W).Length();
		const bool bFinite =
			FMath::IsFinite(T.X) && FMath::IsFinite(T.Y) &&
			FMath::IsFinite(T.Z) && FMath::IsFinite(T.W) &&
			FMath::IsFinite(ScaleX) && FMath::IsFinite(ScaleY);
		if (!bFinite)
		{
			++NaNOrInfCount;
			continue;
		}

		MinScaleX = FMath::Min(MinScaleX, ScaleX);
		MinScaleY = FMath::Min(MinScaleY, ScaleY);
		MaxScaleX = FMath::Max(MaxScaleX, ScaleX);
		MaxScaleY = FMath::Max(MaxScaleY, ScaleY);
		SumScaleX += ScaleX;
		SumScaleY += ScaleY;
		const float MaxScale = FMath::Max(ScaleX, ScaleY);
		ScaleOver512 += MaxScale > 512.0f ? 1u : 0u;
		ScaleOver2048 += MaxScale > 2048.0f ? 1u : 0u;
		ScaleOver10000 += MaxScale > 10000.0f ? 1u : 0u;

		if (Index < SampleCount)
		{
			Samples += FString::Printf(
				TEXT(" [%u]=(%.3f, %.3f, %.3f, %.3f) scale=(%.3f, %.3f)"),
				Index,
				T.X,
				T.Y,
				T.Z,
				T.W,
				ScaleX,
				ScaleY);
		}
	}

	const uint32 FiniteCount = NumSplats - NaNOrInfCount;
	const double AvgScaleX =
		FiniteCount > 0 ? SumScaleX / double(FiniteCount) : 0.0;
	const double AvgScaleY =
		FiniteCount > 0 ? SumScaleY / double(FiniteCount) : 0.0;
	if (FiniteCount == 0)
	{
		MinScaleX = 0.0f;
		MinScaleY = 0.0f;
	}

	PICO_LOGD(
		"GSPortalCube TransformStats face=%d splat_count=%u transform_element_count=%u half4_per_splat=1 byte_size=%u",
		FaceIndex,
		NumSplats,
		NumSplats,
		NumBytes);
	PICO_LOGD("GSPortalCube TransformStats samples:%s", *Samples);
	PICO_LOGD(
		"GSPortalCube TransformStats scale.x min=%.3f max=%.3f avg=%.3f scale.y min=%.3f max=%.3f avg=%.3f NaNOrInf=%u >512=%u >2048=%u >10000=%u",
		MinScaleX,
		MaxScaleX,
		AvgScaleX,
		MinScaleY,
		MaxScaleY,
		AvgScaleY,
		NaNOrInfCount,
		ScaleOver512,
		ScaleOver2048,
		ScaleOver10000);

	Readback.Unlock();
}

uint32 CountFlag(uint32 Flags, uint32 Mask)
{
	return (Flags & Mask) != 0u ? 1u : 0u;
}

void LogPortalVisibilityStats_RenderThread(
	FRHIGPUBufferReadback& ViewZReadback,
	FRHIGPUBufferReadback& FlagsReadback,
	uint32 NumSplats,
	int32 FaceIndex)
{
	const uint32 ViewZBytes = NumSplats * sizeof(float);
	const uint32 FlagsBytes = NumSplats * sizeof(uint32);
	const float* ViewZData =
		static_cast<const float*>(ViewZReadback.Lock(ViewZBytes));
	const uint32* FlagsData =
		static_cast<const uint32*>(FlagsReadback.Lock(FlagsBytes));
	if (!ViewZData || !FlagsData)
	{
		PICO_LOGE(
			"GSPortalCube VisibilityStats failed: readback lock returned null.");
		if (FlagsData)
		{
			FlagsReadback.Unlock();
		}
		if (ViewZData)
		{
			ViewZReadback.Unlock();
		}
		return;
	}

	float MinZ = TNumericLimits<float>::Max();
	float MaxZ = -TNumericLimits<float>::Max();
	double SumZ = 0.0;
	uint32 FiniteZCount = 0;
	uint32 ZLEZero = 0;
	uint32 ZLENear = 0;
	uint32 ZGTFar = 0;
	uint32 PosNaNInf = 0;
	uint32 InsideClip = 0;
	uint32 OutsideClip = 0;
	uint32 NDCValid = 0;
	uint32 ScaleNaNInf = 0;
	uint32 ScaleGT512 = 0;
	uint32 ScaleGT2048 = 0;
	uint32 ScaleGT10000 = 0;
	uint32 ScaleGTMax = 0;

	for (uint32 Index = 0; Index < NumSplats; ++Index)
	{
		const float Z = ViewZData[Index];
		if (FMath::IsFinite(Z))
		{
			MinZ = FMath::Min(MinZ, Z);
			MaxZ = FMath::Max(MaxZ, Z);
			SumZ += Z;
			++FiniteZCount;
		}

		const uint32 Flags = FlagsData[Index];
		ZLEZero += CountFlag(Flags, GSPortalVis_ZLEZero);
		ZLENear += CountFlag(Flags, GSPortalVis_ZLENear);
		ZGTFar += CountFlag(Flags, GSPortalVis_ZGTFar);
		PosNaNInf += CountFlag(Flags, GSPortalVis_PosNaNInf);
		InsideClip += CountFlag(Flags, GSPortalVis_InsideClip);
		OutsideClip += CountFlag(Flags, GSPortalVis_OutsideClip);
		NDCValid += CountFlag(Flags, GSPortalVis_NDCValid);
		ScaleNaNInf += CountFlag(Flags, GSPortalVis_ScaleNaNInf);
		ScaleGT512 += CountFlag(Flags, GSPortalVis_ScaleGT512);
		ScaleGT2048 += CountFlag(Flags, GSPortalVis_ScaleGT2048);
		ScaleGT10000 += CountFlag(Flags, GSPortalVis_ScaleGT10000);
		ScaleGTMax += CountFlag(Flags, GSPortalVis_ScaleGTMax);
	}

	if (FiniteZCount == 0)
	{
		MinZ = 0.0f;
		MaxZ = 0.0f;
	}
	const double AvgZ = FiniteZCount > 0 ? SumZ / double(FiniteZCount) : 0.0;

	PICO_LOGD(
		"GSPortalCube VisibilityStats face=%d splat_count=%u pos_view.z min=%.3f max=%.3f avg=%.3f",
		FaceIndex,
		NumSplats,
		MinZ,
		MaxZ,
		AvgZ);
	PICO_LOGD(
		"GSPortalCube VisibilityStats z<=0=%u z<=NearClip=%u z>FarClip=%u posNaNInf=%u insideClip=%u outsideClip=%u ndcValid=%u",
		ZLEZero,
		ZLENear,
		ZGTFar,
		PosNaNInf,
		InsideClip,
		OutsideClip,
		NDCValid);
	PICO_LOGD(
		"GSPortalCube VisibilityStats scaleNaNInf=%u scale>512=%u scale>2048=%u scale>10000=%u scale>Max=%u",
		ScaleNaNInf,
		ScaleGT512,
		ScaleGT2048,
		ScaleGT10000,
		ScaleGTMax);

	FlagsReadback.Unlock();
	ViewZReadback.Unlock();
}

FRDGPassRef CalculateDistancesForPortalCubeFace(
	FRDGBuilder& GraphBuilder,
	FSplatSceneProxy* Proxy,
	const FGSPortalCubeFaceViewParams& FaceView,
	FGSPortalCubeSplatBuffers& Buffers,
	FRDGBufferRef Indices,
	FRDGBufferRef Distances)
{
	check(Proxy);
	check(Buffers.PortalIndices);

	const FGlobalShaderMap* GlobalShaderMap =
		GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderRef<Shaders::FComputeDistanceCS> DistanceShader =
		GlobalShaderMap->GetShader<Shaders::FComputeDistanceCS>();

	Shaders::FComputeDistanceCS::FParameters* DistanceParams =
		GraphBuilder
			.AllocParameters<Shaders::FComputeDistanceCS::FParameters>();
	DistanceParams->local_to_clip =
		FMatrix44f(Proxy->GetLocalToWorld() * FaceView.ViewProjectionMatrix);
	DistanceParams->num_splats = Proxy->GetNumSplats();
	DistanceParams->Positions = MakePositionParams(Proxy);
	DistanceParams->indices = Buffers.PortalIndices->UnorderedAccessViewRHI;
	DistanceParams->distances =
		GraphBuilder.CreateUAV(Distances, PF_R16_UINT);

	return FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME(
			"GS Portal: Distances Face %d %s",
			FaceView.FaceIndex,
			*Proxy->GetResourceName().ToString()),
		ERDGPassFlags::AsyncCompute,
		DistanceShader,
		DistanceParams,
		FIntVector(NumThreadGroups(Proxy->GetNumSplats()), 1, 1));
}

FRDGPassRef SortSplatsForPortalCubeFace(
	FRDGBuilder& GraphBuilder,
	FSplatSceneProxy* Proxy,
	FGSPortalCubeSplatBuffers& Buffers,
	FRDGBufferRef Indices,
	FRDGBufferRef Distances)
{
	check(Proxy);
	check(Buffers.PortalIndices);

	const uint32 NumSplats = Proxy->GetNumSplats();

	FRDGBufferDesc IndexDesc =
		FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumSplats);
	FRDGBufferRef Indices2 =
		GraphBuilder.CreateBuffer(IndexDesc, TEXT("GSPortalIndices2"));

	FRDGBufferDesc DistanceDesc =
		FRDGBufferDesc::CreateBufferDesc(sizeof(uint16), NumSplats);
	FRDGBufferRef Distances2 =
		GraphBuilder.CreateBuffer(DistanceDesc, TEXT("GSPortalDistances2"));

	FGSPortalSortProducerParameters* SetupParameters =
		GraphBuilder.AllocParameters<FGSPortalSortProducerParameters>();
	SetupParameters->IndicesUAV = GraphBuilder.CreateUAV(Indices, PF_R32_UINT);
	SetupParameters->Indices2UAV =
		GraphBuilder.CreateUAV(Indices2, PF_R32_UINT);
	SetupParameters->Distances2UAV =
		GraphBuilder.CreateUAV(Distances2, PF_R16_UINT);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("GS Portal: RDG Sort Producer"),
		SetupParameters,
		ERDGPassFlags::Compute,
		[](FRHIComputeCommandList& RHICmdList) {});

	FGSPortalSortParameters* SortParameters =
		GraphBuilder.AllocParameters<FGSPortalSortParameters>();
	SortParameters->IndicesSRV = GraphBuilder.CreateSRV(Indices, PF_R32_UINT);
	SortParameters->IndicesUAV = GraphBuilder.CreateUAV(Indices, PF_R32_UINT);
	SortParameters->Indices2SRV =
		GraphBuilder.CreateSRV(Indices2, PF_R32_UINT);
	SortParameters->Indices2UAV =
		GraphBuilder.CreateUAV(Indices2, PF_R32_UINT);
	SortParameters->DistancesSRV =
		GraphBuilder.CreateSRV(Distances, PF_R16_UINT);
	SortParameters->DistancesUAV =
		GraphBuilder.CreateUAV(Distances, PF_R16_UINT);
	SortParameters->Distances2SRV =
		GraphBuilder.CreateSRV(Distances2, PF_R16_UINT);
	SortParameters->Distances2UAV =
		GraphBuilder.CreateUAV(Distances2, PF_R16_UINT);

	return GraphBuilder.AddPass(
		RDG_EVENT_NAME("GS Portal: Sort %s", *Proxy->GetName()),
		SortParameters,
		ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
		[NumSplats,
	     SortParameters,
	     SRV = Buffers.PortalIndices->ShaderResourceViewRHI,
	     UAV = Buffers.PortalIndices->UnorderedAccessViewRHI](
			FRHIComputeCommandList& RHICmdList)
		{
			FGPUSortBuffers SortBuffers;
			SortBuffers.RemoteKeySRVs[0] =
				SortParameters->DistancesSRV->GetRHI();
			SortBuffers.RemoteKeySRVs[1] =
				SortParameters->Distances2SRV->GetRHI();
			SortBuffers.RemoteKeyUAVs[0] =
				SortParameters->DistancesUAV->GetRHI();
			SortBuffers.RemoteKeyUAVs[1] =
				SortParameters->Distances2UAV->GetRHI();
			SortBuffers.RemoteValueSRVs[0] = SRV;
			SortBuffers.RemoteValueSRVs[1] =
				SortParameters->Indices2SRV->GetRHI();
			SortBuffers.RemoteValueUAVs[0] = UAV;
			SortBuffers.RemoteValueUAVs[1] =
				SortParameters->Indices2UAV->GetRHI();

			int32 ResultIndex = SortGPUBuffers(
				static_cast<FRHICommandList&>(RHICmdList),
				SortBuffers,
				0,
				DepthMask,
				NumSplats,
				GMaxRHIFeatureLevel);
			check(ResultIndex == 0);
		});
}

FRDGPassRef ComputeTransformsForPortalCubeFace(
	FRDGBuilder& GraphBuilder,
	FSplatSceneProxy* Proxy,
	const FGSPortalCubeFaceViewParams& FaceView,
	FGSPortalCubeSplatBuffers& Buffers)
{
	check(Proxy);
	check(Buffers.PortalTransforms);

	const FGlobalShaderMap* GlobalShaderMap =
		GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderRef<Shaders::FComputeTransformCS> ComputeSplatTransforms =
		GlobalShaderMap->GetShader<Shaders::FComputeTransformCS>();

	Shaders::FComputeTransformCS::FParameters* SplatParams =
		GraphBuilder
			.AllocParameters<Shaders::FComputeTransformCS::FParameters>();
	SplatParams->local_to_view =
		FMatrix44f(Proxy->GetLocalToWorld() * FaceView.ViewMatrix);
	const float FocalScale =
		CVarGSPortalCubeDebugFocalScale.GetValueOnRenderThread();
	SplatParams->two_focal_length =
		FaceView.FocalLength.X * 2.0f * FocalScale;
	SplatParams->num_splats = Proxy->GetNumSplats();
	SplatParams->Positions = MakePositionParams(Proxy);
	SplatParams->covariances = Proxy->GetCovariancesSRV();
	SplatParams->transforms =
		Buffers.PortalTransforms->UnorderedAccessViewRHI;

	return FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME(
			"GS Portal: Transforms Face %d %s",
			FaceView.FaceIndex,
			*Proxy->GetResourceName().ToString()),
		ERDGPassFlags::AsyncCompute,
		ComputeSplatTransforms,
		SplatParams,
		FIntVector(NumThreadGroups(Proxy->GetNumSplats()), 1, 1));
}

FRDGPassRef AddPortalTransformStatsReadbackPass(
	FRDGBuilder& GraphBuilder,
	FSplatSceneProxy* Proxy,
	const FGSPortalCubeFaceViewParams& FaceView,
	FGSPortalCubeSplatBuffers& Buffers,
	FRHIGPUBufferReadback* Readback)
{
	check(Proxy);
	check(Buffers.PortalTransforms);
	check(Readback);

	FGSPortalReadbackParameters* PassParameters =
		GraphBuilder.AllocParameters<FGSPortalReadbackParameters>();
	return GraphBuilder.AddPass(
		RDG_EVENT_NAME("GS Portal: Transform Stats Readback Face %d", FaceView.FaceIndex),
		PassParameters,
		ERDGPassFlags::Readback,
		[Readback,
	     SourceBuffer = Buffers.PortalTransforms->VertexBufferRHI,
	     NumBytes = Proxy->GetNumSplats() * uint32(sizeof(FFloat16Color))](
			FRHICommandList& RHICmdList)
		{
			RHICmdList.Transition(FRHITransitionInfo(
				SourceBuffer,
				ERHIAccess::UAVCompute,
				ERHIAccess::CopySrc));
			Readback->EnqueueCopy(RHICmdList, SourceBuffer, NumBytes);
			RHICmdList.Transition(FRHITransitionInfo(
				SourceBuffer,
				ERHIAccess::CopySrc,
				ERHIAccess::UAVCompute));
		});
}

FRDGPassRef AddPortalVisibilityStatsPass(
	FRDGBuilder& GraphBuilder,
	FSplatSceneProxy* Proxy,
	const FGSPortalCubeFaceViewParams& FaceView,
	FGSPortalCubeSplatBuffers& Buffers,
	FGSPortalVisibilityStatsReadback& StatsReadback)
{
	check(Proxy);
	check(Buffers.PortalTransforms);
	check(StatsReadback.ViewZReadback);
	check(StatsReadback.FlagsReadback);

	const uint32 NumSplats = Proxy->GetNumSplats();
	FRDGBufferRef ViewZBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateBufferDesc(sizeof(float), NumSplats),
		TEXT("GSPortalVisibilityViewZ"));
	FRDGBufferRef FlagsBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumSplats),
		TEXT("GSPortalVisibilityFlags"));

	Shaders::FGSPortalVisibilityStatsCS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<
			Shaders::FGSPortalVisibilityStatsCS::FParameters>();
	PassParameters->local_to_view =
		FMatrix44f(Proxy->GetLocalToWorld() * FaceView.ViewMatrix);
	PassParameters->local_to_clip =
		FMatrix44f(Proxy->GetLocalToWorld() * FaceView.ViewProjectionMatrix);
	PassParameters->near_clip = FaceView.NearClip;
	PassParameters->far_clip = FaceView.FarClip;
	PassParameters->frustum_margin =
		FMath::Max(
			0.0f,
			CVarGSPortalCubeDebugFrustumMargin.GetValueOnRenderThread());
	PassParameters->max_splat_scale =
		FMath::Max(
			1.0f,
			CVarGSPortalCubeMaxSplatScale.GetValueOnRenderThread());
	PassParameters->num_splats = NumSplats;
	PassParameters->Positions = MakePositionParams(Proxy);
	PassParameters->transforms =
		Buffers.PortalTransforms->ShaderResourceViewRHI;
	PassParameters->view_z = GraphBuilder.CreateUAV(ViewZBuffer, PF_R32_FLOAT);
	PassParameters->flags = GraphBuilder.CreateUAV(FlagsBuffer, PF_R32_UINT);

	FRDGPassRef StatsPass = GraphBuilder.AddPass(
		RDG_EVENT_NAME("GS Portal: Visibility Stats Face %d", FaceView.FaceIndex),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters,
	     TransformBuffer = Buffers.PortalTransforms->VertexBufferRHI,
	     NumSplats](FRHIComputeCommandList& RHICmdList)
		{
			const FGlobalShaderMap* GlobalShaderMap =
				GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderRef<Shaders::FGSPortalVisibilityStatsCS> ComputeShader =
				GlobalShaderMap->GetShader<
					Shaders::FGSPortalVisibilityStatsCS>();
			RHICmdList.Transition(FRHITransitionInfo(
				TransformBuffer,
				ERHIAccess::UAVCompute,
				ERHIAccess::SRVCompute));
			SetComputePipelineState(
				RHICmdList,
				ComputeShader.GetComputeShader());
			SetShaderParameters(
				RHICmdList,
				ComputeShader,
				ComputeShader.GetComputeShader(),
				*PassParameters);
			DispatchComputeShader(
				RHICmdList,
				ComputeShader.GetShader(),
				NumThreadGroups(NumSplats),
				1,
				1);
			UnsetShaderUAVs(
				RHICmdList,
				ComputeShader,
				ComputeShader.GetComputeShader());
			RHICmdList.Transition(FRHITransitionInfo(
				TransformBuffer,
				ERHIAccess::SRVCompute,
				ERHIAccess::UAVCompute));
		});

	AddEnqueueCopyPass(
		GraphBuilder,
		StatsReadback.ViewZReadback.Get(),
		ViewZBuffer,
		NumSplats * uint32(sizeof(float)));
	AddEnqueueCopyPass(
		GraphBuilder,
		StatsReadback.FlagsReadback.Get(),
		FlagsBuffer,
		NumSplats * uint32(sizeof(uint32)));

	return StatsPass;
}

void RenderSplatToCubeFace(
	FRDGBuilder& GraphBuilder,
	FSplatSceneProxy* Proxy,
	const FGSPortalCubeFaceViewParams& FaceView,
	FGSPortalCubeSplatBuffers& Buffers,
	FRDGTextureRef TargetCubeTexture,
	FRDGTextureRef DebugFaceTexture2D,
	const FLinearColor& ClearColor,
	FRDGPassRef IndexReadyPass,
	FRDGPassRef TransformPass,
	bool bLogDebugInfo,
	bool bForceDebugFace2DCopy,
	bool bForceSolidColorTest)
{
	check(Proxy);
	check(Buffers.PortalTransforms);
	check(Buffers.PortalIndices);
	check(TargetCubeTexture);

	const bool bDebugSolidColor =
		bForceSolidColorTest ||
		CVarGSPortalCubeDebugSolidColor.GetValueOnRenderThread() != 0;
	const bool bDebugDisablePortalDraw =
		CVarGSPortalCubeDebugDisablePortalDraw.GetValueOnRenderThread() != 0;
	const bool bDebugOpaqueBlend =
		CVarGSPortalCubeDebugOpaqueBlend.GetValueOnRenderThread() != 0;
	const EPixelFormat TargetFormat = TargetCubeTexture->Desc.Format;
	const FLinearColor EffectiveClearColor =
		bDebugSolidColor ? GetDebugSolidColorForFace(FaceView.FaceIndex)
		                 : ClearColor;
	const FClearValueBinding ClearValue(EffectiveClearColor);
	const FRDGTextureDesc FaceDesc = FRDGTextureDesc::Create2D(
		FIntPoint(FaceView.Resolution, FaceView.Resolution),
		TargetFormat,
		ClearValue,
		TexCreate_RenderTargetable | TexCreate_ShaderResource);
	FRDGTextureRef FaceTexture = GraphBuilder.CreateTexture(
		FaceDesc,
		*FString::Printf(TEXT("GSPortalCubeFace_%d"), FaceView.FaceIndex));
	const int32 DestSliceIndex = GetCubeSliceIndexForFace(FaceView.FaceIndex);

	if (bLogDebugInfo)
	{
		PICO_LOGD(
			"GSPortalCubeCapture resources Face%d %s TransformBuffer=%p IndexBuffer=%p FaceTexture=%p DestSliceIndex=%d TargetFormat=%s ForceSolidColor=%d",
			FaceView.FaceIndex,
			*FaceView.FaceName,
			Buffers.PortalTransforms->VertexBufferRHI.GetReference(),
			Buffers.PortalIndices->VertexBufferRHI.GetReference(),
			FaceTexture,
			DestSliceIndex,
			GPixelFormats[TargetFormat].Name,
			bDebugSolidColor ? 1 : 0);
	}

	AddClearRenderTargetPass(GraphBuilder, FaceTexture, EffectiveClearColor);

	if (bDebugSolidColor || bDebugDisablePortalDraw)
	{
		FRHICopyTextureInfo CopyInfo;
		CopyInfo.Size =
			FIntVector(FaceView.Resolution, FaceView.Resolution, 1);
		CopyInfo.SourceSliceIndex = 0;
		CopyInfo.DestSliceIndex = DestSliceIndex;
		CopyInfo.SourceMipIndex = 0;
		CopyInfo.DestMipIndex = 0;
		CopyInfo.NumMips = 1;
		CopyInfo.NumSlices = 1;
		AddCopyTexturePass(
			GraphBuilder, FaceTexture, TargetCubeTexture, CopyInfo);
		PICO_LOGD(
			"GSPortalCubeCapture CopyTexture QUEUED Face%d Texture2D->CubeSlice%d SolidOrDisabledDraw=%d Format=%s",
			FaceView.FaceIndex,
			DestSliceIndex,
			bDebugSolidColor ? 1 : 0,
			GPixelFormats[TargetFormat].Name);
		const int32 DebugFace2DIndex =
			CVarGSPortalCubeDebugFace2DIndex.GetValueOnRenderThread();
		if (DebugFaceTexture2D &&
		    (bForceDebugFace2DCopy || DebugFace2DIndex == FaceView.FaceIndex))
		{
			FRHICopyTextureInfo DebugCopyInfo;
			DebugCopyInfo.Size =
				FIntVector(FaceView.Resolution, FaceView.Resolution, 1);
			DebugCopyInfo.SourceSliceIndex = 0;
			DebugCopyInfo.DestSliceIndex = 0;
			DebugCopyInfo.SourceMipIndex = 0;
			DebugCopyInfo.DestMipIndex = 0;
			DebugCopyInfo.NumMips = 1;
			DebugCopyInfo.NumSlices = 1;
			AddCopyTexturePass(
				GraphBuilder,
				FaceTexture,
				DebugFaceTexture2D,
				DebugCopyInfo);
			PICO_LOGD(
				"GSPortalCubeCapture DebugFaceRT2D CopyTexture QUEUED Face%d",
				FaceView.FaceIndex);
		}
		return;
	}

	FGSPortalRenderSplatDeps* PassParameters =
		GraphBuilder.AllocParameters<FGSPortalRenderSplatDeps>();
	PassParameters->VS.local_to_view =
		FMatrix44f(Proxy->GetLocalToWorld() * FaceView.ViewMatrix);
	PassParameters->VS.local_to_clip =
		FMatrix44f(Proxy->GetLocalToWorld() * FaceView.ViewProjectionMatrix);
	PassParameters->VS.render_resolution =
		FVector2f(float(FaceView.Resolution), float(FaceView.Resolution));
	PassParameters->VS.near_clip = FaceView.NearClip;
	PassParameters->VS.far_clip = FaceView.FarClip;
	PassParameters->VS.frustum_margin =
		FMath::Max(
			0.0f,
			CVarGSPortalCubeDebugFrustumMargin.GetValueOnRenderThread());
	PassParameters->VS.max_splat_scale =
		FMath::Max(
			1.0f,
			CVarGSPortalCubeMaxSplatScale.GetValueOnRenderThread());
	PassParameters->VS.debug_centers_only =
		CVarGSPortalCubeDebugCentersOnly.GetValueOnRenderThread() != 0 ? 1u : 0u;
	PassParameters->VS.debug_fixed_splat_radius =
		FMath::Max(
			0.0f,
			CVarGSPortalCubeDebugFixedSplatRadius.GetValueOnRenderThread());
	PassParameters->VS.debug_splat_offset_scale =
		CVarGSPortalCubeDebugSplatOffsetScale.GetValueOnRenderThread();
	PassParameters->VS.debug_branch_color =
		CVarGSPortalCubeDebugBranchColor.GetValueOnRenderThread() != 0 ? 1u : 0u;
	PassParameters->VS.Positions = MakePositionParams(Proxy);
	PassParameters->VS.transforms =
		Buffers.PortalTransforms->ShaderResourceViewRHI;
	PassParameters->VS.colors = Proxy->GetColorsSRV();
	PassParameters->VS.Indices = Buffers.PortalIndices->ShaderResourceViewRHI;
	PassParameters->PS.global_tint = Proxy->GetGlobalTint();
	PassParameters->PS.exposure = Proxy->GetSplatExposure();
	PassParameters->PS.RenderTargets[0] = FRenderTargetBinding(
		FaceTexture,
		ERenderTargetLoadAction::ELoad);

	FRDGPassRef RenderPass = GraphBuilder.AddPass(
		RDG_EVENT_NAME(
			"GS Portal: Render Face %d %s",
			FaceView.FaceIndex,
			*Proxy->GetName()),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters,
	     NumSplats = Proxy->GetNumSplats(),
	     TransformBuffer = Buffers.PortalTransforms->VertexBufferRHI,
	     FaceView,
	     bDebugOpaqueBlend](FRHICommandList& RHICmdList)
		{
			const FGlobalShaderMap* GlobalShaderMap =
				GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderRef<Shaders::FGSPortalRenderSplatVS> VertexShader =
				GlobalShaderMap->GetShader<Shaders::FGSPortalRenderSplatVS>();
			TShaderRef<Shaders::FRenderSplatPS> PixelShader =
				GlobalShaderMap->GetShader<Shaders::FRenderSplatPS>();

			RHICmdList.SetViewport(
				0.0f,
				0.0f,
				0.0f,
				float(FaceView.Resolution),
				float(FaceView.Resolution),
				1.0f);

			RHICmdList.Transition(FRHITransitionInfo(
				TransformBuffer,
				ERHIAccess::UAVCompute,
				ERHIAccess::SRVGraphics));

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI =
				PipelineStateCache::GetOrCreateVertexDeclaration({});
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI =
				VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI =
				PixelShader.GetPixelShader();
			GraphicsPSOInit.DepthStencilState =
				TStaticDepthStencilState<false>::GetRHI();
			GraphicsPSOInit.RasterizerState =
				TStaticRasterizerState<>::GetRHI();
			if (bDebugOpaqueBlend)
			{
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			}
			else
			{
				GraphicsPSOInit.BlendState = TStaticBlendState<
					CW_RGBA,
					BO_Add,
					BF_SourceAlpha,
					BF_InverseSourceAlpha>::GetRHI();
			}
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
			SetShaderParameters(
				RHICmdList,
				VertexShader,
				VertexShader.GetVertexShader(),
				PassParameters->VS);
			SetShaderParameters(
				RHICmdList,
				PixelShader,
				PixelShader.GetPixelShader(),
				PassParameters->PS);

			RHICmdList.DrawPrimitive(0, 2 * NumSplats, 1);

			RHICmdList.Transition(FRHITransitionInfo(
				TransformBuffer,
				ERHIAccess::SRVGraphics,
				ERHIAccess::UAVCompute));
		});
	GraphBuilder.AddPassDependency(IndexReadyPass, RenderPass);
	GraphBuilder.AddPassDependency(TransformPass, RenderPass);

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size =
		FIntVector(FaceView.Resolution, FaceView.Resolution, 1);
	CopyInfo.SourceSliceIndex = 0;
	CopyInfo.DestSliceIndex = DestSliceIndex;
	CopyInfo.SourceMipIndex = 0;
	CopyInfo.DestMipIndex = 0;
	CopyInfo.NumMips = 1;
	CopyInfo.NumSlices = 1;
	AddCopyTexturePass(GraphBuilder, FaceTexture, TargetCubeTexture, CopyInfo);
	PICO_LOGD(
		"GSPortalCubeCapture CopyTexture QUEUED Face%d Texture2D->CubeSlice%d Format=%s",
		FaceView.FaceIndex,
		DestSliceIndex,
		GPixelFormats[TargetFormat].Name);

	const int32 DebugFace2DIndex =
		CVarGSPortalCubeDebugFace2DIndex.GetValueOnRenderThread();
	if (DebugFaceTexture2D &&
	    (bForceDebugFace2DCopy || DebugFace2DIndex == FaceView.FaceIndex))
	{
		FRHICopyTextureInfo DebugCopyInfo;
		DebugCopyInfo.Size =
			FIntVector(FaceView.Resolution, FaceView.Resolution, 1);
		DebugCopyInfo.SourceSliceIndex = 0;
		DebugCopyInfo.DestSliceIndex = 0;
		DebugCopyInfo.SourceMipIndex = 0;
		DebugCopyInfo.DestMipIndex = 0;
		DebugCopyInfo.NumMips = 1;
		DebugCopyInfo.NumSlices = 1;
		AddCopyTexturePass(
			GraphBuilder, FaceTexture, DebugFaceTexture2D, DebugCopyInfo);
		PICO_LOGD(
			"GSPortalCubeCapture DebugFaceRT2D CopyTexture QUEUED Face%d",
			FaceView.FaceIndex);
	}
}

void ExecutePortalCubeFace_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	const FGSPortalCubeCaptureRequest& Request,
	FSplatSceneProxy* Proxy,
	int32 FaceIndex)
{
	if (!Proxy)
	{
		PICO_LOGE(
			"GSPortalCubeCapture failed: cannot get FSplatSceneProxy from TargetSplatComponent.");
		PortalScreenMessageAnyThread(
			TEXT("Face START failed: missing FSplatSceneProxy"),
			true);
		return;
	}

	if (!Request.TargetRenderTargetResource ||
	    !Request.TargetRenderTargetResource->GetShaderResourceTexture().IsValid())
	{
		PICO_LOGE(
			"GSPortalCubeCapture failed: invalid TextureRenderTargetCube resource.");
		PortalScreenMessageAnyThread(
			FString::Printf(TEXT("Face%d failed: invalid Cube RT"), FaceIndex),
			true);
		return;
	}

	UE_LOG(LogPICOSplat, Display, TEXT("Face%d START render thread"), FaceIndex);
	PortalScreenMessageAnyThread(
		FString::Printf(TEXT("Face%d START"), FaceIndex));

	if (Request.bLogDebugInfo)
	{
		PICO_LOGD(
			"GSPortalCubeCapture face render started. Proxy=%s, Splats=%u, Resolution=%d, FaceIndex=%d RHI=%s ShaderPlatform=%d FeatureLevel=%d CVarState={%s}",
			*Proxy->GetName(),
			Proxy->GetNumSplats(),
			Request.CubeFaceResolution,
			FaceIndex,
			GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"),
			int32(GMaxRHIShaderPlatform),
			int32(GMaxRHIFeatureLevel),
			*FString::Printf(
				TEXT("DebugSolidColor=%d DebugBranchColor=%d DebugFixedSplatRadius=%.3f DebugDisablePortalDraw=%d DebugFace2DIndex=%d ForceSolidColorTest=%d"),
				CVarGSPortalCubeDebugSolidColor.GetValueOnRenderThread(),
				CVarGSPortalCubeDebugBranchColor.GetValueOnRenderThread(),
				CVarGSPortalCubeDebugFixedSplatRadius.GetValueOnRenderThread(),
				CVarGSPortalCubeDebugDisablePortalDraw.GetValueOnRenderThread(),
				CVarGSPortalCubeDebugFace2DIndex.GetValueOnRenderThread(),
				Request.bForceSolidColorTest ? 1 : 0));
	}

	FGSPortalCubeSplatBuffers Buffers;
	Buffers.Init(RHICmdList, Proxy->GetNumSplats());

	FRDGBuilder GraphBuilder(RHICmdList);
	FRDGTextureRef TargetCubeTexture = RegisterExternalTexture(
		GraphBuilder,
		Request.TargetRenderTargetResource->GetShaderResourceTexture(),
		TEXT("GSPortalTargetCube"));
	FRDGTextureRef DebugFaceTexture2D = nullptr;
	if (Request.DebugFaceRenderTargetResource &&
	    Request.DebugFaceRenderTargetResource->GetShaderResourceTexture().IsValid())
	{
		DebugFaceTexture2D = RegisterExternalTexture(
			GraphBuilder,
			Request.DebugFaceRenderTargetResource->GetShaderResourceTexture(),
			TEXT("GSPortalDebugFace2D"));
	}

	const bool bDebugDisableSorting =
		CVarGSPortalCubeDebugDisableSorting.GetValueOnRenderThread() != 0;
	const bool bDebugProjectionPoints =
		CVarGSPortalCubeDebugProjectionPoints.GetValueOnRenderThread() != 0;
	const bool bDebugTransformStats =
		CVarGSPortalCubeDebugTransformStats.GetValueOnRenderThread() != 0;
	const bool bDebugVisibilityStats =
		CVarGSPortalCubeDebugVisibilityStats.GetValueOnRenderThread() != 0;

	const FGSPortalCubeFaceViewParams FaceView =
		BuildFaceViewParams(Request, FaceIndex);
	if (bDebugProjectionPoints)
	{
		LogFaceViewDebug(FaceView);
	}

	if (Request.bLogDebugInfo)
	{
		PICO_LOGD(
			"GSPortalCubeCapture face %d %s Direction=%s Up=%s Right=%s Slice=%d",
			FaceView.FaceIndex,
			*FaceView.FaceName,
			*VecToString(FaceView.ViewForward),
			*VecToString(FaceView.ViewUp),
			*VecToString(FaceView.ViewRight),
			GetCubeSliceIndexForFace(FaceView.FaceIndex));
	}

	const uint32 NumSplats = Proxy->GetNumSplats();
	FRDGBufferDesc IndexDesc =
		FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumSplats);
	FRDGBufferRef Indices =
		GraphBuilder.CreateBuffer(IndexDesc, TEXT("GSPortalIndices"));
	FRDGBufferDesc DistanceDesc =
		FRDGBufferDesc::CreateBufferDesc(sizeof(uint16), NumSplats);
	FRDGBufferRef Distances =
		GraphBuilder.CreateBuffer(DistanceDesc, TEXT("GSPortalDistances"));

	if (Request.bLogDebugInfo)
	{
		PICO_LOGD(
			"GSPortalCubeCapture RDG buffers Face%d IndexBuffer=%p DistanceBuffer=%p",
			FaceView.FaceIndex,
			Indices,
			Distances);
	}

	FRDGPassRef DistancePass = CalculateDistancesForPortalCubeFace(
		GraphBuilder,
		Proxy,
		FaceView,
		Buffers,
		Indices,
		Distances);
	FRDGPassRef IndexReadyPass = DistancePass;
	if (!bDebugDisableSorting)
	{
		IndexReadyPass =
			SortSplatsForPortalCubeFace(GraphBuilder, Proxy, Buffers, Indices, Distances);
	}
	FRDGPassRef TransformPass =
		ComputeTransformsForPortalCubeFace(GraphBuilder, Proxy, FaceView, Buffers);

	FGSPortalVisibilityStatsReadback VisibilityReadback;
	if (bDebugVisibilityStats)
	{
		VisibilityReadback.ViewZReadback =
			MakeUnique<FRHIGPUBufferReadback>(
				*FString::Printf(
					TEXT("GSPortalCube.VisibilityViewZ.Face%d"),
					FaceView.FaceIndex));
		VisibilityReadback.FlagsReadback =
			MakeUnique<FRHIGPUBufferReadback>(
				*FString::Printf(
					TEXT("GSPortalCube.VisibilityFlags.Face%d"),
					FaceView.FaceIndex));
		VisibilityReadback.NumSplats = Proxy->GetNumSplats();
		VisibilityReadback.FaceIndex = FaceView.FaceIndex;
		FRDGPassRef VisibilityStatsPass = AddPortalVisibilityStatsPass(
			GraphBuilder,
			Proxy,
			FaceView,
			Buffers,
			VisibilityReadback);
		GraphBuilder.AddPassDependency(TransformPass, VisibilityStatsPass);
		TransformPass = VisibilityStatsPass;
	}

	FGSPortalTransformStatsReadback TransformStatsReadback;
	if (bDebugTransformStats)
	{
		TransformStatsReadback.Readback = MakeUnique<FRHIGPUBufferReadback>(
			*FString::Printf(
				TEXT("GSPortalCube.TransformStats.Face%d"),
				FaceView.FaceIndex));
		TransformStatsReadback.NumSplats = Proxy->GetNumSplats();
		TransformStatsReadback.FaceIndex = FaceView.FaceIndex;
		FRDGPassRef ReadbackPass = AddPortalTransformStatsReadbackPass(
			GraphBuilder,
			Proxy,
			FaceView,
			Buffers,
			TransformStatsReadback.Readback.Get());
		GraphBuilder.AddPassDependency(TransformPass, ReadbackPass);
		TransformPass = ReadbackPass;
	}

	const bool bForceDebugFace2DCopy =
		Request.bForceDebugFace2DCopy;
	RenderSplatToCubeFace(
		GraphBuilder,
		Proxy,
		FaceView,
		Buffers,
		TargetCubeTexture,
		DebugFaceTexture2D,
		Request.ClearColor,
		IndexReadyPass,
		TransformPass,
		Request.bLogDebugInfo,
		bForceDebugFace2DCopy,
		Request.bForceSolidColorTest);

	GraphBuilder.Execute();
	UE_LOG(LogPICOSplat, Display, TEXT("Face%d DONE render thread"), FaceIndex);
	PortalScreenMessageAnyThread(
		FString::Printf(TEXT("Face%d DONE"), FaceIndex));

	if (bDebugTransformStats || bDebugVisibilityStats)
	{
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		if (bDebugVisibilityStats)
		{
			if (!VisibilityReadback.ViewZReadback ||
			    !VisibilityReadback.FlagsReadback)
			{
				PICO_LOGE(
					"GSPortalCube VisibilityStats failed: readback for face %d is missing.",
					FaceView.FaceIndex);
			}
			else if (!VisibilityReadback.ViewZReadback->IsReady() ||
			         !VisibilityReadback.FlagsReadback->IsReady())
			{
				PICO_LOGE(
					"GSPortalCube VisibilityStats failed: readback for face %d is not ready after GPU idle.",
					FaceView.FaceIndex);
			}
			else
			{
				LogPortalVisibilityStats_RenderThread(
					*VisibilityReadback.ViewZReadback,
					*VisibilityReadback.FlagsReadback,
					VisibilityReadback.NumSplats,
					VisibilityReadback.FaceIndex);
			}
		}

		if (bDebugTransformStats)
		{
			if (!TransformStatsReadback.Readback)
			{
				PICO_LOGE(
					"GSPortalCube TransformStats failed: readback for face %d is missing.",
					FaceView.FaceIndex);
			}
			else if (!TransformStatsReadback.Readback->IsReady())
			{
				PICO_LOGE(
					"GSPortalCube TransformStats failed: readback for face %d is not ready after GPU idle.",
					FaceView.FaceIndex);
			}
			else
			{
				LogPortalTransformStats_RenderThread(
					*TransformStatsReadback.Readback,
					TransformStatsReadback.NumSplats,
					TransformStatsReadback.FaceIndex);
			}
		}
	}

	Buffers.Release();

	if (Request.bLogDebugInfo)
	{
		PICO_LOGD(
			"GSPortalCubeCapture face render finished. FaceIndex=%d Slice=%d",
			FaceIndex,
			GetCubeSliceIndexForFace(FaceIndex));
	}
}

void ExecutePortalCubeCapture_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FGSPortalCubeCaptureRequest Request)
{
	USplatComponent* Component = Request.TargetSplatComponent.Get();
	if (!Component)
	{
		PICO_LOGE(
			"GSPortalCubeCapture failed: TargetSplatComponent is null.");
		PortalScreenMessageAnyThread(
			TEXT("Capture render failed: TargetSplatComponent null"),
			true);
		return;
	}

	FSplatSceneProxy* Proxy = static_cast<FSplatSceneProxy*>(
		Component->GetCurrentSceneProxyForCapture());
	if (!Proxy)
	{
		PICO_LOGE(
			"GSPortalCubeCapture failed: cannot get FSplatSceneProxy from TargetSplatComponent.");
		PortalScreenMessageAnyThread(
			TEXT("Capture render failed: FSplatSceneProxy null"),
			true);
		return;
	}

	if (!Request.TargetRenderTargetResource ||
	    !Request.TargetRenderTargetResource->GetShaderResourceTexture().IsValid())
	{
		PICO_LOGE(
			"GSPortalCubeCapture failed: invalid TextureRenderTargetCube resource.");
		PortalScreenMessageAnyThread(
			TEXT("Capture render failed: invalid Cube RT"),
			true);
		return;
	}

	if (Request.bLogDebugInfo)
	{
		PICO_LOGD(
			"GSPortalCubeCapture render command started. Proxy=%s, Splats=%u, Resolution=%d, CaptureFaceIndex=%d",
			*Proxy->GetName(),
			Proxy->GetNumSplats(),
			Request.CubeFaceResolution,
			Request.CaptureFaceIndex);
	}

	const int32 DebugSingleFaceIndex =
		CVarGSPortalCubeDebugSingleFaceIndex.GetValueOnRenderThread();
	if (Request.CaptureFaceIndex >= CubeFace_PosX &&
	    Request.CaptureFaceIndex < CubeFace_MAX)
	{
		ExecutePortalCubeFace_RenderThread(
			RHICmdList, Request, Proxy, Request.CaptureFaceIndex);
	}
	else if (DebugSingleFaceIndex >= CubeFace_PosX &&
	         DebugSingleFaceIndex < CubeFace_MAX)
	{
		ExecutePortalCubeFace_RenderThread(
			RHICmdList, Request, Proxy, DebugSingleFaceIndex);
	}
	else
	{
		for (int32 FaceIndex = CubeFace_PosX; FaceIndex < CubeFace_MAX; ++FaceIndex)
		{
			ExecutePortalCubeFace_RenderThread(
				RHICmdList, Request, Proxy, FaceIndex);
		}
	}

	if (Request.bLogDebugInfo)
	{
		PICO_LOGD("GSPortalCubeCapture render command finished.");
	}
}
} // namespace

void SubmitGSPortalCubeCapture_RenderThread(
	const FGSPortalCubeCaptureRequest& Request)
{
	ENQUEUE_RENDER_COMMAND(GSPortalCubeCapture)(
		[Request](FRHICommandListImmediate& RHICmdList)
		{
			ExecutePortalCubeCapture_RenderThread(RHICmdList, Request);
		});
}

} // namespace PICO::Splat
