#include "FPVWarGameMode.h"
#include "DroneTarget.h"
#include "EnemyDroneTarget.h"
#include "FPVDrone.h"
#include "FPVDronePawn.h"
#include "FPVHUD.h"
#include "HelicopterTarget.h"
#include "StrikeCamera.h"
#include "VehicleTarget.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
	/**
	 * Drop a representative mission around the player.
	 *
	 * A scratch tool for tuning blast radius, damage and evade speeds without authoring a level
	 * first. Delete it once there are real maps.
	 */
	void SpawnTestTargets(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		const APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
		const FVector Origin = Player ? FVector(Player->GetActorLocation().X, Player->GetActorLocation().Y, 0.f)
									  : FVector::ZeroVector;

		auto SpawnTarget = [World](TSubclassOf<ADroneTarget> Class, const FVector& Location,
			const FRotator& Rotation, TFunctionRef<void(ADroneTarget*)> Configure) -> ADroneTarget*
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			// Deferred so properties are set before OnConstruction builds the body.
			ADroneTarget* Target = World->SpawnActorDeferred<ADroneTarget>(
				Class, FTransform(Rotation, Location), nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

			if (Target)
			{
				Configure(Target);
				Target->FinishSpawning(FTransform(Rotation, Location));
			}
			return Target;
		};

		// Two structures at different ranges, to check blast falloff reads sensibly.
		SpawnTarget(ADroneTarget::StaticClass(), Origin + FVector(4000.f, -2500.f, 0.f), FRotator::ZeroRotator,
			[](ADroneTarget* T) { T->Kind = ETargetKind::Structure; T->BodySize = FVector(900.f, 700.f, 500.f); T->MaxHealth = 140.f; });

		SpawnTarget(ADroneTarget::StaticClass(), Origin + FVector(9000.f, 3000.f, 0.f), FRotator(0.f, 35.f, 0.f),
			[](ADroneTarget* T) { T->Kind = ETargetKind::Structure; T->BodySize = FVector(1200.f, 800.f, 700.f); T->MaxHealth = 200.f; });

		// Gas line: thin and chains hard, so it rewards precision.
		SpawnTarget(ADroneTarget::StaticClass(), Origin + FVector(5500.f, 5500.f, 0.f), FRotator(0.f, 90.f, 0.f),
			[](ADroneTarget* T)
			{
				T->Kind = ETargetKind::GasLine;
				T->BodySize = FVector(3000.f, 200.f, 240.f);
				T->MaxHealth = 45.f;
				T->bSecondaryExplosion = true;
				T->SecondaryBlastRadius = 2200.f;
				T->SecondaryBlastDamage = 220.f;
				T->ScoreValue = 300;
			});

		// Substation sits near the gas line, so a good pipeline hit should take it too.
		SpawnTarget(ADroneTarget::StaticClass(), Origin + FVector(6800.f, 6200.f, 0.f), FRotator::ZeroRotator,
			[](ADroneTarget* T) { T->Kind = ETargetKind::ElectricalStation; T->BodySize = FVector(800.f, 800.f, 600.f); T->MaxHealth = 110.f; T->ScoreValue = 350; });

		// A long straight run for the car, so the chase has room.
		SpawnTarget(AVehicleTarget::StaticClass(), Origin + FVector(-3000.f, 6000.f, 0.f), FRotator::ZeroRotator,
			[](ADroneTarget* T)
			{
				if (AVehicleTarget* V = Cast<AVehicleTarget>(T))
				{
					V->RoutePoints = { FVector::ZeroVector, FVector(14000.f, 0.f, 0.f), FVector(14000.f, 5000.f, 0.f), FVector(0.f, 5000.f, 0.f) };
					V->Speed = 1500.f;
				}
			});

		SpawnTarget(AVehicleTarget::StaticClass(), Origin + FVector(2000.f, -6500.f, 0.f), FRotator(0.f, 180.f, 0.f),
			[](ADroneTarget* T)
			{
				if (AVehicleTarget* V = Cast<AVehicleTarget>(T))
				{
					V->RoutePoints = { FVector::ZeroVector, FVector(10000.f, 0.f, 0.f) };
					V->Speed = 1900.f;
					V->bPrimaryObjective = false;   // secondary: score only
				}
			});

		// Two UAVs at different heights.
		SpawnTarget(AEnemyDroneTarget::StaticClass(), Origin + FVector(0.f, 0.f, 2200.f), FRotator::ZeroRotator,
			[](ADroneTarget* T) {});

		SpawnTarget(AEnemyDroneTarget::StaticClass(), Origin + FVector(-6000.f, -2000.f, 3400.f), FRotator(0.f, 120.f, 0.f),
			[](ADroneTarget* T)
			{
				if (AEnemyDroneTarget* D = Cast<AEnemyDroneTarget>(T))
				{
					D->PatrolSpeed = 1200.f;
					D->bPrimaryObjective = false;
				}
			});

		// The transport heli, high and well clear. Kept far out deliberately: it is large, and
		// spawning anything large near the player start risks the drone starting inside it.
		SpawnTarget(AHelicopterTarget::StaticClass(), Origin + FVector(-20000.f, -20000.f, 9000.f), FRotator(0.f, 45.f, 0.f),
			[](ADroneTarget* T) {});

		UE_LOG(LogFPV, Log, TEXT("Spawned test targets around %s"), *Origin.ToCompactString());
	}

	FAutoConsoleCommandWithWorld CmdSpawnTestTargets(
		TEXT("fpv.SpawnTestTargets"),
		TEXT("Drop a mixed set of targets around the player: structures, a gas line, a substation, two vehicles and two UAVs."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&SpawnTestTargets));
}

