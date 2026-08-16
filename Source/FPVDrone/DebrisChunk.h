#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DebrisChunk.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * One physically simulated piece of a destroyed target.
 *
 * Chunks are generic rather than fractured from the source mesh. That is a deliberate trade:
 * generic debris works on every target in the game today at a fraction of the cost of Chaos
 * geometry collections, and at the speed these are moving the difference is largely invisible.
 * Real fracture is worth it only for hero objects.
 *
 * A hard global cap is enforced, recycling oldest-first. Without one, a single good chain
 * reaction puts hundreds of rigid bodies in the scene and the frame budget disappears.
 */
UCLASS()
class FPVDRONE_API ADebrisChunk : public AActor
{
	GENERATED_BODY()

public:
	ADebrisChunk();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Throw a burst of debris out of a destroyed target.
	 *
	 * @param Origin          centre of the burst
	 * @param InheritedVelocity the target's own velocity at the moment it died. A helicopter
	 *                        destroyed mid-orbit must throw its wreckage forward along the
	 *                        flight path; without this the debris reads as detached from it.
	 * @param ChunkMeshes     optional real meshes; falls back to scaled cubes when empty
	 */
	static void SpawnBurst(
		UWorld* World,
		const FVector& Origin,
		const FVector& InheritedVelocity,
		int32 Count,
		float SpreadSpeed,
		float ChunkSize,
		const TArray<UStaticMesh*>& ChunkMeshes);

	/** Live chunks are capped globally; the oldest are destroyed to make room. */
	static constexpr int32 MaxLiveChunks = 140;

	/** Seconds before a chunk starts sinking away. */
	UPROPERTY(EditAnywhere, Category = "Debris")
	float Lifetime = 7.f;

	/** How long the sink-and-vanish takes. */
	UPROPERTY(EditAnywhere, Category = "Debris")
	float FadeDuration = 1.5f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Debris")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	float Age = 0.f;

	static void RegisterChunk(ADebrisChunk* Chunk);
	static void UnregisterChunk(ADebrisChunk* Chunk);
};
