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

	// The best kill in the game: it keeps its momentum, tumbles out of the sky trailing smoke,
	// and goes up again when it lands.
	bFallOnDestruction = true;
	DebrisCount = 34;
	DebrisSpeed = 1300.f;
	DebrisChunkSize = 110.f;
	WreckImpactBlastRadius = 2000.f;
	WreckImpactBlastDamage = 140.f;

	// Auto-assign the Fab airframe so the class works without anyone wiring it up. If the asset
	// moves or is missing, BodyMesh stays null and the primitive placeholder takes over.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HeliMesh(
		TEXT("/Game/Fab/Russian_Transport_Heli/russian_transport_heli/StaticMeshes/russian_transport_heli.russian_transport_heli"));
	if (HeliMesh.Succeeded())
	{
		BodyMesh = HeliMesh.Object;
	}

	// Low and slow. A transport working an airfield loiters at walking-pace groundspeed a few
	// tens of metres up -- and at that height it is genuinely reachable, which makes it a target
	// rather than a distant decoration. It is also the one thing in the scene big enough to read
	// clearly at low altitude.
	CruiseSpeed = 550.f;
	RunSpeed = 1100.f;
	OrbitRadius = 11000.f;
	OrbitAltitude = 2200.f;
	MaxBankDegrees = 12.f;      // gentle, at this speed it is barely leaning
	CruisePitchDegrees = -3.f;
	FleeClimbRate = 160.f;      // still climbs when hunted, but slowly enough to be caught
	BobAmplitude = 55.f;        // more visible hover wobble at low speed
	BobFrequency = 0.7f;

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

	// The circle is centred on wherever the actor was placed, so dragging it in the editor moves
	// the whole orbit rather than just the starting point.
	OrbitCentre = GetActorLocation() + OrbitCentreOffset;
	OrbitAngle = 0.f;

	CurrentOrbitRadius = OrbitRadius;
	CurrentSpeed = CruiseSpeed;
	CurrentAltitude = OrbitAltitude;

	UE_LOG(LogFPV, Log, TEXT("Helicopter: %s, centre %s, radius %.0f m, altitude %.0f m"),
		bOrbit ? TEXT("orbiting") : TEXT("on route"),
		*OrbitCentre.ToCompactString(), OrbitRadius / 100.f, OrbitAltitude / 100.f);
}

void AHelicopterTarget::TickOrbit(float DeltaSeconds)
{
	// Everything driven by bFleeing is eased. Switching any of it directly moves the aircraft
	// a large distance in one frame, which reads as a glitch rather than as evasion.
	const float TargetRadius = FMath::Max(OrbitRadius, 100.f) * (bFleeing ? 1.15f : 1.f);
	CurrentOrbitRadius = FMath::FInterpTo(CurrentOrbitRadius, TargetRadius, DeltaSeconds, 0.8f);

	const float TargetSpeed = bFleeing ? RunSpeed : CruiseSpeed;
	CurrentSpeed = FMath::FInterpTo(CurrentSpeed, TargetSpeed, DeltaSeconds, 1.2f);

	// Climbs while hunted and settles back afterwards, rather than permanently overwriting the
	// configured altitude.
	if (bFleeing)
	{
		CurrentAltitude += FleeClimbRate * DeltaSeconds;
	}
	else
	{
		CurrentAltitude = FMath::FInterpTo(CurrentAltitude, OrbitAltitude, DeltaSeconds, 0.35f);
	}

	// Constant ground speed converted to angular rate, so changing the radius changes how long
	// a lap takes rather than how fast the aircraft appears to fly.
	const float AngularRate = (CurrentSpeed / FMath::Max(CurrentOrbitRadius, 100.f))
		* (bOrbitClockwise ? 1.f : -1.f);

	OrbitAngle += AngularRate * DeltaSeconds;

	const FVector Position = OrbitCentre + FVector(
		FMath::Cos(OrbitAngle) * CurrentOrbitRadius,
		FMath::Sin(OrbitAngle) * CurrentOrbitRadius,
		CurrentAltitude + FMath::Sin(BobPhase) * BobAmplitude);

	const FVector Previous = GetActorLocation();
	SetActorLocation(Position, false);
	Velocity = (Position - Previous) / FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);

	// Tangent to the circle, with a fixed bank into it -- a steady turn holds a steady angle.
	const FVector Tangent = FVector(
		-FMath::Sin(OrbitAngle) * (bOrbitClockwise ? 1.f : -1.f),
		 FMath::Cos(OrbitAngle) * (bOrbitClockwise ? 1.f : -1.f),
		 0.f).GetSafeNormal();

	const float TargetBank = MaxBankDegrees * (bOrbitClockwise ? 1.f : -1.f);
	CurrentBank = FMath::FInterpTo(CurrentBank, TargetBank, DeltaSeconds, 1.5f);

	SetActorRotation(FRotator(CruisePitchDegrees, Tangent.Rotation().Yaw, CurrentBank));
}

void AHelicopterTarget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsDestroyed() || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	BobPhase += DeltaSeconds * BobFrequency;
	RotorSpin += DeltaSeconds * 1800.f;

	// Threat assessment with hysteresis: it starts running at DetectionRadius but does not stop
	// until well outside it. Without the gap, hovering near the boundary flips the state every
	// frame and the aircraft judders in place.
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		const float DistanceSq = FVector::DistSquared(Player->GetActorLocation(), GetActorLocation());
		const float EnterSq = FMath::Square(DetectionRadius);
		const float ExitSq = FMath::Square(DetectionRadius * 1.35f);

		if (!bFleeing && DistanceSq < EnterSq)
		{
			bFleeing = true;
		}
		else if (bFleeing && DistanceSq > ExitSq)
		{
			bFleeing = false;
		}
	}
	else
	{
		bFleeing = false;
	}

	if (bOrbit)
	{
		TickOrbit(DeltaSeconds);
	}
	else
	{
		TickRoute(DeltaSeconds);
	}

	// Spin the placeholder rotor disc. With a real airframe mesh there is nothing separate to
	// turn, so this is skipped.
	if (!IsUsingMeshOverride() && BodyParts.IsValidIndex(3) && BodyParts[3])
	{
		BodyParts[3]->SetRelativeRotation(FRotator(0.f, RotorSpin, 0.f));
	}
}

void AHelicopterTarget::TickRoute(float DeltaSeconds)
{
	if (WorldRoute.Num() == 0)
	{
		return;
	}

	const FVector Location = GetActorLocation();

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
}

void AHelicopterTarget::OnDestroyed_Internal(AActor* Killer)
{
	SetActorTickEnabled(false);
	Velocity = FVector::ZeroVector;
	Super::OnDestroyed_Internal(Killer);
}