AFPVWarGameMode::AFPVWarGameMode()
{
	DefaultPawnClass = AFPVDronePawn::StaticClass();
	HUDClass = AFPVHUD::StaticClass();

	// Ticks to drive the post-strike sequence.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AFPVWarGameMode::BeginPlay()
{
	Super::BeginPlay();

	AllTargets.Reset();
	for (TActorIterator<ADroneTarget> It(GetWorld()); It; ++It)
	{
		AllTargets.Add(*It);
	}

	DronesRemaining = StartingDrones;

	int32 PrimaryCount = 0;
	for (const ADroneTarget* Target : AllTargets)
	{
		if (Target && Target->bPrimaryObjective)
		{
			++PrimaryCount;
		}
	}

	if (AllTargets.Num() == 0)
	{
		UE_LOG(LogFPV, Warning,
			TEXT("No ADroneTarget actors in the level -- free flight only. Place targets to build a mission."));
	}
	else
	{
		UE_LOG(LogFPV, Log, TEXT("Sortie ready: %d targets (%d primary), %s drones."),
			AllTargets.Num(), PrimaryCount,
			DronesRemaining < 0 ? TEXT("unlimited") : *FString::FromInt(DronesRemaining));
	}
}

// ---------------------------------------------------------------------------------------------
// Blast
// ---------------------------------------------------------------------------------------------

void AFPVWarGameMode::ApplyBlast(const FVector& Origin, float Radius, float Damage, AActor* DamageCauser, AActor* IgnoreActor)
{
	if (Radius <= 0.f || Damage <= 0.f)
	{
		return;
	}

	const float RadiusSq = FMath::Square(Radius);

	// Snapshotted because a chain reaction can destroy targets while we iterate.
	TArray<TObjectPtr<ADroneTarget>> Snapshot = AllTargets;

	for (ADroneTarget* Target : Snapshot)
	{
		if (!Target || Target == IgnoreActor || Target->IsDestroyed())
		{
			continue;
		}

		const FVector TargetPoint = Target->GetAimPoint();
		const float DistanceSq = FVector::DistSquared(Origin, TargetPoint);

		// Measured to the body surface rather than its centre, so large structures are not
		// unfairly hard to damage just for being big.
		const float EffectiveDistance = FMath::Max(0.f, FMath::Sqrt(DistanceSq) - Target->GetApproximateRadius());
		if (EffectiveDistance > Radius)
		{
			continue;
		}

		// Linear falloff. Physically it should be steeper, but linear is far easier to read as
		// a player -- "closer is better" without a cliff edge.
		const float Falloff = 1.f - FMath::Clamp(EffectiveDistance / Radius, 0.f, 1.f);
		Target->ApplyBlastDamage(Damage * Falloff, Origin, DamageCauser);
	}

	// Shake the operator's camera. Felt well beyond the lethal radius on purpose -- explosions
	// you survive are most of what makes the ones you do not survive feel dangerous.
	if (AFPVDronePawn* Drone = Cast<AFPVDronePawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)))
	{
		const float Distance = FVector::Dist(Drone->GetActorLocation(), Origin);
		const float Trauma = FImpactShake::TraumaFromBlast(Distance, Radius);
		if (Trauma > 0.f)
		{
			Drone->AddImpactShake(Trauma);
		}
	}
}

