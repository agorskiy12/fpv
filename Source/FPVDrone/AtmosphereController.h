#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AtmosphereController.generated.h"

class UPostProcessComponent;

/** Mood presets. Each one drives light, fog and colour grading together. */
UENUM(BlueprintType)
enum class EAtmosphereMood : uint8
{
	/** Flat grey overcast. The default exclusion-zone look. */
	Overcast,

	/** Dense low fog, short sight lines, oppressive. */
	HeavyFog,

	/** Low blue sun, long shadows, hard cold light. */
	ColdDawn,

	/** Sickly yellow-green haze. */
	Toxic,

	/** Clear and neutral, for checking geometry without the grade fighting you. */
	Neutral,

	/** Nothing preset -- use the exposed values as authored. */
	Custom
};

/**
 * Sets the mood of the whole level: sun, sky light, fog and colour grading.
 *
 * Drop one into a level and it reconfigures whatever lighting actors are already there rather
 * than spawning duplicates, so it composes with the Basic template instead of fighting it.
 *
 * This exists because atmosphere is most of what makes an abandoned place read as abandoned,
 * and unlike meshes it costs nothing to acquire. Grey boxes under flat grey overcast with fog
 * banks already look like somewhere; the same boxes under default daylight look like a test map.
 */
UCLASS()
class FPVDRONE_API AAtmosphereController : public AActor
{
	GENERATED_BODY()

public:
	AAtmosphereController();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Preset to apply. Set to Custom to drive the values below directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere")
	EAtmosphereMood Mood = EAtmosphereMood::Overcast;

	/** Re-apply whenever a property changes, so the viewport previews without pressing Play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere")
	bool bApplyInEditor = true;

	// --- Sun ----------------------------------------------------------------------------------

	/** Sun brightness in lux. Overcast daylight is far dimmer than people expect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Sun")
	float SunIntensity = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Sun")
	FLinearColor SunColor = FLinearColor(0.78f, 0.82f, 0.92f);

	/** Sun pitch. Shallow angles give the long raking shadows that read as early or late. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Sun")
	float SunPitchDegrees = -28.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Sun")
	float SunYawDegrees = 140.f;

	/** Widening the light source softens shadow edges, which is what overcast actually does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Sun")
	float SunSourceAngle = 3.5f;

	// --- Sky ----------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Sky")
	float SkyLightIntensity = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Sky")
	FLinearColor SkyLightColor = FLinearColor(0.62f, 0.66f, 0.74f);

	// --- Fog ----------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Fog")
	float FogDensity = 0.045f;

	/** Higher values keep fog low to the ground, which is what makes it read as fog and not haze. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Fog")
	float FogHeightFalloff = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Fog")
	FLinearColor FogColor = FLinearColor(0.42f, 0.45f, 0.48f);

	/** Keeps fog off the camera so the drone is not flying inside a grey smear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Fog")
	float FogStartDistance = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Fog")
	bool bVolumetricFog = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Fog")
	float VolumetricFogExtinction = 1.4f;

	// --- Grade --------------------------------------------------------------------------------

	/** Below 1 drains colour. Around 0.6 reads as bleak without going monochrome. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Grade")
	float Saturation = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Grade")
	float Contrast = 1.08f;

	/** Overall tint. Cool shifts read as cold and abandoned; warm reads as inhabited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Grade")
	FLinearColor ColorTint = FLinearColor(0.94f, 0.97f, 1.05f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Grade")
	float VignetteIntensity = 0.45f;

	/** A little grain hides the fact that there is not much surface detail yet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Grade")
	float FilmGrainIntensity = 0.32f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Grade")
	float BloomIntensity = 0.45f;

	/** Exposure compensation. Negative darkens, which suits overcast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere|Grade")
	float ExposureBias = -0.35f;

	/** Push the preset's values into the level. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Atmosphere")
	void ApplyAtmosphere();

protected:
	/** Unbound, so the grade covers the level without needing a volume around the play space. */
	UPROPERTY(VisibleAnywhere, Category = "Atmosphere")
	TObjectPtr<UPostProcessComponent> PostProcess;

private:
	/** Overwrite the exposed values with the chosen preset. */
	void LoadMoodPreset();

	void ApplyToSun();
	void ApplyToSkyLight();
	void ApplyToFog();
	void ApplyToPostProcess();
};
