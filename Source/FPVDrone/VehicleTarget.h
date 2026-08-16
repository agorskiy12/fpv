#pragma once

#include "CoreMinimal.h"
#include "DroneTarget.h"
#include "VehicleTarget.generated.h"

/**
 * A vehicle driving a fixed route.
 *
 * Route points are authored relative to wherever the actor is placed, so a patrol can be dropped
 * into a level and dragged around without re-authoring it. Movement is deliberately simple --
 * constant speed with a turn-rate limit -- because the interesting problem is hitting it, not
 * simulating it.
 */
UCLASS()
class FPVDRONE_API AVehicleTarget : public ADroneTarget
{
	GENERATED_BODY()

public:
	AVehicleTarget();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Route in actor-local space. Fewer than two points means it sits still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	TArray<FVector> RoutePoints;

	/** Cruise speed, cm/s. 1400 is about 50 km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	float Speed = 1400.f;

	/** Yaw rate limit, deg/s. Lower makes corners wider and the chase easier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	float TurnRateDegrees = 90.f;

	/** How close counts as reaching a route point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	float WaypointTolerance = 250.f;

	/** Loop the route, or stop at the end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	bool bLoopRoute = true;

	/** Speed up when the operator gets close. Makes the chase a chase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	bool bFleeWhenHunted = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	float FleeDetectionRadius = 4000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	float FleeSpeedMultiplier = 1.6f;

protected:
	virtual void OnDestroyed_Internal(AActor* Killer) override;

private:
	TArray<FVector> WorldRoute;
	int32 CurrentPoint = 0;
	bool bRouteFinished = false;

	FVector GetCurrentTargetPoint() const;
	void AdvanceRoutePoint();
	bool IsBeingHunted(float& OutDistance) const;
};
