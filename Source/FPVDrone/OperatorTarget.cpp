#include "OperatorTarget.h"
#include "FPVDrone.h"

AOperatorTarget::AOperatorTarget()
{
	PrimaryActorTick.bCanEverTick = true;

	// The objective of the entire match, so it is worth far more than anything else on the map.
	ScoreValue = 5000;
	MaxHealth = 30.f;
	SoldierRole = ESoldierRole::Objective;

	// Stands its ground. A single waypoint means hold position rather than patrol.
	PatrolPoints = { FVector::ZeroVector };

	// Never runs, even when a drone is overhead. An operator breaking cover would give the
	// position away far more reliably than any signal reading.
	AlertRadius = 0.f;
}

void AOperatorTarget::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogFPV, Log, TEXT("%s operator in position at %s, signal range %.0f m"),
		FPVFaction::GetDisplayName(Faction), *GetActorLocation().ToCompactString(),
		SignalRange / 100.f);
}

void AOperatorTarget::SetTransmitting(bool bTransmitting)
{
	if (bTransmitting)
	{
		TransmitLevel = 1.f;
	}
}

void AOperatorTarget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Decays rather than switching off, so a brief landing does not instantly erase the fix and
	// a long silence genuinely does.
	if (SignalDecayTime > KINDA_SMALL_NUMBER)
	{
		TransmitLevel = FMath::Max(0.f, TransmitLevel - DeltaSeconds / SignalDecayTime);
	}
	else
	{
		TransmitLevel = 0.f;
	}
}

float AOperatorTarget::GetSignalStrengthAt(const FVector& SampleLocation) const
{
	if (IsDestroyed() || TransmitLevel <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}

	const float Distance = FVector::Dist(SampleLocation, GetActorLocation());
	if (Distance >= SignalRange)
	{
		return 0.f;
	}

	const float Proximity = 1.f - (Distance / SignalRange);

	// Scaled by how recently they transmitted, so the reading fades as the fix goes stale.
	return Proximity * TransmitLevel;
}
