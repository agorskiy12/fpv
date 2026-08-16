#include "AtmosphereController.h"
#include "FPVDrone.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace
{
	FAutoConsoleCommandWithWorld CmdApplyAtmosphere(
		TEXT("fpv.ApplyAtmosphere"),
		TEXT("Re-apply every AAtmosphereController in the level. Use after tweaking values at runtime."),
		FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
		{
			if (!World)
			{
				return;
			}
			int32 Count = 0;
			for (TActorIterator<AAtmosphereController> It(World); It; ++It)
			{
				It->ApplyAtmosphere();
				++Count;
			}
			UE_LOG(LogFPV, Log, TEXT("Applied %d atmosphere controller(s)."), Count);
		}));
}

AAtmosphereController::AAtmosphereController()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(Root);
	PostProcess->bUnbound = true;   // applies everywhere, no volume to fly out of
	PostProcess->Priority = 1.f;
}

void AAtmosphereController::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (bApplyInEditor)
	{
		ApplyAtmosphere();
	}
}

void AAtmosphereController::BeginPlay()
{
	Super::BeginPlay();
	ApplyAtmosphere();
}

void AAtmosphereController::ApplyAtmosphere()
{
	LoadMoodPreset();

	ApplyToSun();
	ApplyToSkyLight();
	ApplyToFog();
	ApplyToPostProcess();
}

// ---------------------------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------------------------

void AAtmosphereController::LoadMoodPreset()
{
	switch (Mood)
	{
	case EAtmosphereMood::Overcast:
		// Flat, dim, and almost shadowless. The sun is a bright patch of cloud, not a disc.
		SunIntensity = 1.8f;
		SunColor = FLinearColor(0.78f, 0.82f, 0.92f);
		SunPitchDegrees = -50.f;
		SunSourceAngle = 6.f;
		SkyLightIntensity = 1.2f;
		SkyLightColor = FLinearColor(0.62f, 0.66f, 0.74f);
		FogDensity = 0.045f;
		FogHeightFalloff = 0.12f;
		FogColor = FLinearColor(0.42f, 0.45f, 0.48f);
		VolumetricFogExtinction = 1.4f;
		Saturation = 0.62f;
		Contrast = 1.08f;
		ColorTint = FLinearColor(0.94f, 0.97f, 1.05f);
		VignetteIntensity = 0.45f;
		ExposureBias = -0.35f;
		break;

	case EAtmosphereMood::HeavyFog:
		// Sight lines short enough that buildings arrive rather than approach.
		SunIntensity = 0.9f;
		SunColor = FLinearColor(0.8f, 0.83f, 0.9f);
		SunPitchDegrees = -60.f;
		SunSourceAngle = 8.f;
		SkyLightIntensity = 1.0f;
		SkyLightColor = FLinearColor(0.58f, 0.61f, 0.66f);
		FogDensity = 0.16f;
		FogHeightFalloff = 0.06f;
		FogColor = FLinearColor(0.5f, 0.52f, 0.55f);
		VolumetricFogExtinction = 3.2f;
		Saturation = 0.5f;
		Contrast = 1.02f;
		ColorTint = FLinearColor(0.95f, 0.98f, 1.03f);
		VignetteIntensity = 0.55f;
		ExposureBias = -0.15f;
		break;

	case EAtmosphereMood::ColdDawn:
		// Low hard sun. The one preset with real shadows, so geometry reads properly.
		SunIntensity = 4.5f;
		SunColor = FLinearColor(1.0f, 0.86f, 0.72f);
		SunPitchDegrees = -8.f;
		SunSourceAngle = 1.2f;
		SkyLightIntensity = 0.8f;
		SkyLightColor = FLinearColor(0.45f, 0.55f, 0.78f);
		FogDensity = 0.035f;
		FogHeightFalloff = 0.2f;
		FogColor = FLinearColor(0.48f, 0.54f, 0.66f);
		VolumetricFogExtinction = 1.8f;
		Saturation = 0.78f;
		Contrast = 1.15f;
		ColorTint = FLinearColor(0.92f, 0.96f, 1.1f);
		VignetteIntensity = 0.4f;
		ExposureBias = -0.2f;
		break;

	case EAtmosphereMood::Toxic:
		SunIntensity = 2.4f;
		SunColor = FLinearColor(0.95f, 0.98f, 0.7f);
		SunPitchDegrees = -35.f;
		SunSourceAngle = 5.f;
		SkyLightIntensity = 1.0f;
		SkyLightColor = FLinearColor(0.6f, 0.65f, 0.45f);
		FogDensity = 0.09f;
		FogHeightFalloff = 0.1f;
		FogColor = FLinearColor(0.46f, 0.5f, 0.33f);
		VolumetricFogExtinction = 2.2f;
		Saturation = 0.7f;
		Contrast = 1.12f;
		ColorTint = FLinearColor(1.02f, 1.04f, 0.86f);
		VignetteIntensity = 0.5f;
		ExposureBias = -0.25f;
		break;

	case EAtmosphereMood::Neutral:
		// Deliberately plain, for judging geometry without the grade flattering it.
		SunIntensity = 6.f;
		SunColor = FLinearColor::White;
		SunPitchDegrees = -45.f;
		SunSourceAngle = 0.5357f;
		SkyLightIntensity = 1.f;
		SkyLightColor = FLinearColor::White;
		FogDensity = 0.005f;
		FogHeightFalloff = 0.2f;
		FogColor = FLinearColor(0.5f, 0.55f, 0.6f);
		VolumetricFogExtinction = 1.f;
		Saturation = 1.f;
		Contrast = 1.f;
		ColorTint = FLinearColor::White;
		VignetteIntensity = 0.f;
		FilmGrainIntensity = 0.f;
		ExposureBias = 0.f;
		break;

	case EAtmosphereMood::Custom:
	default:
		break;   // leave the authored values alone
	}
}

