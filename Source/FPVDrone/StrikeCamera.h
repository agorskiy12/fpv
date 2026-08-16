#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ImpactShake.h"
#include "StrikeCamera.generated.h"

class UCameraComponent;

/**
 * Third-person camera that frames a detonation.
 *
 * Exists because the strike is the payoff and the operator never sees it: the FPV camera is
 * rigidly bolted to a drone that has just been destroyed. Cutting away for a couple of seconds
 * is the only way to actually show what the run achieved.
 *
 * It drifts slowly around the blast rather than sitting still, which keeps the shot alive while
 * the smoke clears and avoids reading as a freeze.
 */
UCLASS()
class FPVDRONE_API AStrikeCamera : public AActor
{
	GENERATED_BODY()

public:
	AStrikeCamera();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Place a camera looking at a detonation.
	 *
	 * @param ApproachDirection the drone's heading at impact. The camera sets up off to one side
	 *        of it, so the shot reads as observing the strike rather than replaying the flight.
	 */
	static AStrikeCamera* Spawn(UWorld* World, const FVector& BlastLocation, const FVector& ApproachDirection, float BlastRadius);

	/** Distance from the blast, as a multiple of blast radius. */
	UPROPERTY(EditAnywhere, Category = "Strike Camera")
	float DistanceMultiplier = 3.2f;

	UPROPERTY(EditAnywhere, Category = "Strike Camera")
	float MinDistance = 1200.f;

	/** Height above the blast, as a fraction of the viewing distance. */
	UPROPERTY(EditAnywhere, Category = "Strike Camera")
	float HeightFraction = 0.45f;

	/** Slow drift around the blast, deg/s. */
	UPROPERTY(EditAnywhere, Category = "Strike Camera")
	float OrbitRateDegrees = 9.f;

	/** Gentle push in, which reads as interest without becoming a zoom. */
	UPROPERTY(EditAnywhere, Category = "Strike Camera")
	float DollyInRate = 0.045f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Strike Camera")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Strike Camera")
	TObjectPtr<UCameraComponent> Camera;

private:
	FVector FocusPoint = FVector::ZeroVector;
	float Distance = 2000.f;
	float Angle = 0.f;
	float Height = 800.f;

	/** Shaken on spawn, so the cut lands on the concussion rather than on a steady shot. */
	FImpactShake Shake;

	/** Blown-out exposure and bloom that decays over the first moments of the blast. */
	float Flash = 0.f;
};
