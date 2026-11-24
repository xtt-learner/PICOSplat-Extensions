/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#include "PICO4DGSComponent.h"

#include "GameFramework/Actor.h"
#include "Logging.h"
#include "Misc/AssertionMacros.h"
#include "SplatComponent.h"

UPICO4DGSComponent::UPICO4DGSComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UPICO4DGSComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveTargetSplatComponent();

	int32 EffectiveStartFrame = 0;
	int32 EffectiveEndFrame = INDEX_NONE;
	if (!NormalizeFrameRange(EffectiveStartFrame, EffectiveEndFrame))
	{
		UE_LOG(
			LogPICO4DGS,
			Warning,
			TEXT("PICO4DGS BeginPlay skipped: Frames is empty on %s."),
			*GetNameSafe(GetOwner()));
		bIsPlaying = false;
		return;
	}

	StartFrame = EffectiveStartFrame;
	EndFrame = EffectiveEndFrame;
	CurrentFrame = StartFrame;
	FPS = FMath::Max(FPS, 0.01f);

	if (bPrewarmFramesOnBeginPlay)
	{
		PrewarmFrames();
	}

	SetFrameInternal(CurrentFrame, true);

	UE_LOG(
		LogPICO4DGS,
		Log,
		TEXT(
			"PICO4DGS BeginPlay. Owner=%s FrameCount=%d FPS=%.2f Range=[%d,%d] Loop=%s PlayOnBeginPlay=%s"),
		*GetNameSafe(GetOwner()),
		Frames.Num(),
		FPS,
		StartFrame,
		EndFrame,
		bLoop ? TEXT("true") : TEXT("false"),
		bPlayOnBeginPlay ? TEXT("true") : TEXT("false"));

#if PLATFORM_ANDROID
	UE_LOG(
		LogPICO4DGS,
		Log,
		TEXT("PICO4DGS Android runtime mode. FrameCount=%d FPS=%.2f"),
		Frames.Num(),
		FPS);
#endif

	if (bPlayOnBeginPlay)
	{
		Play4DGS();
	}
}

void UPICO4DGSComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bIsPlaying = false;
	AccumulatedFrameTime = 0.0f;
	Super::EndPlay(EndPlayReason);
}

void UPICO4DGSComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsPlaying || FPS <= 0.0f)
	{
		return;
	}

	int32 EffectiveStartFrame = 0;
	int32 EffectiveEndFrame = INDEX_NONE;
	if (!NormalizeFrameRange(EffectiveStartFrame, EffectiveEndFrame))
	{
		bIsPlaying = false;
		return;
	}

	AccumulatedFrameTime += DeltaTime;
	const float FrameDuration = 1.0f / FMath::Max(FPS, 0.01f);

	while (AccumulatedFrameTime >= FrameDuration && bIsPlaying)
	{
		AccumulatedFrameTime -= FrameDuration;

		if (CurrentFrame >= EffectiveEndFrame)
		{
			if (bLoop)
			{
				SetFrameInternal(EffectiveStartFrame, true);
			}
			else
			{
				SetFrameInternal(EffectiveEndFrame, true);
				bIsPlaying = false;
			}
		}
		else
		{
			SetFrameInternal(CurrentFrame + 1, true);
		}
	}
}

void UPICO4DGSComponent::Play4DGS()
{
	if (!ResolveTargetSplatComponent())
	{
		UE_LOG(
			LogPICO4DGS,
			Warning,
			TEXT("PICO4DGS Play failed: TargetSplatComponent is null on %s."),
			*GetNameSafe(GetOwner()));
		return;
	}

	int32 EffectiveStartFrame = 0;
	int32 EffectiveEndFrame = INDEX_NONE;
	if (!NormalizeFrameRange(EffectiveStartFrame, EffectiveEndFrame))
	{
		UE_LOG(
			LogPICO4DGS,
			Warning,
			TEXT("PICO4DGS Play failed: Frames is empty on %s."),
			*GetNameSafe(GetOwner()));
		return;
	}

	if (CurrentFrame < EffectiveStartFrame || CurrentFrame > EffectiveEndFrame)
	{
		SetFrameInternal(EffectiveStartFrame, true);
	}

	AccumulatedFrameTime = 0.0f;
	bIsPlaying = true;
}

void UPICO4DGSComponent::Pause4DGS()
{
	bIsPlaying = false;
}

void UPICO4DGSComponent::Stop4DGS()
{
	bIsPlaying = false;
	AccumulatedFrameTime = 0.0f;

	int32 EffectiveStartFrame = 0;
	int32 EffectiveEndFrame = INDEX_NONE;
	if (NormalizeFrameRange(EffectiveStartFrame, EffectiveEndFrame))
	{
		SetFrameInternal(EffectiveStartFrame, true);
	}
}

bool UPICO4DGSComponent::SeekFrame(int32 FrameIndex)
{
	AccumulatedFrameTime = 0.0f;
	return SetFrameInternal(FrameIndex, true);
}

void UPICO4DGSComponent::SetPlaybackFPS(float NewFPS)
{
	FPS = FMath::Max(NewFPS, 0.01f);
	AccumulatedFrameTime = 0.0f;
}

void UPICO4DGSComponent::SetLooping(bool bNewLoop)
{
	bLoop = bNewLoop;
}

int32 UPICO4DGSComponent::GetCurrentFrame() const
{
	return CurrentFrame;
}

int32 UPICO4DGSComponent::GetFrameCount() const
{
	return Frames.Num();
}

bool UPICO4DGSComponent::IsPlaying() const
{
	return bIsPlaying;
}

bool UPICO4DGSComponent::StepForward()
{
	Pause4DGS();
	return SetFrameInternal(GetNextFrame(1), true);
}

