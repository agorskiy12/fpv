#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FPVGameMode.generated.h"

class ARaceGate;

/**
 * Race director: finds the gates in the level, enforces the running order, and times laps.
 *
 * The clock starts when you cross gate 0 for the first time, so you get a flying start rather
 * than being timed while you lift off. Every later crossing of gate 0 closes a lap and opens
 * the next one. Gates hit out of order are ignored -- you have to go back and take the one
 * you missed.
 */
UCLASS()
class FPVDRONE_API AFPVGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFPVGameMode();

	virtual void BeginPlay() override;

	/** Called by ARaceGate when the drone flies through it. */
	void NotifyGatePassed(ARaceGate* Gate);

	UFUNCTION(BlueprintPure, Category = "Race")
	bool IsRaceStarted() const { return bRaceStarted; }

	/** Seconds elapsed on the current lap; 0 before the first gate. */
	UFUNCTION(BlueprintPure, Category = "Race")
	float GetCurrentLapTime() const;

	UFUNCTION(BlueprintPure, Category = "Race")
	float GetLastLapTime() const { return LastLapTime; }

	/** Best lap so far, or -1 if no lap has been completed. */
	UFUNCTION(BlueprintPure, Category = "Race")
	float GetBestLapTime() const { return BestLapTime; }

	UFUNCTION(BlueprintPure, Category = "Race")
	int32 GetCompletedLaps() const { return CompletedLaps; }

	UFUNCTION(BlueprintPure, Category = "Race")
	int32 GetTotalGates() const { return OrderedGates.Num(); }

	/** 1-based position of the gate you need next, for display. */
	UFUNCTION(BlueprintPure, Category = "Race")
	int32 GetNextGateNumber() const { return NextGateSlot + 1; }

	/** The gate you need next, so the HUD can point at it. Null if the course is empty. */
	UFUNCTION(BlueprintPure, Category = "Race")
	ARaceGate* GetNextGate() const;

	/** Formats seconds as m:ss.mmm. */
	UFUNCTION(BlueprintPure, Category = "Race")
	static FString FormatLapTime(float Seconds);

protected:
	/** Gates found in the level, sorted by GateIndex. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ARaceGate>> OrderedGates;

private:
	int32 NextGateSlot = 0;
	int32 CompletedLaps = 0;
	bool bRaceStarted = false;

	float LapStartTime = 0.f;
	float LastLapTime = -1.f;
	float BestLapTime = -1.f;
};
