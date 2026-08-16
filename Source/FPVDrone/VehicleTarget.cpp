#include "VehicleTarget.h"
#include "FPVDrone.h"
#include "FPVDronePawn.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

AVehicleTarget::AVehicleTarget()
{
	PrimaryActorTick.bCanEverTick = true;

	Kind = ETargetKind::Vehicle;
	MaxHealth = 60.f;          // thin skinned; a solid hit kills it
	ScoreValue = 250;
	BodySize = FVector(480.f, 200.f, 160.f);
	bSecondaryExplosion = true;
	SecondaryBlastRadius = 700.f;
	SecondaryBlastDamage = 70.f;

	// A short there-and-back so a freshly placed vehicle already moves.
	RoutePoints = { FVector(0.f, 0.f, 0.f), FVector(4000.f, 0.f, 0.f) };
}

void AVehicleTarget::BeginPlay()
{
	Super::BeginPlay();

	// Bake the route into world space once, so later movement does not compound drift.
	WorldRoute.Reset();
	const FTransform SpawnTransform = GetActorTransform();
	for (const FVector& Point : RoutePoints)
	{
		WorldRoute.Add(SpawnTransform.TransformPosition(Point));
	}

	CurrentPoint = (WorldRoute.Num() > 1) ? 1 : 0;
}

FVector AVehicleTarget::GetCurrentTargetPoint() const
{
	return WorldRoute.IsValidIndex(CurrentPoint) ? WorldRoute[CurrentPoint] : GetActorLocation();
}

void AVehicleTarget::AdvanceRoutePoint()
{
	if (WorldRoute.Num() < 2)
	{
		return;
	}

	++CurrentPoint;
	if (CurrentPoint >= WorldRoute.Num())
	{
		if (bLoopRoute)
		{
			CurrentPoint = 0;
		}
		else
		{
			CurrentPoint = WorldRoute.Num() - 1;
			bRouteFinished = true;
		}
	}
}

bool AVehicleTarget::IsBeingHunted(float& OutDistance) const
{
	OutDistance = TNumericLimits<float>::Max();

	if (!bFleeWhenHunted)
	{
		return false;
	}

	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player)
	{
		return false;
	}

	OutDistance = FVector::Dist(Player->GetActorLocation(), GetActorLocation());
	return OutDistance < FleeDetectionRadius;
}

void AVehicleTarget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsDestroyed() || bRouteFinished || WorldRoute.Num() < 2 || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector Location = GetActorLocation();
	FVector ToPoint = GetCurrentTargetPoint() - Location;
	ToPoint.Z = 0.f;   // vehicles stay on the ground plane

	if (ToPoint.SizeSquared() < FMath::Square(WaypointTolerance))
	{
		AdvanceRoutePoint();
		ToPoint = GetCurrentTargetPoint() - Location;
		ToPoint.Z = 0.f;
	}

	if (ToPoint.IsNearlyZero())
	{
		return;
	}

	// Yaw toward the next point, rate limited so corners are not instantaneous.
	const FRotator CurrentRotation = GetActorRotation();
	const FRotator DesiredRotation = ToPoint.Rotation();
	const FRotator NewRotation = FMath::RInterpConstantTo(
		CurrentRotation, FRotator(0.f, DesiredRotation.Yaw, 0.f), DeltaSeconds, TurnRateDegrees);
	SetActorRotation(NewRotation);

	float CurrentSpeed = Speed;
	float DistanceToPlayer;
	if (IsBeingHunted(DistanceToPlayer))
	{
		CurrentSpeed *= FleeSpeedMultiplier;
	}

	// Drive along the facing direction rather than straight at the point, so the turn radius
	// is visible instead of the vehicle crabbing sideways.
	SetActorLocation(Location + GetActorForwardVector() * CurrentSpeed * DeltaSeconds, true);
}

void AVehicleTarget::OnDestroyed_Internal(AActor* Killer)
{
	bRouteFinished = true;
	SetActorTickEnabled(false);
	Super::OnDestroyed_Internal(Killer);
}
