#include "HelicopterTarget.h"
#include "FPVDrone.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AHelicopterTarget::AHelicopterTarget()
{
	PrimaryActorTick.bCanEverTick = true;

	Kind = ETargetKind::Helicopter;
	MaxHealth = 130.f;         // airframe, not a hobby quad -- one clean hit should not be enough
	ScoreValue = 750;
	bPrimaryObjective = true;

	// Roughly a Mi-8: about 18 m of fuselage. Gameplay reads this for blast falloff and HUD
	// marker sizing regardless of what the mesh actually measures.
	BodySize = FVector(1800.f, 400.f, 500.f);

	bSecondaryExplosion = true;
	SecondaryBlastRadius = 1600.f;
	SecondaryBlastDamage = 120.f;

	// Auto-assign the Fab airframe so the class works without anyone wiring it up. If the asset
	// moves or is missing, BodyMesh stays null and the primitive placeholder takes over.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HeliMesh(
		TEXT("/Game/Fab/Russian_Transport_Heli/russian_transport_heli/StaticMeshes/russian_transport_heli.russian_transport_heli"));
	if (HeliMesh.Succeeded())
	{
		BodyMesh = HeliMesh.Object;
	}

	// A wide circuit -- it should be crossing the map, not orbiting one spot.
	RoutePoints = {
		FVector(0.f, 0.f, 0.f),
		FVector(18000.f, 4000.f, 800.f),
		FVector(22000.f, 20000.f, 0.f),
		FVector(4000.f, 18000.f, 900.f)
	};
}

void AHelicopterTarget::BeginPlay()
{
	Super::BeginPlay();

	WorldRoute.Reset();
	const FTransform SpawnTransform = GetActorTransform();
	for (const FVector& Point : RoutePoints)
	{
		WorldRoute.Add(SpawnTransform.TransformPosition(Point));
	}

	CurrentPoint = (WorldRoute.Num() > 1) ? 1 : 0;

	// Start pointed along the route so it does not swing wildly on the first frame.
	if (WorldRoute.Num() > 1)
	{
		const FVector Initial = (WorldRoute[CurrentPoint] - GetActorLocation()).GetSafeNormal();
		Velocity = Initial * CruiseSpeed;
	}
}

void AHelicopterTarget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsDestroyed() || DeltaSeconds <= KINDA_SMALL_NUMBER || WorldRoute.Num() == 0)
	{
		return;
	}

	BobPhase += DeltaSeconds * BobFrequency;
	RotorSpin += DeltaSeconds * 1800.f;

	const FVector Location = GetActorLocation();

	// Threat assessment
	bFleeing = false;
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		bFleeing = FVector::DistSquared(Player->GetActorLocation(), Location) < FMath::Square(DetectionRadius);
	}

	// Advance the circuit
	if (WorldRoute.Num() > 1 && FVector::DistSquared(WorldRoute[CurrentPoint], Location) < FMath::Square(WaypointTolerance))
	{
		CurrentPoint = (CurrentPoint + 1) % WorldRoute.Num();
	}

	FVector Desired = (WorldRoute[CurrentPoint] - Location).GetSafeNormal();

	// Fleeing means climbing, not dodging. Altitude is the only defence a transport has, and it
	// costs the operator battery to answer.
	if (bFleeing)
	{
		Desired.Z = FMath::Max(Desired.Z, 0.f) + (FleeClimbRate / FMath::Max(RunSpeed, 1.f));
		Desired = Desired.GetSafeNormal();
	}

	const float TargetSpeed = bFleeing ? RunSpeed : CruiseSpeed;

	// Heavy aircraft change direction slowly, so this interpolates far more gently than the
	// small UAVs do.
	Velocity = FMath::VInterpTo(Velocity, Desired * TargetSpeed, DeltaSeconds, bFleeing ? 1.2f : 0.7f);

	FVector NewLocation = Location + Velocity * DeltaSeconds;
	NewLocation.Z += FMath::Sin(BobPhase) * BobAmplitude * DeltaSeconds;
	SetActorLocation(NewLocation, false);

	if (Velocity.IsNearlyZero())
	{
		return;
	}

	// Bank proportional to how hard it is turning, eased so it rolls in and out rather than
	// snapping between angles.
	const FVector Forward = Velocity.GetSafeNormal();
	const float TurnRate = FVector::CrossProduct(Forward, Desired).Z;
	const float TargetBank = FMath::Clamp(-TurnRate * MaxBankDegrees * 2.f, -MaxBankDegrees, MaxBankDegrees);
	CurrentBank = FMath::FInterpTo(CurrentBank, TargetBank, DeltaSeconds, 1.5f);

	const FRotator Facing = Forward.Rotation();
	SetActorRotation(FRotator(CruisePitchDegrees + Facing.Pitch * 0.3f, Facing.Yaw, CurrentBank));

	// Spin the placeholder rotor disc. With a real airframe mesh there is nothing separate to
	// turn, so this is skipped.
	if (!IsUsingMeshOverride() && BodyParts.IsValidIndex(3) && BodyParts[3])
	{
		BodyParts[3]->SetRelativeRotation(FRotator(0.f, RotorSpin, 0.f));
	}
}

void AHelicopterTarget::OnDestroyed_Internal(AActor* Killer)
{
	SetActorTickEnabled(false);
	Velocity = FVector::ZeroVector;
	Super::OnDestroyed_Internal(Killer);
}