// ---------------------------------------------------------------------------------------------
// Application
//
// Existing level actors are reconfigured rather than replaced, so this composes with whatever
// the level template already put down instead of doubling up on lights.
// ---------------------------------------------------------------------------------------------

void AAtmosphereController::ApplyToSun()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		ADirectionalLight* Sun = *It;
		if (!Sun)
		{
			continue;
		}

		if (UDirectionalLightComponent* Component = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Component->SetIntensity(SunIntensity);
			Component->SetLightColor(SunColor);
			Component->LightSourceAngle = SunSourceAngle;
			Component->MarkRenderStateDirty();
		}

		Sun->SetActorRotation(FRotator(SunPitchDegrees, SunYawDegrees, 0.f));
	}
}

void AAtmosphereController::ApplyToSkyLight()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		if (USkyLightComponent* Component = (*It)->GetLightComponent())
		{
			Component->SetIntensity(SkyLightIntensity);
			Component->SetLightColor(SkyLightColor);
			Component->MarkRenderStateDirty();
		}
	}
}

void AAtmosphereController::ApplyToFog()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		UExponentialHeightFogComponent* Component = (*It)->GetComponent();
		if (!Component)
		{
			continue;
		}

		Component->SetFogDensity(FogDensity);
		Component->SetFogHeightFalloff(FogHeightFalloff);
		Component->SetFogInscatteringColor(FogColor);
		Component->SetStartDistance(FogStartDistance);
		Component->SetVolumetricFog(bVolumetricFog);
		Component->SetVolumetricFogExtinctionScale(VolumetricFogExtinction);
		Component->MarkRenderStateDirty();
	}
}

void AAtmosphereController::ApplyToPostProcess()
{
	if (!PostProcess)
	{
		return;
	}

	FPostProcessSettings& Settings = PostProcess->Settings;

	// Saturation and contrast are stored as per-channel plus a luminance term, hence the W.
	Settings.bOverride_ColorSaturation = true;
	Settings.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.f);

	Settings.bOverride_ColorContrast = true;
	Settings.ColorContrast = FVector4(Contrast, Contrast, Contrast, 1.f);

	Settings.bOverride_ColorGain = true;
	Settings.ColorGain = FVector4(ColorTint.R, ColorTint.G, ColorTint.B, 1.f);

	Settings.bOverride_VignetteIntensity = true;
	Settings.VignetteIntensity = VignetteIntensity;

	Settings.bOverride_FilmGrainIntensity = true;
	Settings.FilmGrainIntensity = FilmGrainIntensity;

	Settings.bOverride_BloomIntensity = true;
	Settings.BloomIntensity = BloomIntensity;

	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = ExposureBias;

	// Auto exposure is disabled project-wide, so the bias above is a straight offset rather
	// than something the eye adaptation will quietly undo.
	Settings.bOverride_AutoExposureMinBrightness = true;
	Settings.AutoExposureMinBrightness = 1.f;
	Settings.bOverride_AutoExposureMaxBrightness = true;
	Settings.AutoExposureMaxBrightness = 1.f;
}
