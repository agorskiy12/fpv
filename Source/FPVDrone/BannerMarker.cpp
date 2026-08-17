#include "BannerMarker.h"
#include "FPVDrone.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ABannerMarker::ABannerMarker()
{
	PrimaryActorTick.bCanEverTick = false;

	BannerRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BannerRoot"));
	SetRootComponent(BannerRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	Mast = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mast"));
	Mast->SetupAttachment(BannerRoot);
	if (CubeMesh.Succeeded())
	{
		Mast->SetStaticMesh(CubeMesh.Object);
	}
	// Solid, so a mast is a real obstacle. Flying between flags should carry a price.
	Mast->SetCollisionProfileName(TEXT("BlockAllDynamic"));

	for (int32 Index = 0; Index < MaxBands; ++Index)
	{
		const FName BandName = *FString::Printf(TEXT("Band%d"), Index);
		UStaticMeshComponent* Band = CreateDefaultSubobject<UStaticMeshComponent>(BandName);
		Band->SetupAttachment(BannerRoot);
		if (CubeMesh.Succeeded())
		{
			Band->SetStaticMesh(CubeMesh.Object);
		}
		Band->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Band->SetVisibility(false);
		Bands.Add(Band);
	}
}

void ABannerMarker::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildBanner();
}

void ABannerMarker::BeginPlay()
{
	Super::BeginPlay();
	RebuildBanner();

	UE_LOG(LogFPV, Log, TEXT("%s banner at %s"),
		FPVFaction::GetDisplayName(Faction), *GetActorLocation().ToCompactString());
}

void ABannerMarker::GetBandColours(EFaction Faction, TArray<FLinearColor>& OutColours)
{
	OutColours.Reset();

	switch (Faction)
	{
	case EFaction::Russia:
		// White, blue, red, top to bottom.
		OutColours.Add(FLinearColor(1.f, 1.f, 1.f));
		OutColours.Add(FLinearColor(0.0f, 0.14f, 0.45f));
		OutColours.Add(FLinearColor(0.72f, 0.06f, 0.09f));
		break;

	case EFaction::NATO:
		// Ukrainian blue over yellow.
		OutColours.Add(FLinearColor(0.0f, 0.19f, 0.53f));
		OutColours.Add(FLinearColor(1.f, 0.72f, 0.0f));
		break;

	default:
		break;
	}
}

void ABannerMarker::PaintBand(UStaticMeshComponent* Band, const FLinearColor& Colour)
{
	if (!Band)
	{
		return;
	}

	// BasicShapeMaterial exposes a single vector parameter named Color, confirmed by querying
	// the material's parameter list at runtime.
	if (UMaterialInterface* Source = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Source, this))
		{
			Dynamic->SetVectorParameterValue(TEXT("Color"), Colour);
			Band->SetMaterial(0, Dynamic);
		}
	}
}

void ABannerMarker::RebuildBanner()
{
	if (!Mast)
	{
		return;
	}

	constexpr float MastThickness = 45.f;

	Mast->SetRelativeScale3D(FVector(MastThickness / 100.f, MastThickness / 100.f, MastHeight / 100.f));
	Mast->SetRelativeLocation(FVector(0.f, 0.f, MastHeight * 0.5f));
	PaintBand(Mast, FLinearColor(0.18f, 0.18f, 0.2f));

	TArray<FLinearColor> Colours;
	GetBandColours(Faction, Colours);

	const float BandHeight = (Colours.Num() > 0) ? FlagHeight / Colours.Num() : 0.f;

	// Hangs from the top of the mast, offset to one side so it reads as a flag rather than as a
	// sign bolted through the pole.
	const float FlagTop = MastHeight - 60.f;

	for (int32 Index = 0; Index < Bands.Num(); ++Index)
	{
		UStaticMeshComponent* Band = Bands[Index];
		if (!Band)
		{
			continue;
		}

		if (!Colours.IsValidIndex(Index))
		{
			Band->SetVisibility(false);
			continue;
		}

		const float BandCentreZ = FlagTop - BandHeight * (Index + 0.5f);

		Band->SetVisibility(true);
		Band->SetRelativeScale3D(FVector(0.12f, FlagWidth / 100.f, BandHeight / 100.f));
		Band->SetRelativeLocation(FVector(0.f, FlagWidth * 0.5f + MastThickness * 0.5f, BandCentreZ));
		PaintBand(Band, Colours[Index]);
	}
}
