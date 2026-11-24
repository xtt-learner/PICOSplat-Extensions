/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#include "GSPortalCubeCaptureActor.h"

#include "Engine/Engine.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Logging.h"
#include "Rendering/GSPortalCubeCaptureRenderer.h"
#include "SplatComponent.h"
#include "SplatSettings.h"
#include "TextureResource.h"

using PICO::Splat::FGSPortalCubeCaptureRequest;

AGSPortalCubeCaptureActor::AGSPortalCubeCaptureActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	USceneComponent* SceneRoot =
		CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	LastCaptureStatus = TEXT("Not captured yet.");
}

void AGSPortalCubeCaptureActor::BeginPlay()
{
	Super::BeginPlay();

	const FString BeginPlayMessage = FString::Printf(
		TEXT("GSPortalCubeCaptureActor BeginPlay Actor=%s Map=%s CaptureOnBeginPlay=%d CaptureEveryFrame=%d TargetCube=%s"),
		*GetNameSafe(this),
		GetWorld() ? *GetWorld()->GetMapName() : TEXT("None"),
		bCaptureOnBeginPlay ? 1 : 0,
		bCaptureEveryFrame ? 1 : 0,
		*GetNameSafe(TargetRenderTargetCube));
	UE_LOG(LogPICOSplat, Display, TEXT("%s"), *BeginPlayMessage);
	AddPortalScreenDebugMessage(BeginPlayMessage);

	if (bCaptureOnBeginPlay)
	{
		ScheduleAutoCapture(TEXT("BeginPlay"));
	}
}

void AGSPortalCubeCaptureActor::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	CancelScheduledAutoCapture();
	Super::EndPlay(EndPlayReason);
}

void AGSPortalCubeCaptureActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (bCaptureOnEditorMapOpened && World &&
	    World->WorldType == EWorldType::Editor)
	{
		ScheduleAutoCapture(TEXT("EditorMapOpened"));
	}
#endif
}

void AGSPortalCubeCaptureActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bCaptureEveryFrame)
	{
		return;
	}

	TimeSinceLastCapture += DeltaSeconds;
	if (TimeSinceLastCapture >= FMath::Max(CaptureInterval, 0.01f))
	{
		TimeSinceLastCapture = 0.0f;
		CapturePortalCube();
	}
}

#if WITH_EDITOR
void AGSPortalCubeCaptureActor::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!bRecaptureOnPropertyChanged)
	{
		return;
	}

	const FName PropertyName =
		PropertyChangedEvent.Property
			? PropertyChangedEvent.Property->GetFName()
			: NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(
		                    AGSPortalCubeCaptureActor, TargetSplatActor) ||
	    PropertyName == GET_MEMBER_NAME_CHECKED(
		                    AGSPortalCubeCaptureActor, TargetSplatActorName) ||
	    PropertyName == GET_MEMBER_NAME_CHECKED(
		                    AGSPortalCubeCaptureActor, TargetSplatComponent) ||
	    PropertyName == GET_MEMBER_NAME_CHECKED(
		                    AGSPortalCubeCaptureActor, TargetRenderTargetCube) ||
	    PropertyName == GET_MEMBER_NAME_CHECKED(
		                    AGSPortalCubeCaptureActor, CubeFaceResolution) ||
	    PropertyName == GET_MEMBER_NAME_CHECKED(
		                    AGSPortalCubeCaptureActor, NearClip) ||
	    PropertyName == GET_MEMBER_NAME_CHECKED(
		                    AGSPortalCubeCaptureActor, FarClip))
	{
		ScheduleAutoCapture(TEXT("PropertyChanged"));
	}
}

void AGSPortalCubeCaptureActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (bFinished && bRecaptureOnPropertyChanged)
	{
		ScheduleAutoCapture(TEXT("ActorTransformChanged"));
	}
}
#endif

void AGSPortalCubeCaptureActor::CaptureOnce()
{
	if (bLogDebugInfo)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT(
				"[GSPortalCubeCapture] CaptureOnce called; forwarding to CapturePortalCube."));
	}

	CapturePortalCube();
}

