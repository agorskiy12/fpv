#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FPVWarGameMode.generated.h"

class ADroneTarget;
class AFPVDronePawn;
class AStrikeCamera;

/** One target killed by a single strike, captured for the after-action report. */
USTRUCT(BlueprintType)
struct FStrikeKill
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Strike")
	FString TargetName;

	UPROPERTY(BlueprintReadOnly, Category = "Strike")
	int32 Score = 0;

	/** True when the kill came from a chain reaction rather than the drone's own warhead. */
	UPROPERTY(BlueprintReadOnly, Category = "Strike")
	bool bSecondary = false;
};

/** Where the post-detonation sequence has got to. */
UENUM(BlueprintType)
enum class EStrikeState : uint8
{
	/** Normal flight. */
	Flying,

	/** Third-person camera on the blast, no text yet. */
	KillCam,

	/** Camera still running, after-action report on screen. */
	Report,

	/** Blending back to a fresh drone. */
	Respawning
};

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
	virtual void Tick(float DeltaSeconds) override;

	// --- Blast ------------------------------------------------------------------------------

	/**
	 * Damage every target within Radius, falling off with distance.
	 *
	 * @param IgnoreActor target that caused the blast, so a chain reaction cannot re-damage itself.
	 */
	void ApplyBlast(const FVector& Origin, float Radius, float Damage, AActor* DamageCauser, AActor* IgnoreActor = nullptr);

	/** Called by a target as it dies. */
	void NotifyTargetDestroyed(ADroneTarget* Target, AActor* Killer);

	/**
	 * Add a target to the mission.
	 *
	 * Targets register themselves on BeginPlay rather than only being swept up by the game
	 * mode at level start, so anything spawned at runtime still counts toward objectives.
	 */
	void RegisterTarget(ADroneTarget* Target);

	/**
	 * Called by the pawn when its warhead goes off. Runs the whole strike sequence: applies the
	 * blast while recording what it killed, cuts to a third-person camera, shows the report, and
	 * only then issues a fresh drone.
	 *
	 * The blast is applied from here rather than by the pawn so kills can be attributed to this
	 * specific strike -- chain reactions resolve synchronously inside ApplyBlast, so everything
	 * this warhead was responsible for lands inside the recording window.
	 */
	void NotifyDroneDetonated(AFPVDronePawn* Drone, const FVector& BlastLocation, const FVector& ApproachDirection,
		float BlastRadius, float BlastDamage);

	// --- Strike sequence --------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Strike")
	EStrikeState GetStrikeState() const { return StrikeState; }

	UFUNCTION(BlueprintPure, Category = "Strike")
	bool IsShowingStrikeReport() const { return StrikeState == EStrikeState::Report; }

	/** What the last strike destroyed. */
	const TArray<FStrikeKill>& GetStrikeKills() const { return StrikeKills; }

	UFUNCTION(BlueprintPure, Category = "Strike")
	int32 GetStrikeScore() const { return StrikeScore; }

	/** Seconds left in the current phase, for a progress cue. */
	UFUNCTION(BlueprintPure, Category = "Strike")
	float GetStrikePhaseRemaining() const { return FMath::Max(0.f, PhaseEndTime - GetWorld()->GetTimeSeconds()); }

	/** How long the camera runs before the report appears. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Strike")
	float KillCamDuration = 2.2f;

	/** How long the report stays up before a new drone is issued. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Strike")
	float ReportDuration = 3.4f;

	/**
	 * Time dilation applied at the instant of detonation.
	 *
	 * Brief, and it does two jobs: it gives weight to the hit, and it buys the eye a moment to
	 * register what was struck before the camera cuts away.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Strike")
	float ImpactTimeDilation = 0.35f;

	/** Real seconds the slowdown lasts. Long enough to feel, short enough not to be a cutscene. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Strike")
	float ImpactSlowMoDuration = 0.18f;

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
	void EnterStrikeState(EStrikeState NewState, float Duration);
	void FinishStrikeSequence();

	/** True while a skip key is held, so the sequence can be cut short. */
	bool WantsToSkipStrikeSequence() const;

	int32 Score = 0;
	int32 TargetsDestroyed = 0;
	int32 DronesRemaining = -1;
	bool bMissionComplete = false;

	// Strike sequence
	EStrikeState StrikeState = EStrikeState::Flying;
	float PhaseEndTime = 0.f;
	TArray<FStrikeKill> StrikeKills;
	int32 StrikeScore = 0;

	/** Set while a blast is resolving, so destroyed targets attribute to the current strike. */
	bool bRecordingStrike = false;

	/** True once the drone's own blast has resolved, marking later kills as chain reactions. */
	bool bPrimaryBlastResolved = false;

	UPROPERTY(Transient)
	TObjectPtr<AStrikeCamera> ActiveStrikeCamera;

	/** Tracked in real seconds, because dilated time cannot be used to time its own recovery. */
	float SlowMoEndRealTime = 0.f;
	bool bSlowMoActive = false;
};
