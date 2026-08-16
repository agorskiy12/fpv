#pragma once

#include "CoreMinimal.h"

/**
 * Trauma-based camera shake.
 *
 * Deliberately not built on UCameraShakeBase. That system wants a shake *pattern* asset, and
 * this project owns both of its cameras outright -- the FPV camera bolted to the airframe and
 * the third-person strike camera -- so driving the offset directly is simpler, has no asset
 * dependency, and gives exact control over the feel.
 *
 * Trauma decays linearly but is applied squared. That is the standard trick and it matters:
 * shake that decays linearly reads as a mechanical fade, whereas squared falls off fast at
 * first and then lingers faintly, which is how a real knock actually feels.
 */
struct FImpactShake
{
	/** Current trauma, 0..1. */
	float Trauma = 0.f;

	/** Trauma lost per second. */
	float DecayRate = 1.7f;

	/** Advances the noise. */
	float Time = 0.f;

	/** Add trauma, saturating at 1 so a chain reaction cannot produce an unusable camera. */
	void Add(float Amount)
	{
		Trauma = FMath::Clamp(Trauma + Amount, 0.f, 1.f);
	}

	void Update(float DeltaSeconds)
	{
		Time += DeltaSeconds;
		Trauma = FMath::Max(0.f, Trauma - DecayRate * DeltaSeconds);
	}

	bool IsActive() const { return Trauma > KINDA_SMALL_NUMBER; }

	/** Squared trauma, which is what everything below is scaled by. */
	float GetShakeAmount() const { return Trauma * Trauma; }

	/**
	 * Angular offset in degrees.
	 * Three different frequencies per axis, so it never settles into a visible rhythm.
	 */
	FRotator GetRotationOffset(float MaxAngleDegrees) const
	{
		const float Amount = GetShakeAmount() * MaxAngleDegrees;
		return FRotator(
			Amount * FMath::Sin(Time * 47.3f),
			Amount * FMath::Sin(Time * 38.9f + 1.7f),
			Amount * FMath::Sin(Time * 55.1f + 3.1f));
	}

	FVector GetLocationOffset(float MaxOffset) const
	{
		const float Amount = GetShakeAmount() * MaxOffset;
		return FVector(
			Amount * FMath::Sin(Time * 41.7f + 0.6f),
			Amount * FMath::Sin(Time * 52.3f + 2.2f),
			Amount * FMath::Sin(Time * 44.9f + 4.4f));
	}

	/**
	 * Trauma for a blast, falling off with distance.
	 * Returns 0 outside the felt radius, which is several times the lethal radius -- you feel
	 * explosions you survive, and that is most of what sells them.
	 */
	static float TraumaFromBlast(float Distance, float BlastRadius, float Strength = 1.f)
	{
		const float FeltRadius = FMath::Max(BlastRadius * 4.f, 1.f);
		if (Distance >= FeltRadius)
		{
			return 0.f;
		}
		const float Falloff = 1.f - (Distance / FeltRadius);
		return FMath::Clamp(Falloff * Falloff * Strength, 0.f, 1.f);
	}
};
