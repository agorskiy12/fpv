#include "RaceGate.h"
#include "FPVDrone.h"
#include "FPVDronePawn.h"
#include "FPVGameMode.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ARaceGate::ARaceGate()
{
	PrimaryActorTick.bCanEverTick = false;

	GateRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GateRoot"));
	SetRootComponent(GateRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	static const TCHAR* BarNames[] = { TEXT("BarTop"), TEXT("BarBottom"), TEXT("BarLeft"), TEXT("BarRight") };
	for (const TCHAR* BarName : BarNames)
	{
		UStaticMeshComponent* Bar = CreateDefaultSubobject<UStaticMeshComponent>(BarName);
		Bar->SetupAttachment(GateRoot);
		if (CubeMesh.Succeeded())
		{
			Bar->SetStaticMesh(CubeMesh.Object);
		}
		// Solid: clipping a gate should cost you the run.
		Bar->SetCollisionProfileName(TEXT("BlockAll"));
		FrameBars.Add(Bar);
	}

	PassTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("PassTrigger"));
	PassTrigger->SetupAttachment(GateRoot);
	PassTrigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	PassTrigger->SetGenerateOverlapEvents(true);
	PassTrigger->SetHiddenInGame(true);
}

void ARaceGate::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildFrame();
}

void ARaceGate::RebuildFrame()
{
	if (FrameBars.Num() < 4)
	{
		return;
	}

	const float Half = GateSize * 0.5f;
	const float Offset = Half + FrameThickness * 0.5f;

	// The engine cube is 100 units on a side, so scale is size-in-cm / 100.
	const float BarScale = FrameThickness / 100.f;
	const float SpanScale = (GateSize + FrameThickness) / 100.f;

	// The opening faces along the actor's local +X, so the frame lies in the YZ plane.
	const FVector Positions[] = {
		FVector(0.f, 0.f,  Offset),   // top
		FVector(0.f, 0.f, -Offset),   // bottom
		FVector(0.f, -Offset, 0.f),   // left
		FVector(0.f,  Offset, 0.f),   // right
	};

	const FVector Scales[] = {
		FVector(BarScale, SpanScale, BarScale),   // top
		FVector(BarScale, SpanScale, BarScale),   // bottom
		FVector(BarScale, BarScale, SpanScale),   // left
		FVector(BarScale, BarScale, SpanScale),   // right
	};

	for (int32 i = 0; i < 4; ++i)
	{
		if (FrameBars[i])
		{
			FrameBars[i]->SetRelativeLocation(Positions[i]);
			FrameBars[i]->SetRelativeScale3D(Scales[i]);
		}
	}

	if (PassTrigger)
	{
		// Deep enough along X that a fast drone cannot tunnel through between ticks.
		PassTrigger->SetBoxExtent(FVector(40.f, Half, Half), false);
	}
}

void ARaceGate::BeginPlay()
{
	Super::BeginPlay();

	if (PassTrigger)
	{
		PassTrigger->OnComponentBeginOverlap.AddDynamic(this, &ARaceGate::OnPassTriggerOverlap);
	}
}

void ARaceGate::OnPassTriggerOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (!Cast<AFPVDronePawn>(OtherActor))
	{
		return;
	}

	if (AFPVGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPVGameMode>() : nullptr)
	{
		GameMode->NotifyGatePassed(this);
	}
}
