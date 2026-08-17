#pragma once

#include "CoreMinimal.h"
#include "DroneTarget.h"
#include "SoldierTarget.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;

/** What a given soldier is there for. */
UENUM(BlueprintType)
enum class ESoldierRole : uint8
{
	/** Scenery. Killable, worth points, but does not gate the mission. */
	Guard,

	/** Counts toward mission completion. */
	Objective
};

/**
 * Infantry on foot.
 *
 * The first target built on a skeletal mesh rather than static geometry, which is why it does
 * not simply set BodyMesh like everything else: different component, different animation
 * pipeline, and a physics asset that makes ragdoll the right death rather than a debris burst.
 *
 * People moving through a scene are what make it read as inhabited. Even purely as guards that
 * never factor into scoring, a patrol crossing the runway does more for the place than another
 * static prop would.
 */
UCLASS()
class FPVDRONE_API ASoldierTarget : public ADroneTarget
{
	GENERATED_BODY()

public:
	ASoldierTarget();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Guard or objective. Objectives must be cleared for the sortie to complete. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier")
	ESoldierRole SoldierRole = ESoldierRole::Guard;

	/** Patrol route in actor-local space. A single point means standing watch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier")
	TArray<FVector> PatrolPoints;

	/** Walking pace, cm/s. 150 is a little over 5 km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier")
	float WalkSpeed = 150.f;

	/** Pace once a drone has been noticed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier")
	float RunSpeed = 420.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier")
	float TurnRateDegrees = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier")
	float WaypointTolerance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier")
	bool bLoopPatrol = true;

	/** How close a drone gets before they break into a run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier")
	float AlertRadius = 2500.f;

	/** Standing height in centimetres, used to normalise the imported mesh scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier|Visuals")
	float StandingHeight = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier|Visuals")
	bool bAutoScaleMesh = true;

	/** Correction if the model does not face along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier|Visuals")
	FRotator MeshFacingCorrection = FRotator(0.f, -90.f, 0.f);

	/**
	 * Draw a faction-coloured cube instead of the character model.
	 *
	 * On by default while identification is being designed. The eventual game identifies sides by
	 * uniform markings, which is a skill; a coloured block is the opposite of that. But the
	 * soldier pack shipped without textures, so the model currently reads as grey either way --
	 * and until markings exist, unambiguous colour is what makes faction behaviour testable at
	 * all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soldier|Visuals")
	bool bUseFactionCube = true;

	/** Colour of the placeholder. Overridden by subclasses that need to stand out. */
	virtual FLinearColor GetPlaceholderColour() const;

	UPROPERTY(BlueprintReadOnly, Category = "Soldier")
	bool bAlerted = false;

	virtual FVector GetCurrentVelocity() const override;

protected:
	virtual void OnDestroyed_Internal(AActor* Killer) override;

	/** Drawn by a skeletal mesh, so the primitive body is suppressed. */
	virtual bool HasCustomVisual() const override { return true; }

	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<USkeletalMeshComponent> SoldierMesh;

	UPROPERTY(VisibleAnywhere, Category = "Soldier")
	TObjectPtr<UStaticMeshComponent> PlaceholderCube;

	void ApplyPlaceholderCube();

	UPROPERTY(EditAnywhere, Category = "Soldier|Animation")
	TObjectPtr<UAnimSequence> WalkAnimation;

	UPROPERTY(EditAnywhere, Category = "Soldier|Animation")
	TObjectPtr<UAnimSequence> RunAnimation;

private:
	TArray<FVector> WorldRoute;
	int32 CurrentPoint = 0;
	bool bPatrolFinished = false;
	bool bRunningAnimActive = false;

	void UpdateLocomotionAnimation();
	void CollapseIntoRagdoll(const FVector& BlastOrigin);
};
