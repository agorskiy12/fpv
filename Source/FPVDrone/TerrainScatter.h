#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TerrainScatter.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

/** One category of scattered mesh, and the rules for laying it out. */
USTRUCT()
struct FScatterLayer
{
	GENERATED_BODY()

	/** Meshes to pick from, chosen at random per instance. */
	TArray<UStaticMesh*> Meshes;

	/** Annulus around the centre that instances land in. */
	float InnerRadius = 0.f;
	float OuterRadius = 10000.f;

	int32 Count = 100;

	/**
	 * Longest axis each instance is normalised to, centimetres.
	 *
	 * Imported meshes arrive at whatever scale they were authored at, and this pack is no
	 * exception, so nothing is trusted to already be the right size.
	 */
	float TargetSize = 200.f;

	/** Random size variation around TargetSize. */
	float ScaleVariance = 0.35f;

	/**
	 * Surfaces nothing may be scattered onto -- the runway, and anything else man-made.
	 *
	 * Deliberately not a height cut-off. A height rule works only while the ground is a flat slab
	 * at a known Z; over real terrain the ground rises above any fixed threshold and the rule
	 * starts discarding perfectly good hillside.
	 */
	TArray<TWeakObjectPtr<AActor>> AvoidActors;

	/** Slope above which nothing is placed, degrees. Scrub does not grow on a cliff face. */
	float MaxSlopeDegrees = 40.f;

	/** Tilt toward the surface normal, 0 to 1. Rocks look planted; grass should stay upright. */
	float NormalAlignment = 0.f;

	/**
	 * Whether instances block movement.
	 *
	 * Rock and mountain should, because flying into one ought to end the sortie. Grass must not:
	 * clipping a blade at 130 km/h detonating the warhead would be absurd.
	 */
	bool bCollides = true;
};

/**
 * Scatters static meshes across the terrain as instanced geometry.
 *
 * Instanced rather than one actor per rock because the counts involved are in the hundreds, and
 * hundreds of individual actors each with their own transform and tick would cost far more than
 * the scenery is worth.
 *
 * Every instance is traced onto the ground rather than placed at a fixed height. That is what
 * makes the scatter survive the terrain changing underneath it -- a sculpted landscape, a
 * different ground plane, or anything placed later.
 */
UCLASS()
class FPVDRONE_API ATerrainScatter : public AActor
{
	GENERATED_BODY()

public:
	ATerrainScatter();

	/** Lay out one layer. Safe to call repeatedly; each call adds its own cluster. */
	void ScatterLayer(const FScatterLayer& Layer, const FVector& Centre, int32 Seed);

	/** Remove everything scattered so far. */
	void ClearScatter();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Scatter")
	TObjectPtr<USceneComponent> ScatterRoot;

	/** One component per mesh, since instancing requires a single mesh per component. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Clusters;

private:
	UInstancedStaticMeshComponent* GetOrCreateCluster(UStaticMesh* Mesh, bool bCollides);
};
