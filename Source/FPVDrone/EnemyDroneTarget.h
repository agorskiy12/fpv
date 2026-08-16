#pragma once

#include "CoreMinimal.h"
#include "DroneTarget.h"
#include "EnemyDroneTarget.generated.h"

/**
 * An enemy UAV to intercept.
 *
 * Patrols a route in three dimensions and breaks away when the operator closes in. The evasion
 * is intentionally crude -- run perpendicular, gain height, add a wobble -- because a genuinely
 * good evader is impossible to catch with a rate-mode quad, and the point is a chase the player
 * can win.
 */
UCLASS()
class FPVDRONE_API AEnemyDroneTarget : public ADroneTarget
{
	GENERATED_BODY()

public:
	AEnemyDroneTarget();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Patrol route in actor-local space, flown in order and looped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone")
	TArray<FVector> RoutePoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone")
	float PatrolSpeed = 900.f;

	/** Speed while running away. Kept below a quad's top speed so the chase is winnable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone")
	float EvadeSpeed = 1700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone")
	float DetectionRadius = 3500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone")
	float WaypointTolerance = 300.f;

	/** Amplitude of the idle bob, so it never looks pinned in the air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone")
	float BobAmplitude = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone")
	float BobFrequency = 1.6f;

	/** True while it is running from the operator -- the HUD flags this. */
	UFUNCTION(BlueprintPure, Category = "Enemy Drone")
	bool IsEvading() const { return bEvading; }

protected:
	virtual void OnDestroyed_Internal(AActor* Killer) override;

private:
	TArray<FVector> WorldRoute;
	int32 CurrentPoint = 0;
	bool bEvading = false;

	FVector Velocity = FVector::ZeroVector;
	float BobPhase = 0.f;

	FVector ComputePatrolDirection(const FVector& Location) const;
	FVector ComputeEvadeDirection(const FVector& Location, const FVector& ThreatLocation) const;
};