void AGSPortalCubeCaptureActor::CapturePortalCube()
{
	const FString StartMessage = FString::Printf(
		TEXT("CapturePortalCube START Actor=%s TargetActor=%s TargetComp=%s"),
		*GetNameSafe(this),
		*GetNameSafe(TargetSplatActor),
		*GetNameSafe(TargetSplatComponent));
	UE_LOG(LogPICOSplat, Display, TEXT("%s"), *StartMessage);
	AddPortalScreenDebugMessage(TEXT("CapturePortalCube START"));

	UE_LOG(
		LogPICOSplat,
		Display,
		TEXT("GSPortalCubeCapture CVar state: %s"),
		*PICO::Splat::GetGSPortalCubeCaptureDebugState_GameThread());

	CaptureAllFacesSequential();

	UE_LOG(
		LogPICOSplat,
		Display,
		TEXT("CapturePortalCube DONE submitted all face requests."));
	AddPortalScreenDebugMessage(TEXT("CapturePortalCube DONE"));
}

void AGSPortalCubeCaptureActor::CaptureFace(int32 FaceIndex)
{
	if (FaceIndex < 0 || FaceIndex >= 6)
	{
		const FString Status = FString::Printf(
			TEXT("GSPortalCubeCapture failed: invalid FaceIndex %d."),
			FaceIndex);
		SetLastCaptureStatus(Status);
		PICO_LOGE("%s", *Status);
		return;
	}

	if (bLogDebugInfo)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[GSPortalCubeCapture] CaptureFace called. FaceIndex=%d"),
			FaceIndex);
	}

	if (!ResolveTargetSplat())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT(
				"GSPortalCubeCapture failed: cannot resolve TargetSplatComponent. Please assign TargetSplatActor or place a SplatActor with USplatComponent in the level."));
		return;
	}

	SubmitCaptureRequest(GetActorTransform(), FaceIndex, true);
}

void AGSPortalCubeCaptureActor::CaptureAllFacesSequential()
{
	UE_LOG(LogPICOSplat, Display, TEXT("CaptureAllFacesSequential START"));
	AddPortalScreenDebugMessage(TEXT("CaptureAllFacesSequential START"));

	if (!ResolveTargetSplat())
	{
		const FString Status = TEXT(
			"GSPortalCubeCapture failed: cannot resolve TargetSplatComponent. Please assign TargetSplatActor or place a SplatActor with USplatComponent in the level.");
		UE_LOG(LogPICOSplat, Error, TEXT("%s"), *Status);
		AddPortalScreenDebugMessage(Status, true);
		return;
	}

	for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
	{
		const FString FaceStart =
			FString::Printf(TEXT("Face%d START"), FaceIndex);
		UE_LOG(LogPICOSplat, Display, TEXT("%s"), *FaceStart);
		AddPortalScreenDebugMessage(FaceStart);
		SubmitCaptureRequest(GetActorTransform(), FaceIndex);
	}

	UE_LOG(LogPICOSplat, Display, TEXT("CaptureAllFacesSequential DONE"));
}

void AGSPortalCubeCaptureActor::CaptureAtTransform(
	const FTransform& InCaptureTransform)
{
	if (!ResolveTargetSplat())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT(
				"GSPortalCubeCapture failed: cannot resolve TargetSplatComponent. Please assign TargetSplatActor or place a SplatActor with USplatComponent in the level."));
		return;
	}

	for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
	{
		SubmitCaptureRequest(InCaptureTransform, FaceIndex);
	}
}

void AGSPortalCubeCaptureActor::SetTargetSplatComponent(
	USplatComponent* InComponent)
{
	TargetSplatComponent = InComponent;
}

void AGSPortalCubeCaptureActor::SetTargetRenderTargetCube(
	UTextureRenderTargetCube* InRenderTargetCube)
{
	TargetRenderTargetCube = InRenderTargetCube;
}

bool AGSPortalCubeCaptureActor::IsCaptureValid() const
{
	FString UnusedStatus;
	return ValidateCaptureInputs(UnusedStatus);
}

FString AGSPortalCubeCaptureActor::GetLastCaptureStatus() const
{
	return LastCaptureStatus;
}

bool AGSPortalCubeCaptureActor::CanAutoCaptureInCurrentWorld() const
{
	if (IsTemplate() ||
	    HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject |
	                RF_BeginDestroyed | RF_FinishDestroyed))
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	return World->WorldType == EWorldType::Game ||
	       World->WorldType == EWorldType::PIE ||
	       World->WorldType == EWorldType::Editor;
}

