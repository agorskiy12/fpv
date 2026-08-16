#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FPVWarGameMode.generated.h"

class ADroneTarget;
class AFPVDronePawn;

/**
 * Mission director for a strike sortie.
 *
 * Owns the target list, applies blast damage with distance falloff, keeps score, and manages the
 * supply of drones. Because the drone is the munition, "dying" is the normal way a run ends --
 * so respawn is the core loop rather than a failure path.
 */
UCLASS()
class FPVDRONE_API AFPVWarGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFPVWarGameMode();

	virtual void BeginPlay() override;

	// --- Blast ------------------------------------------------------------------------------

	/**
	 * Damage every target within Radius, falling off with distance.
	 *
	 * @param IgnoreActor target that caused the blast, so a chain reaction cannot re-damage itself.
	 */
	void ApplyBlast(const FVector& Origin, float Radius, float Damage, AActor* DamageCauser, AActor* IgnoreActor = nullptr);

	/** Called by a target as it dies. */
	void NotifyTargetDestroyed(ADroneTarget* Target, AActor* Killer);

	/** Called by the pawn when its warhead goes off. Consumes a drone and queues a respawn. */
	void NotifyDroneExpended(AFPVDronePawn* Drone);

	// --- Mission state ----------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Mission")
	int32 GetScore() const { return Score; }

	UFUNCTION(BlueprintPure, Category = "Mission")
	int32 GetTargetsDestroyed() const { return TargetsDestroyed; }

	UFUNCTION(BlueprintPure, Category = "Mission")
	int32 GetPrimaryTargetsRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Mission")
	int32 GetTotalTargets() const { return AllTargets.Num(); }

	/** Drones left in the crate. -1 means unlimited. */
	UFUNCTION(BlueprintPure, Category = "Mission")
	int32 GetDronesRemaining() const { return DronesRemaining; }

	UFUNCTION(BlueprintPure, Category = "Mission")
	bool IsMissionComplete() const { return bMissionComplete; }

	UFUNCTION(BlueprintPure, Category = "Mission")
	bool IsOutOfDrones() const { return DronesRemaining == 0 && !bMissionComplete; }

	/** Seconds until the next drone is in the air, or 0 if one already is. */
	UFUNCTION(BlueprintPure, Category = "Mission")
	float GetRespawnCountdown() const;

	/** Nearest live target to a point, for the HUD marker. */
	UFUNCTION(BlueprintPure, Category = "Mission")
	ADroneTarget* FindNearestLiveTarget(const FVector& FromLocation) const;

	/** Every target still alive, for HUD listing. */
	void GetLiveTargets(TArray<ADroneTarget*>& OutTargets) const;

	/** Drones available at mission start. -1 for unlimited. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Mission")
	int32 StartingDrones = -1;

	/** Delay before the next drone spawns after a detonation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Mission")
	float RespawnDelay = 2.0f;

protected:
	UPROPERTY(Transient)
	TArray<TObjectPtr<ADroneTarget>> AllTargets;

private:
	void RespawnDrone();

	int32 Score = 0;
	int32 TargetsDestroyed = 0;
	int32 DronesRemaining = -1;
	bool bMissionComplete = false;

	float RespawnAtTime = 0.f;
	bool bRespawnPending = false;

	FTimerHandle RespawnTimer;
};
