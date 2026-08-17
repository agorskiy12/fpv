#pragma once

#include "CoreMinimal.h"
#include "SoldierTarget.h"
#include "OperatorTarget.generated.h"

/**
 * A hidden FPV operator — the primary objective, and the thing hunting you back.
 *
 * Built on the soldier so it inherits the body, the animation and the ragdoll death for free.
 * It does not patrol: it sits where it was placed, in a building, on a roof, inside a parked
 * vehicle. Nothing about its appearance advertises what it is.
 *
 * It is found by one route only. While its drone is airborne it transmits, and that signal is
 * detectable at range. Flying is therefore what exposes an operator, which is the central
 * bargain of the whole design: you cannot accomplish anything without becoming findable.
 */
UCLASS()
class FPVDRONE_API AOperatorTarget : public ASoldierTarget
{
	GENERATED_BODY()

public:
	AOperatorTarget();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Distance at which the signal becomes undetectable, centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operator|Signal")
	float SignalRange = 45000.f;

	/**
	 * How long the fix takes to fade once transmission stops.
	 *
	 * Going quiet is the only counter available while operators cannot move, and it only ever
	 * buys time -- a decayed fix still points at the same place, because the operator is still
	 * standing in it. It becomes a real escape once relocating is possible.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operator|Signal")
	float SignalDecayTime = 4.f;

	/** True while this operator's drone is airborne and drawing power. */
	UFUNCTION(BlueprintPure, Category = "Operator|Signal")
	bool IsTransmitting() const { return TransmitLevel > KINDA_SMALL_NUMBER; }

	/**
	 * Signal strength sampled at a point, 0 to 1.
	 *
	 * Falls off linearly rather than by inverse square. Inverse square is realistic and useless:
	 * the gradient goes flat past a short distance, so there is nothing to follow from far out.
	 * Linear keeps a readable gradient at every range, which is what makes flying search legs
	 * work as a mechanic.
	 */
	UFUNCTION(BlueprintPure, Category = "Operator|Signal")
	float GetSignalStrengthAt(const FVector& SampleLocation) const;

	/** Called by the game mode each frame with whether this side currently has a drone up. */
	void SetTransmitting(bool bTransmitting);

	/** Orange, so an operator is distinguishable from the infantry around it while testing. */
	virtual FLinearColor GetPlaceholderColour() const override { return FLinearColor(1.f, 0.35f, 0.f); }

protected:
	/** Rises while transmitting, decays when quiet. Smoothed so the tone never snaps. */
	float TransmitLevel = 0.f;
};
