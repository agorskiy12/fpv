#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExplosionEffect.generated.h"

class UPointLightComponent;
class URadialForceComponent;

/**
 * Short-lived detonation effect.
 *
 * Built entirely from code -- a light flash, a radial impulse on nearby physics bodies, and an
 * expanding debug sphere -- because the project deliberately carries no imported content. It
 * reads well enough to fly against; swap it for a Niagara system once there is an art pass.
 */
UCLASS()
class FPVDRONE_API AExplosionEffect : public AActor
{
	GENERATED_BODY()

public:
	AExplosionEffect();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Spawn one at a location. Radius drives the visual size and the physics impulse. */
	static AExplosionEffect* Spawn(UWorld* World, const FVector& Location, float Radius, float Intensity = 1.f);

	/** Blast radius in centimetres. */
	UPROPERTY(EditAnywhere, Category = "Explosion")
	float Radius = 900.f;

	/** How long the flash lasts. */
	UPROPERTY(EditAnywhere, Category = "Explosion")
	float Duration = 0.55f;

	/** Peak brightness of the flash. */
	UPROPERTY(EditAnywhere, Category = "Explosion")
	float PeakLightIntensity = 240000.f;

	// ---------------------------------------------------------------------------------------
	// Assets
	//
	// Set these on the class defaults (or via UFPVEffectSettings) and the code-drawn fallback
	// switches off automatically. Nothing else needs changing to move to real VFX.
	// ---------------------------------------------------------------------------------------

	/** Niagara system to play. When set, the debug-sphere fireball is suppressed. */
	UPROPERTY(EditAnywhere, Category = "Explosion|Assets")
	TObjectPtr<class UNiagaraSystem> ExplosionFX;

	/** Scales the Niagara system to the blast radius, assuming the system authors at 1 m. */
	UPROPERTY(EditAnywhere, Category = "Explosion|Assets")
	bool bScaleFXToRadius = true;

	UPROPERTY(EditAnywhere, Category = "Explosion|Assets")
	TObjectPtr<USoundBase> ExplosionSound;

	/** Keep the light flash even when a Niagara system provides its own. */
	UPROPERTY(EditAnywhere, Category = "Explosion|Assets")
	bool bAlwaysUseLightFlash = true;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Explosion")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Explosion")
	TObjectPtr<UPointLightComponent> FlashLight;

	UPROPERTY(VisibleAnywhere, Category = "Explosion")
	TObjectPtr<URadialForceComponent> BlastForce;

private:
	float Elapsed = 0.f;
};
