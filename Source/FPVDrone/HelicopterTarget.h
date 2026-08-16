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

	/** Patrol circuit in actor-local space, flown in order and looped. */
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

protected:
	virtual void OnDestroyed_Internal(AActor* Killer) override;

private:
	TArray<FVector> WorldRoute;
	int32 CurrentPoint = 0;
	bool bFleeing = false;

	FVector Velocity = FVector::ZeroVector;
	float BobPhase = 0.f;
	float CurrentBank = 0.f;

	/** Spin rate for the rotor disc, if the placeholder body is in use. */
	float RotorSpin = 0.f;
};