void AGSPortalCubeCaptureActor::ScheduleAutoCapture(const TCHAR* Reason)
{
	if (!CanAutoCaptureInCurrentWorld())
	{
		return;
	}

	CancelScheduledAutoCapture();

	const float DelaySeconds = FMath::Max(AutoCaptureDelay, 0.0f);
	UWorld* World = GetWorld();
	check(World);

#if WITH_EDITOR
	if (World->WorldType == EWorldType::Editor)
	{
		if (bLogDebugInfo)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT(
					"[GSPortalCubeCapture] Scheduling editor auto capture. Reason=%s, Delay=%.2fs"),
				Reason,
				DelaySeconds);
		}

		EditorAutoCaptureTickerHandle =
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateWeakLambda(
					this,
					[this](float)
					{
						EditorAutoCaptureTickerHandle.Reset();
						ExecuteScheduledAutoCapture();
						return false;
					}),
				DelaySeconds);
		return;
	}
#endif

	if (bLogDebugInfo)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT(
				"[GSPortalCubeCapture] Scheduling runtime auto capture. Reason=%s, Delay=%.2fs"),
			Reason,
			DelaySeconds);
	}

	FTimerDelegate TimerDelegate;
	TimerDelegate.BindUObject(
		this,
		&AGSPortalCubeCaptureActor::ExecuteScheduledAutoCapture);
	World->GetTimerManager().SetTimer(
		AutoCaptureTimerHandle,
		TimerDelegate,
		DelaySeconds,
		false);
}

void AGSPortalCubeCaptureActor::CancelScheduledAutoCapture()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoCaptureTimerHandle);
	}

#if WITH_EDITOR
	if (EditorAutoCaptureTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorAutoCaptureTickerHandle);
		EditorAutoCaptureTickerHandle.Reset();
	}
#endif
}

void AGSPortalCubeCaptureActor::ExecuteScheduledAutoCapture()
{
	if (!CanAutoCaptureInCurrentWorld())
	{
		return;
	}

	if (!TargetRenderTargetCube)
	{
		if (bLogDebugInfo)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT(
					"[GSPortalCubeCapture] Auto capture skipped: TargetRenderTargetCube is null."));
		}
		return;
	}

	CapturePortalCube();
}

bool AGSPortalCubeCaptureActor::ResolveTargetSplat()
{
	if (bLogDebugInfo)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT(
				"[GSPortalCubeCapture] ResolveTargetSplat started. TargetSplatActor=%s, TargetSplatComponent=%s, TargetSplatActorName=%s"),
			*GetNameSafe(TargetSplatActor),
			*GetNameSafe(TargetSplatComponent),
			*TargetSplatActorName);
	}

	if (TargetSplatComponent)
	{
		if (!TargetSplatActor)
		{
			TargetSplatActor = TargetSplatComponent->GetOwner();
		}

		if (bLogDebugInfo)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT(
					"[GSPortalCubeCapture] Resolved TargetSplatActor=%s, TargetSplatComponent=%s"),
				*GetNameSafe(TargetSplatActor),
				*GetNameSafe(TargetSplatComponent));
		}
		return true;
	}

	if (TargetSplatActor)
	{
		if (bLogDebugInfo)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT(
					"[GSPortalCubeCapture] TargetSplatComponent is null, trying TargetSplatActor=%s"),
				*GetNameSafe(TargetSplatActor));
		}

		if (USplatComponent* FoundComp =
			    TargetSplatActor->FindComponentByClass<USplatComponent>())
		{
			TargetSplatComponent = FoundComp;
			if (bLogDebugInfo)
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT(
						"[GSPortalCubeCapture] Found splat actor: %s"),
					*GetNameSafe(TargetSplatActor));
				UE_LOG(
					LogTemp,
					Display,
					TEXT(
						"[GSPortalCubeCapture] Found splat component: %s"),
					*GetNameSafe(TargetSplatComponent));
			}
			return true;
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[GSPortalCubeCapture] Failed to resolve USplatComponent: World is null."));
		return false;
	}

	if (!TargetSplatActorName.IsEmpty())
	{
		if (bLogDebugInfo)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT(
					"[GSPortalCubeCapture] Trying TargetSplatActorName contains match: %s"),
				*TargetSplatActorName);
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			if (bLogDebugInfo)
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT(
						"[GSPortalCubeCapture] Checked actor by name: %s"),
					*GetNameSafe(Actor));
			}

			bool bActorNameMatches =
				Actor->GetName().Contains(TargetSplatActorName);
