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

	/**
	 * Fly chained parabolic arcs instead of a waypoint circuit.
	 *
	 * Waypoint patrols spend most of their time turning, which reads as indecision and makes
	 * the drone hard to lead. A ballistic arc is the opposite: long, smooth, and completely
	 * predictable once you have watched one, so intercepting it becomes a matter of judging
	 * the curve rather than reacting to a direction change.
	 *
	 * Each arc begins where the last one ended, turned by a modest angle, so the path is
	 * continuous and never snaps.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone|Arc")
	bool bParabolicFlight = true;

	/** Horizontal distance covered by one arc, centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone|Arc")
	float ArcLength = 9000.f;

	/** Height gained at the apex, above the arc's start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone|Arc")
	float ArcHeight = 1800.f;

	/** Maximum heading change between consecutive arcs, degrees. Small keeps the path readable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone|Arc")
	float ArcTurnVariance = 32.f;

	/** Altitude the arcs are anchored to; the drone never descends below this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy Drone|Arc")
	float MinimumAltitude = 800.f;

	/** Patrol route in actor-local space. Used only when bParabolicFlight is false. */
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

	virtual FVector GetCurrentVelocity() const override { return Velocity; }

protected:
	virtual void OnDestroyed_Internal(AActor* Killer) override;

private:
	TArray<FVector> WorldRoute;
	int32 CurrentPoint = 0;
	bool bEvading = false;

	FVector Velocity = FVector::ZeroVector;
	float BobPhase = 0.f;

	// Arc state
	FVector ArcStart = FVector::ZeroVector;
	FVector ArcDirection = FVector::ForwardVector;
	float ArcProgress = 0.f;
	float ArcDuration = 6.f;
	float CurrentArcHeight = 1800.f;

	void BeginNewArc(const FVector& FromLocation);
	void TickParabolicFlight(float DeltaSeconds);
	void TickRoutePatrol(float DeltaSeconds);

	FVector ComputePatrolDirection(const FVector& Location) const;
	FVector ComputeEvadeDirection(const FVector& Location, const FVector& ThreatLocation) const;
};