bool UPICO4DGSComponent::StepBackward()
{
	Pause4DGS();
	return SetFrameInternal(GetNextFrame(-1), true);
}

void UPICO4DGSComponent::SetFrameRange(
	int32 NewStartFrame, int32 NewEndFrame)
{
	StartFrame = NewStartFrame;
	EndFrame = NewEndFrame;

	int32 EffectiveStartFrame = 0;
	int32 EffectiveEndFrame = INDEX_NONE;
	if (!NormalizeFrameRange(EffectiveStartFrame, EffectiveEndFrame))
	{
		bIsPlaying = false;
		return;
	}

	StartFrame = EffectiveStartFrame;
	EndFrame = EffectiveEndFrame;
	if (CurrentFrame < StartFrame || CurrentFrame > EndFrame)
	{
		SetFrameInternal(StartFrame, true);
	}
}

void UPICO4DGSComponent::SetSequenceFrames(
	const TArray<USplatAsset*>& NewFrames)
{
	Frames.Reset(NewFrames.Num());
	for (USplatAsset* Frame : NewFrames)
	{
		Frames.Add(Frame);
	}

	AccumulatedFrameTime = 0.0f;
	SetFrameRange(StartFrame, EndFrame);
}

bool UPICO4DGSComponent::ResolveTargetSplatComponent()
{
	if (TargetSplatComponent)
	{
		return true;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	TargetSplatComponent = Owner->FindComponentByClass<USplatComponent>();
	return TargetSplatComponent != nullptr;
}

bool UPICO4DGSComponent::NormalizeFrameRange(
	int32& OutStartFrame, int32& OutEndFrame) const
{
	if (Frames.Num() <= 0)
	{
		return false;
	}

	OutStartFrame = FMath::Clamp(StartFrame, 0, Frames.Num() - 1);
	OutEndFrame = EndFrame < 0
		              ? Frames.Num() - 1
		              : FMath::Clamp(EndFrame, 0, Frames.Num() - 1);

	if (OutEndFrame < OutStartFrame)
	{
		OutEndFrame = OutStartFrame;
	}

	return true;
}

bool UPICO4DGSComponent::SetFrameInternal(
	int32 FrameIndex, bool bWarnOnFailure)
{
	if (!ResolveTargetSplatComponent())
	{
		if (bWarnOnFailure)
		{
			UE_LOG(
				LogPICO4DGS,
				Warning,
				TEXT("PICO4DGS SetFrame failed: TargetSplatComponent is null on %s."),
				*GetNameSafe(GetOwner()));
		}
		return false;
	}

	int32 EffectiveStartFrame = 0;
	int32 EffectiveEndFrame = INDEX_NONE;
	if (!NormalizeFrameRange(EffectiveStartFrame, EffectiveEndFrame))
	{
		if (bWarnOnFailure)
		{
			UE_LOG(
				LogPICO4DGS,
				Warning,
				TEXT("PICO4DGS SetFrame failed: Frames is empty on %s."),
				*GetNameSafe(GetOwner()));
		}
		return false;
	}

	const int32 ClampedFrame =
		FMath::Clamp(FrameIndex, EffectiveStartFrame, EffectiveEndFrame);
	USplatAsset* FrameAsset = Frames[ClampedFrame].Get();
	if (!FrameAsset)
	{
		if (bWarnOnFailure)
		{
			UE_LOG(
				LogPICO4DGS,
				Warning,
				TEXT("PICO4DGS SetFrame failed: Frame %d is null on %s."),
				ClampedFrame,
				*GetNameSafe(GetOwner()));
		}
		return false;
	}

	TargetSplatComponent->SetSplatAsset(FrameAsset);
	CurrentFrame = ClampedFrame;

	UE_LOG(
		LogPICO4DGS,
		Verbose,
		TEXT("PICO4DGS SetFrame. Owner=%s Frame=%d Asset=%s"),
		*GetNameSafe(GetOwner()),
		CurrentFrame,
		*GetNameSafe(FrameAsset));

	return true;
}

void UPICO4DGSComponent::PrewarmFrames()
{
	uint64 TotalSplats = 0;
	int32 ValidFrames = 0;

	for (int32 Index = 0; Index < Frames.Num(); ++Index)
	{
		USplatAsset* FrameAsset = Frames[Index].Get();
		if (!FrameAsset)
		{
			UE_LOG(
				LogPICO4DGS,
				Warning,
				TEXT("PICO4DGS Prewarm: Frame %d is null on %s."),
				Index,
				*GetNameSafe(GetOwner()));
			continue;
		}

		++ValidFrames;
		TotalSplats += FrameAsset->GetNumSplats();
	}

	UE_LOG(
		LogPICO4DGS,
		Log,
		TEXT("PICO4DGS Prewarm complete. Owner=%s ValidFrames=%d/%d TotalSplats=%llu"),
		*GetNameSafe(GetOwner()),
		ValidFrames,
		Frames.Num(),
		TotalSplats);
}

int32 UPICO4DGSComponent::GetNextFrame(int32 Direction) const
{
	int32 EffectiveStartFrame = 0;
	int32 EffectiveEndFrame = INDEX_NONE;
	if (!NormalizeFrameRange(EffectiveStartFrame, EffectiveEndFrame))
	{
		return CurrentFrame;
	}

	const int32 Candidate = CurrentFrame + Direction;
	if (Candidate > EffectiveEndFrame)
	{
		return bLoop ? EffectiveStartFrame : EffectiveEndFrame;
	}
	if (Candidate < EffectiveStartFrame)
	{
		return bLoop ? EffectiveEndFrame : EffectiveStartFrame;
	}
	return Candidate;
}