#if WITH_EDITOR
			bActorNameMatches =
				bActorNameMatches ||
				Actor->GetActorLabel().Contains(TargetSplatActorName);
#endif
			if (!bActorNameMatches)
			{
				continue;
			}

			if (USplatComponent* FoundComp =
				    Actor->FindComponentByClass<USplatComponent>())
			{
				TargetSplatActor = Actor;
				TargetSplatComponent = FoundComp;
				if (bLogDebugInfo)
				{
					UE_LOG(
						LogTemp,
						Display,
						TEXT(
							"[GSPortalCubeCapture] Found splat actor: %s"),
						*GetNameSafe(TargetSplatActor));
					UE_LOG(
						LogTemp,
						Display,
						TEXT(
							"[GSPortalCubeCapture] Found splat component: %s"),
						*GetNameSafe(TargetSplatComponent));
				}
				return true;
			}
		}
	}

	if (bAutoFindSplatActor)
	{
		if (bLogDebugInfo)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT(
					"[GSPortalCubeCapture] TargetSplatActor is null or has no USplatComponent, auto finding splat actor in world."));
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || Actor == this)
			{
				continue;
			}

			if (bLogDebugInfo)
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[GSPortalCubeCapture] Checked actor: %s"),
					*GetNameSafe(Actor));
			}

			if (USplatComponent* FoundComp =
				    Actor->FindComponentByClass<USplatComponent>())
			{
				TargetSplatActor = Actor;
				TargetSplatComponent = FoundComp;
				if (bLogDebugInfo)
				{
					UE_LOG(
						LogTemp,
						Display,
						TEXT(
							"[GSPortalCubeCapture] Found splat actor: %s"),
						*GetNameSafe(TargetSplatActor));
					UE_LOG(
						LogTemp,
						Display,
						TEXT(
							"[GSPortalCubeCapture] Found splat component: %s"),
						*GetNameSafe(TargetSplatComponent));
				}
				return true;
			}
		}
	}

	UE_LOG(
		LogTemp,
		Error,
		TEXT(
			"[GSPortalCubeCapture] Failed to resolve USplatComponent. TargetSplatActor=%s"),
		*GetNameSafe(TargetSplatActor));
	return false;
}

bool AGSPortalCubeCaptureActor::ValidateCaptureInputs(
	FString& OutStatus) const
{
	if (!TargetSplatComponent)
	{
		OutStatus =
			TEXT("GSPortalCubeCapture failed: TargetSplatComponent is null.");
		return false;
	}

	if (!TargetRenderTargetCube)
	{
		OutStatus =
			TEXT("GSPortalCubeCapture failed: TargetRenderTargetCube is null.");
		return false;
	}

	if (!USplatSettings::IsSortingOnGPU())
	{
		OutStatus =
			TEXT("GSPortalCubeCapture MVP only supports GPU sorting.");
		return false;
	}

	if (!TargetSplatComponent->GetCurrentSceneProxyForCapture())
	{
		OutStatus = TEXT(
			"GSPortalCubeCapture failed: cannot get FSplatSceneProxy from TargetSplatComponent.");
		return false;
	}

	OutStatus = TEXT("GSPortalCubeCapture inputs are valid.");
	return true;
}

