#include "FPVWarGameMode.h"
#include "DroneTarget.h"
#include "FPVDrone.h"
#include "FPVDronePawn.h"
#include "FPVHUD.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

AFPVWarGameMode::AFPVWarGameMode()
{
	DefaultPawnClass = AFPVDronePawn::StaticClass();
	HUDClass = AFPVHUD::StaticClass();
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
}

void AFPVWarGameMode::NotifyTargetDestroyed(ADroneTarget* Target, AActor* /*Killer*/)
{
	if (!Target)
	{
		return;
	}

	Score += Target->ScoreValue;
	++TargetsDestroyed;

	if (GetPrimaryTargetsRemaining() == 0 && !bMissionComplete)
	{
		bMissionComplete = true;
		UE_LOG(LogFPV, Log, TEXT("Mission complete. Score %d, %d targets destroyed."), Score, TargetsDestroyed);
	}
}

// ---------------------------------------------------------------------------------------------
// Drone supply
// ---------------------------------------------------------------------------------------------

void AFPVWarGameMode::NotifyDroneExpended(AFPVDronePawn* /*Drone*/)
{
	if (bRespawnPending)
	{
		return;   // one detonation can touch several triggers in a frame
	}

	if (DronesRemaining > 0)
	{
		--DronesRemaining;
	}

	if (DronesRemaining == 0)
	{
		UE_LOG(LogFPV, Log, TEXT("Out of drones. Score %d."), Score);
		return;
	}

	bRespawnPending = true;
	RespawnAtTime = GetWorld()->GetTimeSeconds() + RespawnDelay;

	GetWorld()->GetTimerManager().SetTimer(
		RespawnTimer, this, &AFPVWarGameMode::RespawnDrone, RespawnDelay, false);
}

void AFPVWarGameMode::RespawnDrone()
{
	bRespawnPending = false;

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
	if (!bRespawnPending)
	{
		return 0.f;
	}
	return FMath::Max(0.f, RespawnAtTime - GetWorld()->GetTimeSeconds());
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
