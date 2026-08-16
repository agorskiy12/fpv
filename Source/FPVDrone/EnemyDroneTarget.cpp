#include "EnemyDroneTarget.h"
#include "FPVDrone.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

AEnemyDroneTarget::AEnemyDroneTarget()
{
	PrimaryActorTick.bCanEverTick = true;

	Kind = ETargetKind::Drone;
	MaxHealth = 25.f;          // a clip is enough; the difficulty is connecting at all
	ScoreValue = 400;
	BodySize = FVector(90.f, 90.f, 40.f);
	bSecondaryExplosion = false;

	// A box circuit, so a freshly placed UAV is already patrolling.
	RoutePoints = {
		FVector(0.f, 0.f, 0.f),
		FVector(3000.f, 0.f, 400.f),
		FVector(3000.f, 3000.f, 0.f),
		FVector(0.f, 3000.f, 400.f)
	};
}

void AEnemyDroneTarget::BeginPlay()
{
	Super::BeginPlay();

	WorldRoute.Reset();
	const FTransform SpawnTransform = GetActorTransform();
	for (const FVector& Point : RoutePoints)
	{
		WorldRoute.Add(SpawnTransform.TransformPosition(Point));
	}

	CurrentPoint = (WorldRoute.Num() > 1) ? 1 : 0;
	BobPhase = FMath::Fmod(GetActorLocation().X, 100.f) * 0.06f;   // desynchronise multiple UAVs
}

FVector AEnemyDroneTarget::ComputePatrolDirection(const FVector& Location) const
{
	if (!WorldRoute.IsValidIndex(CurrentPoint))
	{
		return FVector::ZeroVector;
	}

	const FVector ToPoint = WorldRoute[CurrentPoint] - Location;
	return ToPoint.GetSafeNormal();
}

FVector AEnemyDroneTarget::ComputeEvadeDirection(const FVector& Location, const FVector& ThreatLocation) const
{
	FVector Away = Location - ThreatLocation;
	Away.Normalize();

	// Straight-line running is easy to intercept, so break perpendicular and climb -- the
	// direction a rate-mode quad finds most awkward to follow.
	const FVector Perpendicular = FVector::CrossProduct(Away, FVector::UpVector).GetSafeNormal();
	const float Weave = FMath::Sin(BobPhase * 2.2f);

	FVector Direction = Away * 0.65f + Perpendicular * Weave * 0.55f + FVector::UpVector * 0.35f;
	return Direction.GetSafeNormal();
}

void AEnemyDroneTarget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsDestroyed() || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	BobPhase += DeltaSeconds * BobFrequency;

	const FVector Location = GetActorLocation();

	// Threat assessment
	bEvading = false;
	FVector ThreatLocation = FVector::ZeroVector;
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		ThreatLocation = Player->GetActorLocation();
		bEvading = FVector::DistSquared(ThreatLocation, Location) < FMath::Square(DetectionRadius);
	}

	FVector DesiredDirection;
	float DesiredSpeed;

	if (bEvading)
	{
		DesiredDirection = ComputeEvadeDirection(Location, ThreatLocation);
		DesiredSpeed = EvadeSpeed;
	}
	else
	{
		if (WorldRoute.Num() > 1 && FVector::DistSquared(WorldRoute[CurrentPoint], Location) < FMath::Square(WaypointTolerance))
		{
			CurrentPoint = (CurrentPoint + 1) % WorldRoute.Num();
		}
		DesiredDirection = ComputePatrolDirection(Location);
		DesiredSpeed = PatrolSpeed;
	}

	// Eased rather than snapped, so it banks into turns instead of teleporting between headings.
	const FVector DesiredVelocity = DesiredDirection * DesiredSpeed;
	Velocity = FMath::VInterpTo(Velocity, DesiredVelocity, DeltaSeconds, bEvading ? 3.5f : 1.8f);

	FVector NewLocation = Location + Velocity * DeltaSeconds;
	NewLocation.Z += FMath::Sin(BobPhase) * BobAmplitude * DeltaSeconds;

	SetActorLocation(NewLocation, false);

	// Face travel, with a little roll into the turn for readability.
	if (!Velocity.IsNearlyZero())
	{
		const FRotator Facing = Velocity.Rotation();
		const float BankDegrees = FMath::Clamp(FVector::DotProduct(
			Velocity.GetSafeNormal(), GetActorRightVector()) * -35.f, -35.f, 35.f);
		SetActorRotation(FRotator(Facing.Pitch * 0.35f, Facing.Yaw, BankDegrees));
	}
}

void AEnemyDroneTarget::OnDestroyed_Internal(AActor* Killer)
{
	SetActorTickEnabled(false);
	Velocity = FVector::ZeroVector;
	Super::OnDestroyed_Internal(Killer);
}
