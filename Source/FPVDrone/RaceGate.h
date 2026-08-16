#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RaceGate.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * One gate on the race course.
 *
 * The frame is four scaled cubes built in the constructor, so a course can be laid out by
 * dragging these into a level -- no gate mesh to model or import first. Set GateIndex to
 * define the running order; the game mode sorts by it and only counts gates hit in sequence.
 */
UCLASS()
class FPVDRONE_API ARaceGate : public AActor
{
	GENERATED_BODY()

public:
	ARaceGate();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Running order on the course. Gate 0 is the start/finish line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race Gate")
	int32 GateIndex = 0;

	/** Width and height of the opening, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race Gate", meta = (ClampMin = "50.0"))
	float GateSize = 300.f;

	/** Thickness of the frame bars, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race Gate", meta = (ClampMin = "1.0"))
	float FrameThickness = 15.f;

	/** Centre of the opening in world space -- the point the HUD marker aims at. */
	UFUNCTION(BlueprintPure, Category = "Race Gate")
	FVector GetGateCenter() const { return GetActorLocation(); }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Race Gate")
	TObjectPtr<USceneComponent> GateRoot;

	/** Top, bottom, left, right. */
	UPROPERTY(VisibleAnywhere, Category = "Race Gate")
	TArray<TObjectPtr<UStaticMeshComponent>> FrameBars;

	/** Thin slab filling the opening; overlapping it is what scores the gate. */
	UPROPERTY(VisibleAnywhere, Category = "Race Gate")
	TObjectPtr<UBoxComponent> PassTrigger;

	UFUNCTION()
	void OnPassTriggerOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

private:
	void RebuildFrame();
};
