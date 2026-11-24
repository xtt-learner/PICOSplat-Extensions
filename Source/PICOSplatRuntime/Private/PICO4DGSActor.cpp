/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#include "PICO4DGSActor.h"

#include "Components/SceneComponent.h"
#include "PICO4DGSComponent.h"
#include "SplatComponent.h"

APICO4DGSActor::APICO4DGSActor()
{
	USceneComponent* SceneRoot =
		CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	SplatComponent =
		CreateDefaultSubobject<USplatComponent>(TEXT("SplatComponent"));
	SplatComponent->SetupAttachment(SceneRoot);

	PICO4DGSComponent =
		CreateDefaultSubobject<UPICO4DGSComponent>(TEXT("PICO4DGSComponent"));
	PICO4DGSComponent->SetupAttachment(SceneRoot);
	PICO4DGSComponent->TargetSplatComponent = SplatComponent;
}

UPICO4DGSComponent* APICO4DGSActor::GetPICO4DGSComponent() const
{
	return PICO4DGSComponent;
}

USplatComponent* APICO4DGSActor::GetSplatComponent() const
{
	return SplatComponent;
}

void APICO4DGSActor::Play4DGS()
{
	if (PICO4DGSComponent)
	{
		PICO4DGSComponent->Play4DGS();
	}
}

void APICO4DGSActor::Pause4DGS()
{
	if (PICO4DGSComponent)
	{
		PICO4DGSComponent->Pause4DGS();
	}
}

void APICO4DGSActor::Stop4DGS()
{
	if (PICO4DGSComponent)
	{
		PICO4DGSComponent->Stop4DGS();
	}
}

bool APICO4DGSActor::SeekFrame(int32 FrameIndex)
{
	return PICO4DGSComponent
		       ? PICO4DGSComponent->SeekFrame(FrameIndex)
		       : false;
}
