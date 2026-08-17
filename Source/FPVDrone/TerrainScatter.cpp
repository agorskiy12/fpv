#include "TerrainScatter.h"
#include "FPVDrone.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

ATerrainScatter::ATerrainScatter()
{
	PrimaryActorTick.bCanEverTick = false;

	ScatterRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ScatterRoot"));
	SetRootComponent(ScatterRoot);
}

void ATerrainScatter::ClearScatter()
{
	for (UInstancedStaticMeshComponent* Cluster : Clusters)
	{
		if (Cluster)
		{
			Cluster->DestroyComponent();
		}
	}
	Clusters.Reset();
}

UInstancedStaticMeshComponent* ATerrainScatter::GetOrCreateCluster(UStaticMesh* Mesh, bool bCollides)
{
	if (!Mesh)
	{
		return nullptr;
	}

	for (UInstancedStaticMeshComponent* Cluster : Clusters)
	{
		if (Cluster && Cluster->GetStaticMesh() == Mesh)
		{
			return Cluster;
		}
	}

	UInstancedStaticMeshComponent* Cluster = NewObject<UInstancedStaticMeshComponent>(this);
	Cluster->SetStaticMesh(Mesh);
	Cluster->SetupAttachment(ScatterRoot);
	Cluster->SetMobility(EComponentMobility::Movable);

	Cluster->SetCollisionProfileName(bCollides ? TEXT("BlockAllDynamic") : TEXT("NoCollision"));
	Cluster->SetCollisionEnabled(bCollides ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	Cluster->RegisterComponent();
	Clusters.Add(Cluster);
	return Cluster;
}

void ATerrainScatter::ScatterLayer(const FScatterLayer& Layer, const FVector& Centre, int32 Seed)
{
	UWorld* World = GetWorld();
	if (!World || Layer.Meshes.Num() == 0 || Layer.Count <= 0)
	{
		return;
	}

	// Seeded, so a given layout is reproducible between runs. Randomly regenerating scenery
	// every launch makes it impossible to tell a tuning change from a reroll.
	FRandomStream Random(Seed);

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(TerrainScatter), /*bTraceComplex=*/true);
	TraceParams.AddIgnoredActor(this);

	int32 Placed = 0;
	int32 NoGround = 0;
	int32 OnAvoided = 0;
	int32 TooSteep = 0;

	// What the discarded instances actually landed on. Rejection counts alone cannot distinguish
	// "correctly kept off the airfield" from "the ground is not where it is assumed to be".
	TMap<FName, int32> BlockedBy;

	for (int32 Index = 0; Index < Layer.Count; ++Index)
	{
		// Uniform over the annulus rather than over the radius. Sampling the radius linearly
		// bunches everything toward the middle, which reads as a deliberate ring.
		const float Angle = Random.FRandRange(0.f, 2.f * PI);
		const float R = FMath::Sqrt(Random.FRandRange(
			Layer.InnerRadius * Layer.InnerRadius, Layer.OuterRadius * Layer.OuterRadius));

		const FVector Candidate(
			Centre.X + FMath::Cos(Angle) * R,
			Centre.Y + FMath::Sin(Angle) * R,
			Centre.Z);

		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(
			Hit,
			Candidate + FVector(0.f, 0.f, 50000.f),
			Candidate - FVector(0.f, 0.f, 50000.f),
			ECC_Visibility, TraceParams);

		if (!bHit)
		{
			++NoGround;
			continue;
		}

		AActor* HitActor = Hit.GetActor();
		if (Layer.AvoidActors.ContainsByPredicate(
			[HitActor](const TWeakObjectPtr<AActor>& Avoid) { return Avoid.Get() == HitActor; }))
		{
			++OnAvoided;
			BlockedBy.FindOrAdd(HitActor ? HitActor->GetFName() : NAME_None)++;
			continue;
		}

		if (FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Hit.ImpactNormal.Z, -1.f, 1.f))) > Layer.MaxSlopeDegrees)
		{
			++TooSteep;
			continue;
		}

		UStaticMesh* Mesh = Layer.Meshes[Random.RandRange(0, Layer.Meshes.Num() - 1)];
		if (!Mesh)
		{
			continue;
		}

		// Normalised from the mesh's own bounds, so an import at any authoring scale lands at
		// the size the layer asked for.
		const float LongestAxis = Mesh->GetBoundingBox().GetSize().GetMax();
		const float BaseScale = (LongestAxis > KINDA_SMALL_NUMBER) ? Layer.TargetSize / LongestAxis : 1.f;
		const float Scale = BaseScale * Random.FRandRange(1.f - Layer.ScaleVariance, 1.f + Layer.ScaleVariance);

		// Leaning into the slope makes rock read as part of the terrain rather than dropped on
		// top of it. Fully aligning looks wrong on anything with a clear upright axis.
		const FVector Up = FMath::Lerp(FVector::UpVector, Hit.ImpactNormal, Layer.NormalAlignment).GetSafeNormal();
		FRotator Rotation = FRotationMatrix::MakeFromZ(Up).Rotator();
		Rotation.Yaw += Random.FRandRange(0.f, 360.f);

		// Sunk very slightly, so nothing appears to be balancing on the surface.
		const FVector Location = Hit.ImpactPoint - FVector(0.f, 0.f, Layer.TargetSize * 0.04f);

		if (UInstancedStaticMeshComponent* Cluster = GetOrCreateCluster(Mesh, Layer.bCollides))
		{
			Cluster->AddInstance(FTransform(Rotation, Location, FVector(Scale)), /*bWorldSpace=*/true);
			++Placed;
		}
	}

	UE_LOG(LogFPV, Log,
		TEXT("Scatter: %d placed (%d no ground, %d on avoided surface, %d too steep) -- %.0f-%.0f m ring, %.0f cm"),
		Placed, NoGround, OnAvoided, TooSteep,
		Layer.InnerRadius / 100.f, Layer.OuterRadius / 100.f, Layer.TargetSize);
}
