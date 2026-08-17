#pragma once

#include "CoreMinimal.h"
#include "DroneTarget.h"
#include "HelicopterTarget.generated.h"

/**
 * A transport helicopter flying a patrol circuit.
 *
 * Deliberately different in feel from the small UAVs: big, tough, and slow enough that catching
 * it is never the problem -- the problem is that it is a long way up, so you spend most of your
 * battery climbing and arrive with no energy left to correct.
 *
 * It does not jink. A transport aircraft cannot outmanoeuvre an FPV quad and pretending
 * otherwise would only be frustrating; it runs, gains height, and makes you commit.
 */
UCLASS()
class FPVDRONE_API AHelicopterTarget : public ADroneTarget
{
	GENERATED_BODY()

public:
	AHelicopterTarget();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Circle the scene instead of following waypoints.
	 *
	 * An orbit is far more legible than a route: it is always in view, always crossing, and
	 * always coming back around, so a missed pass costs a few seconds rather than the target
	 * disappearing over the horizon. It is also trivially predictable, which is what makes
	 * leading it a skill rather than a guess.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter|Orbit")
	bool bOrbit = true;

	/** Orbit radius in centimetres. 20000 is a 200 m circle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter|Orbit")
	float OrbitRadius = 20000.f;

	/** Height above the spawn point that the circle is flown at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter|Orbit")
	float OrbitAltitude = 5000.f;

	/** Offset of the circle's centre from the spawn point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter|Orbit")
	FVector OrbitCentreOffset = FVector::ZeroVector;

	/** Anticlockwise when false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter|Orbit")
	bool bOrbitClockwise = true;

	/**
	 * How far the centre of the circle wanders, centimetres.
	 *
	 * A fixed circle reads as a machine on rails after one lap. Drifting the centre on two
	 * different periods means the path never quite repeats, so it looks like an aircraft
	 * working an area rather than following a track -- while staying just as easy to intercept,
	 * because locally it is still a slow steady turn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter|Orbit")
	float CentreDriftDistance = 14000.f;

	/** Seconds for one full drift cycle. Long -- this should be barely perceptible moment to moment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter|Orbit")
	float CentreDriftPeriod = 80.f;

	/** Patrol circuit in actor-local space. Used only when bOrbit is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter")
	TArray<FVector> RoutePoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter")
	float CruiseSpeed = 1800.f;

	/** Speed once it knows it is being hunted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter")
	float RunSpeed = 2600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter")
	float DetectionRadius = 6000.f;

	/** Metres per second of climb once fleeing. Height is its real defence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter")
	float FleeClimbRate = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter")
	float WaypointTolerance = 900.f;

	/** Maximum roll into a turn. Banking is most of what sells rotary flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter")
	float MaxBankDegrees = 22.f;

	/** Nose-down attitude at cruise, as a real helicopter flies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter")
	float CruisePitchDegrees = -6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter")
	float BobAmplitude = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helicopter")
	float BobFrequency = 0.9f;

	UFUNCTION(BlueprintPure, Category = "Helicopter")
	bool IsFleeing() const { return bFleeing; }

	virtual FVector GetCurrentVelocity() const override { return Velocity; }

protected:
	virtual void OnDestroyed_Internal(AActor* Killer) override;

private:
	TArray<FVector> WorldRoute;
	int32 CurrentPoint = 0;
	bool bFleeing = false;

	FVector Velocity = FVector::ZeroVector;
	float BobPhase = 0.f;
	float CurrentBank = 0.f;

	FVector OrbitCentre = FVector::ZeroVector;
	float OrbitAngle = 0.f;
	float DriftPhase = 0.f;

	/**
	 * Eased rather than switched.
	 *
	 * Reading these straight off bFleeing moved the aircraft several metres in a single frame
	 * the instant the player crossed the detection radius. That is a teleport, and temporal
	 * anti-aliasing renders a teleport as two aircraft until it resolves.
	 */
	float CurrentOrbitRadius = 0.f;
	float CurrentSpeed = 0.f;
	float CurrentAltitude = 0.f;

	void TickOrbit(float DeltaSeconds);
	void TickRoute(float DeltaSeconds);

	/** Spin rate for the rotor disc, if the placeholder body is in use. */
	float RotorSpin = 0.f;
};