void AFPVWarGameMode::RegisterTarget(ADroneTarget* Target)
{
	if (!Target)
	{
		return;
	}

	// AddUnique: BeginPlay ordering means a level-placed target can be swept up by the game
	// mode's own scan as well as registering itself.
	const int32 PreviousCount = AllTargets.Num();
	AllTargets.AddUnique(Target);

	if (AllTargets.Num() != PreviousCount)
	{
		// A target arriving after the mission was already cleared re-opens it.
		if (bMissionComplete && Target->bPrimaryObjective && !Target->IsDestroyed())
		{
			bMissionComplete = false;
		}
	}
}

void AFPVWarGameMode::NotifyTargetDestroyed(ADroneTarget* Target, AActor* /*Killer*/)
{
	if (!Target)
	{
		return;
	}

	Score += Target->ScoreValue;
	++TargetsDestroyed;

	if (bRecordingStrike)
	{
		FStrikeKill Kill;
		Kill.TargetName = Target->GetDisplayName();
		Kill.Score = Target->ScoreValue;
		Kill.bSecondary = bPrimaryBlastResolved;
		StrikeKills.Add(Kill);
		StrikeScore += Target->ScoreValue;
	}

	if (GetPrimaryTargetsRemaining() == 0 && !bMissionComplete)
	{
		bMissionComplete = true;
		UE_LOG(LogFPV, Log, TEXT("Mission complete. Score %d, %d targets destroyed."), Score, TargetsDestroyed);
	}
}

// ---------------------------------------------------------------------------------------------
// Drone supply
// ---------------------------------------------------------------------------------------------

void AFPVWarGameMode::NotifyDroneDetonated(AFPVDronePawn* Drone, const FVector& BlastLocation,
	const FVector& ApproachDirection, float BlastRadius, float BlastDamage)
{
	if (StrikeState != EStrikeState::Flying)
	{
		return;   // already mid-sequence; a second trigger in the same frame changes nothing
	}

	// Record everything this warhead is responsible for. Chain reactions resolve synchronously
	// inside ApplyBlast, so the whole causal chain lands inside this window.
	StrikeKills.Reset();
	StrikeScore = 0;
	bRecordingStrike = true;
	bPrimaryBlastResolved = false;

	ApplyBlast(BlastLocation, BlastRadius, BlastDamage, Drone);

	// Anything destroyed from here on is a knock-on effect rather than the warhead itself.
	bPrimaryBlastResolved = true;
	bRecordingStrike = false;

	if (DronesRemaining > 0)
	{
		--DronesRemaining;
	}

	// Brief slowdown at the moment of impact. Timed against real seconds, since dilated time
	// cannot be used to measure its own recovery.
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), ImpactTimeDilation);
	SlowMoEndRealTime = GetWorld()->GetRealTimeSeconds() + ImpactSlowMoDuration;
	bSlowMoActive = true;

	ActiveStrikeCamera = AStrikeCamera::Spawn(GetWorld(), BlastLocation, ApproachDirection, BlastRadius);

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (ActiveStrikeCamera)
		{
			// Short blend so the cut registers as a deliberate change of view rather than a
			// glitch, without making the player wait to see the blast.
			PC->SetViewTargetWithBlend(ActiveStrikeCamera, 0.25f);
		}
	}

	EnterStrikeState(EStrikeState::KillCam, KillCamDuration);

	UE_LOG(LogFPV, Log, TEXT("Strike: %d kill(s), %d points."), StrikeKills.Num(), StrikeScore);
}

