#include "FPVGameMode.h"
#include "FPVDrone.h"
#include "FPVDronePawn.h"
#include "FPVHUD.h"
#include "RaceGate.h"

#include "Engine/World.h"
#include "EngineUtils.h"

AFPVGameMode::AFPVGameMode()
{
	DefaultPawnClass = AFPVDronePawn::StaticClass();
	HUDClass = AFPVHUD::StaticClass();
}

void AFPVGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Sorted as raw pointers: TArray::Sort's dereferencing predicate is well defined for T*
	// on every UE5 version, which is not reliably true for TObjectPtr.
	TArray<ARaceGate*> Found;
	for (TActorIterator<ARaceGate> It(GetWorld()); It; ++It)
	{
		Found.Add(*It);
	}

	Found.Sort([](const ARaceGate& A, const ARaceGate& B)
	{
		return A.GateIndex < B.GateIndex;
	});

	OrderedGates.Reset(Found.Num());
	for (ARaceGate* Gate : Found)
	{
		OrderedGates.Add(Gate);
	}

	if (OrderedGates.Num() == 0)
	{
		UE_LOG(LogFPV, Warning,
			TEXT("No ARaceGate actors in the level -- free flight only. Drag some in and set their GateIndex to build a course."));
	}
	else
	{
		UE_LOG(LogFPV, Log, TEXT("Race course ready: %d gates."), OrderedGates.Num());
	}
}

void AFPVGameMode::NotifyGatePassed(ARaceGate* Gate)
{
	if (!Gate || OrderedGates.Num() == 0 || !OrderedGates.IsValidIndex(NextGateSlot))
	{
		return;
	}

	// Out of order -- ignore it. You have to go back for the one you skipped.
	if (OrderedGates[NextGateSlot] != Gate)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	if (NextGateSlot == 0)
	{
		if (bRaceStarted)
		{
			LastLapTime = Now - LapStartTime;
			if (BestLapTime < 0.f || LastLapTime < BestLapTime)
			{
				BestLapTime = LastLapTime;
			}
			++CompletedLaps;
			UE_LOG(LogFPV, Log, TEXT("Lap %d: %s"), CompletedLaps, *FormatLapTime(LastLapTime));
		}

		bRaceStarted = true;
		LapStartTime = Now;
	}

	NextGateSlot = (NextGateSlot + 1) % OrderedGates.Num();
}

float AFPVGameMode::GetCurrentLapTime() const
{
	if (!bRaceStarted)
	{
		return 0.f;
	}
	return GetWorld()->GetTimeSeconds() - LapStartTime;
}

ARaceGate* AFPVGameMode::GetNextGate() const
{
	return OrderedGates.IsValidIndex(NextGateSlot) ? OrderedGates[NextGateSlot] : nullptr;
}

FString AFPVGameMode::FormatLapTime(float Seconds)
{
	if (Seconds < 0.f)
	{
		return TEXT("--:--.---");
	}

	const int32 Minutes = FMath::FloorToInt(Seconds / 60.f);
	const float Remainder = Seconds - Minutes * 60.f;
	const int32 WholeSeconds = FMath::FloorToInt(Remainder);
	const int32 Milliseconds = FMath::FloorToInt((Remainder - WholeSeconds) * 1000.f);

	return FString::Printf(TEXT("%d:%02d.%03d"), Minutes, WholeSeconds, Milliseconds);
}
