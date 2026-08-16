#include "DebrisChunk.h"
#include "FPVDrone.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Every live chunk, oldest first. */
	TArray<TWeakObjectPtr<ADebrisChunk>> GLiveChunks;
}

ADebrisChunk::ADebrisChunk()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}

	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetSimulatePhysics(true);
	Mesh->SetEnableGravity(true);

	// Debris must never interfere with flying. Colliding with the drone would turn a satisfying
	// kill into an unfair one, since the pieces arrive faster than anyone can react to.
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->SetNotifyRigidBodyCollision(false);
	Mesh->bReceivesDecals = false;

	// Chunks are small and numerous; shadows from each would cost more than they add.
	Mesh->SetCastShadow(false);

	InitialLifeSpan = 0.f;   // managed manually so the fade can run first
}

void ADebrisChunk::BeginPlay()
{
	Super::BeginPlay();
	RegisterChunk(this);
}

void ADebrisChunk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterChunk(this);
	Super::EndPlay(EndPlayReason);
}

void ADebrisChunk::RegisterChunk(ADebrisChunk* Chunk)
{
	GLiveChunks.RemoveAll([](const TWeakObjectPtr<ADebrisChunk>& Entry) { return !Entry.IsValid(); });
	GLiveChunks.Add(Chunk);

	// Recycle oldest-first once over budget.
	while (GLiveChunks.Num() > MaxLiveChunks)
	{
		TWeakObjectPtr<ADebrisChunk> Oldest = GLiveChunks[0];
		GLiveChunks.RemoveAt(0);
		if (Oldest.IsValid() && Oldest.Get() != Chunk)
		{
			Oldest->Destroy();
		}
	}
}

void ADebrisChunk::UnregisterChunk(ADebrisChunk* Chunk)
{
	GLiveChunks.RemoveAll([Chunk](const TWeakObjectPtr<ADebrisChunk>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Chunk;
	});
}

void ADebrisChunk::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;

	if (Age < Lifetime)
	{
		return;
	}

	// Sink through the floor rather than popping out of existence, which would be far more
	// noticeable than the disappearance itself.
	const float FadeAlpha = FMath::Clamp((Age - Lifetime) / FMath::Max(FadeDuration, KINDA_SMALL_NUMBER), 0.f, 1.f);

	if (Mesh)
	{
		Mesh->SetSimulatePhysics(false);
		AddActorWorldOffset(FVector(0.f, 0.f, -80.f * DeltaSeconds));
	}

	if (FadeAlpha >= 1.f)
	{
		Destroy();
	}
}

void ADebrisChunk::SpawnBurst(
	UWorld* World,
	const FVector& Origin,
	const FVector& InheritedVelocity,
	int32 Count,
	float SpreadSpeed,
	float ChunkSize,
	const TArray<UStaticMesh*>& ChunkMeshes)
{
	if (!World || Count <= 0)
	{
		return;
	}

	// Never let a single burst blow the entire budget; a chain reaction should still leave
	// something for the explosions that follow it.
	Count = FMath::Min(Count, MaxLiveChunks / 2);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 Index = 0; Index < Count; ++Index)
	{
		// Scattered around the origin rather than all from a point, so the burst has volume
		// from the first frame instead of expanding out of a singularity.
		const FVector Jitter = FMath::VRand() * FMath::FRandRange(0.f, ChunkSize * 2.f);

		ADebrisChunk* Chunk = World->SpawnActor<ADebrisChunk>(
			ADebrisChunk::StaticClass(), Origin + Jitter, FMath::VRand().Rotation(), Params);

		if (!Chunk || !Chunk->Mesh)
		{
			continue;
		}

		if (ChunkMeshes.Num() > 0)
		{
			if (UStaticMesh* Picked = ChunkMeshes[FMath::RandHelper(ChunkMeshes.Num())])
			{
				Chunk->Mesh->SetStaticMesh(Picked);
			}
		}

		// Varied sizes read as broken material; uniform sizes read as a particle system.
		const float Scale = ChunkSize * FMath::FRandRange(0.35f, 1.25f) / 100.f;
		Chunk->Mesh->SetRelativeScale3D(FVector(Scale));

		// Biased upward: debris driven into the ground is invisible, and the pieces that arc up
		// are the ones the player actually sees.
		FVector Direction = FMath::VRand();
		Direction.Z = FMath::Abs(Direction.Z) * 0.9f + 0.25f;
		Direction.Normalize();

		const FVector Velocity = InheritedVelocity + Direction * SpreadSpeed * FMath::FRandRange(0.45f, 1.35f);

		Chunk->Mesh->SetPhysicsLinearVelocity(Velocity);
		Chunk->Mesh->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * FMath::FRandRange(180.f, 900.f));

		// Stagger the despawns so the scene does not visibly clear all at once.
		Chunk->Lifetime = FMath::FRandRange(5.f, 9.f);
	}
}