void AGSPortalCubeCaptureActor::SubmitCaptureRequest(
	const FTransform& InCaptureTransform,
	int32 CaptureFaceIndex,
	bool bForceDebugFace2DCopy)
{
	FString Status;
	if (!ValidateCaptureInputs(Status))
	{
		SetLastCaptureStatus(Status);
		PICO_LOGE("%s", *Status);
		AddPortalScreenDebugMessage(Status, true);
		return;
	}

	CubeFaceResolution = FMath::Clamp(CubeFaceResolution, 64, 4096);
	NearClip = FMath::Max(NearClip, 0.1f);
	FarClip = FMath::Max(FarClip, NearClip + 1.0f);

	const bool bNeedsRenderTargetRebuild =
		TargetRenderTargetCube->SizeX != CubeFaceResolution ||
		!TargetRenderTargetCube->bTargetArraySlicesIndependently;
	TargetRenderTargetCube->bTargetArraySlicesIndependently = true;
	TargetRenderTargetCube->ClearColor = ClearColor;

	if (bNeedsRenderTargetRebuild)
	{
		TargetRenderTargetCube->InitAutoFormat(CubeFaceResolution);
		TargetRenderTargetCube->UpdateResourceImmediate(true);
	}

	UE_LOG(
		LogPICOSplat,
		Display,
		TEXT(
			"GSPortalCubeCapture RT Cube: Name=%s Size=%d OverrideFormat=%d Format=%d HDR=%d SRGB=%d TargetArraySlicesIndependently=%d ForceSolidColorTest=%d FaceIndex=%d"),
		*GetNameSafe(TargetRenderTargetCube),
		TargetRenderTargetCube->SizeX,
		int32(TargetRenderTargetCube->OverrideFormat.GetValue()),
		int32(TargetRenderTargetCube->GetFormat()),
		TargetRenderTargetCube->bHDR ? 1 : 0,
		TargetRenderTargetCube->SRGB ? 1 : 0,
		TargetRenderTargetCube->bTargetArraySlicesIndependently ? 1 : 0,
		bForceSolidColorTest ? 1 : 0,
		CaptureFaceIndex);

	FTextureRenderTargetResource* RenderTargetResource =
		TargetRenderTargetCube->GameThread_GetRenderTargetResource();
	FTextureRenderTargetCubeResource* CubeResource =
		RenderTargetResource
			? RenderTargetResource->GetTextureRenderTargetCubeResource()
			: nullptr;

	if (!CubeResource)
	{
		Status = TEXT(
			"GSPortalCubeCapture failed: invalid TextureRenderTargetCube resource.");
		SetLastCaptureStatus(Status);
		PICO_LOGE("%s", *Status);
		AddPortalScreenDebugMessage(Status, true);
		return;
	}

	FTextureRenderTargetResource* DebugFaceResource = nullptr;
	if (DebugFaceRenderTarget2D)
	{
		const bool bNeedsDebugRenderTargetRebuild =
			DebugFaceRenderTarget2D->SizeX != CubeFaceResolution ||
			DebugFaceRenderTarget2D->SizeY != CubeFaceResolution;
		DebugFaceRenderTarget2D->ClearColor = ClearColor;
		if (bNeedsDebugRenderTargetRebuild)
		{
			DebugFaceRenderTarget2D->InitAutoFormat(
				CubeFaceResolution, CubeFaceResolution);
			DebugFaceRenderTarget2D->UpdateResourceImmediate(true);
		}
		DebugFaceResource =
			DebugFaceRenderTarget2D->GameThread_GetRenderTargetResource();
	}

	FGSPortalCubeCaptureRequest Request;
	Request.TargetSplatComponent = TargetSplatComponent;
	Request.TargetRenderTargetCube = TargetRenderTargetCube;
	Request.DebugFaceRenderTarget2D = DebugFaceRenderTarget2D;
	Request.TargetRenderTargetResource = CubeResource;
	Request.DebugFaceRenderTargetResource = DebugFaceResource;
	Request.CaptureTransform = InCaptureTransform;
	Request.CubeFaceResolution = CubeFaceResolution;
	Request.CaptureFaceIndex = CaptureFaceIndex;
	Request.NearClip = NearClip;
	Request.FarClip = FarClip;
	Request.ClearColor = ClearColor;
	Request.bClearBeforeCapture = bClearBeforeCapture;
	Request.bLogDebugInfo = bLogDebugInfo;
	Request.bForceDebugFace2DCopy = bForceDebugFace2DCopy;
	Request.bForceSolidColorTest = bForceSolidColorTest;

	if (bLogDebugInfo)
	{
		PICO_LOGD(
			"GSPortalCubeCapture request submitted. Resolution=%d, Target=%s, FaceIndex=%d",
			CubeFaceResolution,
			*TargetRenderTargetCube->GetName(),
			CaptureFaceIndex);
	}

	PICO::Splat::SubmitGSPortalCubeCapture_RenderThread(Request);
	SetLastCaptureStatus(TEXT("GSPortalCubeCapture request submitted."));
}

void AGSPortalCubeCaptureActor::SetLastCaptureStatus(
	const FString& InStatus)
{
	LastCaptureStatus = InStatus;
}

void AGSPortalCubeCaptureActor::AddPortalScreenDebugMessage(
	const FString& Message,
	bool bError,
	float Duration) const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			Duration,
			bError ? FColor::Red : FColor::Green,
			FString::Printf(TEXT("[GSPortalCube] %s"), *Message));
	}
}