void AFPVWarGameMode::EnterStrikeState(EStrikeState NewState, float Duration)
{
	StrikeState = NewState;
	PhaseEndTime = GetWorld()->GetTimeSeconds() + Duration;
}

bool AFPVWarGameMode::WantsToSkipStrikeSequence() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return false;
	}

	// Deliberately not SpaceBar, which the calibration wizard uses.
	return PC->WasInputKeyJustPressed(EKeys::R)
		|| PC->WasInputKeyJustPressed(EKeys::F)
		|| PC->WasInputKeyJustPressed(EKeys::Enter)
		|| PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
}

void AFPVWarGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bSlowMoActive && GetWorld()->GetRealTimeSeconds() >= SlowMoEndRealTime)
	{
		bSlowMoActive = false;
		UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f);
	}

	if (StrikeState == EStrikeState::Flying)
	{
		return;
	}

	const bool bPhaseOver = GetWorld()->GetTimeSeconds() >= PhaseEndTime;
	const bool bSkip = WantsToSkipStrikeSequence();

	switch (StrikeState)
	{
	case EStrikeState::KillCam:
		if (bPhaseOver || bSkip)
		{
			EnterStrikeState(EStrikeState::Report, ReportDuration);
		}
		break;

	case EStrikeState::Report:
		if (bPhaseOver || bSkip)
		{
			// Out of drones ends the run rather than looping it.
			if (DronesRemaining == 0)
			{
				StrikeState = EStrikeState::Flying;
				UE_LOG(LogFPV, Log, TEXT("Out of drones. Score %d."), Score);
			}
			else
			{
				EnterStrikeState(EStrikeState::Respawning, 0.35f);
				FinishStrikeSequence();
			}
		}
		break;

	case EStrikeState::Respawning:
		if (bPhaseOver)
		{
			StrikeState = EStrikeState::Flying;
		}
		break;

	default:
		break;
	}
}

void AFPVWarGameMode::FinishStrikeSequence()
{
	RespawnDrone();

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(Pawn, 0.3f);
		}
	}

	if (ActiveStrikeCamera)
	{
		// Outlive the blend, or the view snaps as the camera is torn out from under it.
		ActiveStrikeCamera->SetLifeSpan(0.6f);
		ActiveStrikeCamera = nullptr;
	}
}

void AFPVWarGameMode::RespawnDrone()
{
	// The pawn is reused rather than respawned: it keeps the possession, camera and input
	// bindings intact, which matters because input setup is built in C++ at runtime.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (AFPVDronePawn* Drone = Cast<AFPVDronePawn>(PC->GetPawn()))
		{
			Drone->ResetToStart();
			Drone->RearmWarhead();
		}
	}
}

float AFPVWarGameMode::GetRespawnCountdown() const
{
	// The strike sequence owns the wait now, and it shows its own progress.
	return 0.f;
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

int32 AFPVWarGameMode::GetPrimaryTargetsRemaining() const
{
	int32 Count = 0;
	for (const ADroneTarget* Target : AllTargets)
	{
		if (Target && Target->bPrimaryObjective && !Target->IsDestroyed())
		{
			++Count;
		}
	}
	return Count;
}

ADroneTarget* AFPVWarGameMode::FindNearestLiveTarget(const FVector& FromLocation) const
{
	ADroneTarget* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (ADroneTarget* Target : AllTargets)
	{
		if (!Target || Target->IsDestroyed())
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(FromLocation, Target->GetAimPoint());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Target;
		}
	}

	return Nearest;
}

void AFPVWarGameMode::GetLiveTargets(TArray<ADroneTarget*>& OutTargets) const
{
	OutTargets.Reset();
	for (ADroneTarget* Target : AllTargets)
	{
		if (Target && !Target->IsDestroyed())
		{
			OutTargets.Add(Target);
		}
	}
}
