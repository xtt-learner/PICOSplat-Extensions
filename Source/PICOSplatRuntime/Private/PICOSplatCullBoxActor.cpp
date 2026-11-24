/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#include "PICOSplatCullBoxActor.h"

#include "Components/BoxComponent.h"

APICOSplatCullBoxActor::APICOSplatCullBoxActor()
{
	BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("CullBox"));
	SetRootComponent(BoxComponent);

	BoxComponent->SetBoxExtent(FVector(100.0, 100.0, 100.0));
	BoxComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoxComponent->SetGenerateOverlapEvents(false);
	BoxComponent->SetHiddenInGame(false);
}

void APICOSplatCullBoxActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (BoxComponent)
	{
		BoxComponent->SetHiddenInGame(!bDrawDebug);
	}
}

bool APICOSplatCullBoxActor::ContainsWorldPosition(
	const FVector& WorldPosition) const
{
	if (!BoxComponent)
	{
		return false;
	}

	const FVector BoxLocalPosition =
		BoxComponent->GetComponentTransform().InverseTransformPosition(
			WorldPosition);
	const FVector BoxExtent = BoxComponent->GetUnscaledBoxExtent();

	return FMath::Abs(BoxLocalPosition.X) <= BoxExtent.X &&
	       FMath::Abs(BoxLocalPosition.Y) <= BoxExtent.Y &&
	       FMath::Abs(BoxLocalPosition.Z) <= BoxExtent.Z;
}
