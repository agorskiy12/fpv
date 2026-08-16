#include "StrikeCamera.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Kismet/KismetMathLibrary.h"

AStrikeCamera::AStrikeCamera()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(70.f);   // narrower than the 120 FPV lens, so the blast reads bigger
}

AStrikeCamera* AStrikeCamera::Spawn(UWorld* World, const FVector& BlastLocation, const FVector& ApproachDirection, float BlastRadius)
{
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStrikeCamera* Cam = World->SpawnActor<AStrikeCamera>(AStrikeCamera::StaticClass(), BlastLocation, FRotator::ZeroRotator, Params);
	if (!Cam)
	{
		return nullptr;
	}

	Cam->FocusPoint = BlastLocation;
	Cam->Distance = FMath::Max(BlastRadius * Cam->DistanceMultiplier, Cam->MinDistance);
	Cam->Height = Cam->Distance * Cam->HeightFraction;

	// Set up roughly 50 degrees off the approach, so the strike is seen side-on. Directly behind
	// looks like the flight continuing; directly in front looks like nothing happened.
	FVector Approach = ApproachDirection;
	Approach.Z = 0.f;
	if (!Approach.Normalize())
	{
		Approach = FVector::ForwardVector;
	}

	Cam->Angle = FMath::Atan2(Approach.Y, Approach.X) + FMath::DegreesToRadians(130.f);

	Cam->Tick(0.f);   // frame it before the first render, so there is no one-frame pop
	return Cam;
}

void AStrikeCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Angle += FMath::DegreesToRadians(OrbitRateDegrees) * DeltaSeconds;
	Distance = FMath::Max(Distance * (1.f - DollyInRate * DeltaSeconds), MinDistance * 0.6f);

	const FVector Offset(
		FMath::Cos(Angle) * Distance,
		FMath::Sin(Angle) * Distance,
		Height);

	const FVector NewLocation = FocusPoint + Offset;
	SetActorLocation(NewLocation);
	SetActorRotation(UKismetMathLibrary::FindLookAtRotation(NewLocation, FocusPoint));
}
