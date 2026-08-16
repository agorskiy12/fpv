#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroneTarget.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/** What kind of thing this is, which drives its shape, toughness and score. */
UENUM(BlueprintType)
enum class ETargetKind : uint8
{
	/** Buildings, depots, bunkers. Tough, stationary. */
	Structure,

	/** Pipeline runs. Fragile, and they take their neighbours with them. */
	GasLine,

	/** Transformers and switchgear. Moderate toughness, high value. */
	ElectricalStation,

	/** Cars and trucks. Fast, fragile, hard to hit. */
	Vehicle,

	/** Enemy UAVs. Very fragile, but they move in three dimensions. */
	Drone
};

/**
 * Anything the operator can destroy.
 *
 * Bodies are assembled from engine primitives at construction, the same approach the race gates
 * used, so a level can be laid out with no imported content. Shapes are blocky stand-ins --
 * legible enough to identify from the air, which is all the flying actually needs.
 */
UCLASS()
class FPVDRONE_API ADroneTarget : public AActor
{
	GENERATED_BODY()

public:
	ADroneTarget();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	ETargetKind Kind = ETargetKind::Structure;

	/** Damage absorbed before destruction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	float MaxHealth = 100.f;

	/** Points awarded for killing it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	int32 ScoreValue = 100;

	/** Counts toward mission completion. Clear secondary targets for score only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	bool bPrimaryObjective = true;

	/** Detonates on death, damaging whatever is nearby. Pipelines and fuel chain; cars do not. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target|Secondary")
	bool bSecondaryExplosion = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target|Secondary")
	float SecondaryBlastRadius = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target|Secondary")
	float SecondaryBlastDamage = 160.f;

	/** Overall size in centimetres. Interpreted per kind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	FVector BodySize = FVector(600.f, 400.f, 300.f);

	/** Apply blast damage, already scaled for distance by the caller. */
	UFUNCTION(BlueprintCallable, Category = "Target")
	void ApplyBlastDamage(float Damage, const FVector& BlastOrigin, AActor* DamageCauser);

	UFUNCTION(BlueprintPure, Category = "Target")
	bool IsDestroyed() const { return bDestroyed; }

	UFUNCTION(BlueprintPure, Category = "Target")
	float GetHealthFraction() const { return (MaxHealth > 0.f) ? (Health / MaxHealth) : 0.f; }

	/** Aim point for HUD markers -- the centre of mass, not the actor origin. */
	UFUNCTION(BlueprintPure, Category = "Target")
	virtual FVector GetAimPoint() const;

	/** Short label for the HUD. */
	UFUNCTION(BlueprintPure, Category = "Target")
	FString GetDisplayName() const;

	/** Radius used for HUD marker sizing and proximity checks. */
	UFUNCTION(BlueprintPure, Category = "Target")
	float GetApproximateRadius() const;

protected:
	/** Called once health reaches zero. Subclasses stop their movement here. */
	virtual void OnDestroyed_Internal(AActor* Killer);

	/** Rebuild the body for the current Kind and BodySize. */
	void RebuildBody();

	UPROPERTY(VisibleAnywhere, Category = "Target")
	TObjectPtr<USceneComponent> TargetRoot;

	/** Parts are generic so one actor class can express every kind. */
	UPROPERTY(VisibleAnywhere, Category = "Target")
	TArray<TObjectPtr<UStaticMeshComponent>> BodyParts;

	static constexpr int32 NumBodyParts = 6;

	float Health = 100.f;
	bool bDestroyed = false;

	/** Configure one part; parts left unconfigured are hidden. */
	void SetPart(int32 Index, const FVector& RelativeLocation, const FVector& SizeCm, bool bCylinder = false);

private:
	int32 PartsUsed = 0;
	void HideUnusedParts();
};
