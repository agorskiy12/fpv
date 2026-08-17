#include "ExplosionEffect.h"
#include "FPVDrone.h"

#include "Components/PointLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicsEngine/RadialForceComponent.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Overrides the class default at runtime, so explosion size can be judged by eye. */
	float GExplosionScaleOverride = -1.f;

	FAutoConsoleCommand CmdExplosionScale(
		TEXT("fpv.ExplosionScale"),
		TEXT("Multiplier on explosion visual size. No argument reports the current value."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			if (Args.Num() > 0)
			{
				GExplosionScaleOverride = FMath::Max(static_cast<float>(FCString::Atof(*Args[0])), 0.01f);
			}
			UE_LOG(LogFPV, Log, TEXT("Explosion scale %s"),
				GExplosionScaleOverride > 0.f
					? *FString::Printf(TEXT("%.2f"), GExplosionScaleOverride)
					: TEXT("(class default)"));
		}));
}

AExplosionEffect::AExplosionEffect()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	FlashLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashLight"));
	FlashLight->SetupAttachment(Root);
	FlashLight->SetLightColor(FLinearColor(1.f, 0.6f, 0.22f));
	FlashLight->SetAttenuationRadius(6000.f);
	FlashLight->SetCastShadows(false);
	FlashLight->SetIntensity(0.f);

	BlastForce = CreateDefaultSubobject<URadialForceComponent>(TEXT("BlastForce"));
	BlastForce->SetupAttachment(Root);
	BlastForce->bImpulseVelChange = true;
	BlastForce->bAutoActivate = false;
	BlastForce->Radius = 900.f;
	BlastForce->ImpulseStrength = 1400.f;
	BlastForce->DestructibleDamage = 0.f;

	// Size-graded systems from the Fire_EXP_Vol01_Free pack. Each finder fails quietly if the
	// pack is absent, leaving the code fallback in place.
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SmallFX(
		TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Small_002.NS_Sub_EXP_Small_002"));
	if (SmallFX.Succeeded())
	{
		ExplosionFXSmall = SmallFX.Object;
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> MediumFX(
		TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Mid_002_02.NS_Sub_EXP_Mid_002_02"));
	if (MediumFX.Succeeded())
	{
		ExplosionFXMedium = MediumFX.Object;
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> LargeFX(
		TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Large_001_01.NS_Sub_EXP_Large_001_01"));
	if (LargeFX.Succeeded())
	{
		ExplosionFXLarge = LargeFX.Object;
	}

	// The authored-override path is resolved in BeginPlay rather than here. ConstructorHelpers
	// logs a hard Error when an asset is missing, and this one is optional by design -- an
	// error every launch for a file nobody is required to create is just noise.
}

void AExplosionEffect::TryLoadAuthoredOverride()
{
	if (ExplosionFX)
	{
		return;
	}

	// See docs/NIAGARA_EXPLOSION.md: a system authored here overrides the pack outright.
	static const FSoftObjectPath AuthoredPath(TEXT("/Game/FX/NS_Explosion.NS_Explosion"));
	if (UObject* Loaded = AuthoredPath.ResolveObject())
	{
		ExplosionFX = Cast<UNiagaraSystem>(Loaded);
	}
}

UNiagaraSystem* AExplosionEffect::ResolveExplosionSystem() const
{
	if (ExplosionFX)
	{
		return ExplosionFX;
	}

	// Walk down from the best match, so a partially imported pack still produces something
	// rather than silently falling back to the debug sphere.
	if (Radius >= LargeBlastThreshold)
	{
		if (ExplosionFXLarge)  { return ExplosionFXLarge; }
		if (ExplosionFXMedium) { return ExplosionFXMedium; }
		return ExplosionFXSmall;
	}

	if (Radius >= MediumBlastThreshold)
	{
		if (ExplosionFXMedium) { return ExplosionFXMedium; }
		if (ExplosionFXLarge)  { return ExplosionFXLarge; }
		return ExplosionFXSmall;
	}

	if (ExplosionFXSmall)  { return ExplosionFXSmall; }
	if (ExplosionFXMedium) { return ExplosionFXMedium; }
	return ExplosionFXLarge;
}

AExplosionEffect* AExplosionEffect::Spawn(UWorld* World, const FVector& Location, float InRadius, float Intensity)
{
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AExplosionEffect* Effect = World->SpawnActor<AExplosionEffect>(AExplosionEffect::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (Effect)
	{
		Effect->Radius = InRadius;
		Effect->PeakLightIntensity *= Intensity * 2.f;
		Effect->BlastForce->Radius = InRadius * 1.5f;
		Effect->BlastForce->ImpulseStrength = 2600.f * Intensity;

		// Lights the surroundings far beyond the lethal radius, which is most of what makes a
		// detonation feel large from the air.
		Effect->FlashLight->SetAttenuationRadius(FMath::Max(InRadius * 6.f, 4000.f));
	}
	return Effect;
}

void AExplosionEffect::BeginPlay()
{
	Super::BeginPlay();

	TryLoadAuthoredOverride();

	UNiagaraSystem* System = ResolveExplosionSystem();

	if (System)
	{
		float FXScale = bScaleFXToRadius ? FMath::Max(Radius / 100.f, 0.1f) : 1.f;
		FXScale *= (GExplosionScaleOverride > 0.f)
			? GExplosionScaleOverride
			: FMath::Max(FXScaleMultiplier, 0.01f);

		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), System, GetActorLocation(), FRotator::ZeroRotator,
			FVector(FXScale), /*bAutoDestroy=*/true);
	}

	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), ExplosionSound, GetActorLocation());
	}

	// The light is kept alongside real VFX by default: a Niagara fireball emits no actual light,
	// so without this the surroundings stay unlit through the brightest moment of the explosion.
	if (bAlwaysUseLightFlash || !System)
	{
		FlashLight->SetIntensity(PeakLightIntensity);
	}

	BlastForce->FireImpulse();

	// A Niagara system outlives the flash, so hang around long enough for it to finish.
	SetLifeSpan(System ? FMath::Max(Duration, 6.f) : Duration + 0.1f);
}

void AExplosionEffect::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Elapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(Elapsed / FMath::Max(Duration, KINDA_SMALL_NUMBER), 0.f, 1.f);

	// Sharp flash that decays quickly, which is what a detonation actually looks like.
	FlashLight->SetIntensity(PeakLightIntensity * FMath::Pow(1.f - Alpha, 3.f));

#if ENABLE_DRAW_DEBUG
	// Stand-in for a fireball, suppressed as soon as real VFX is available.
	if (ResolveExplosionSystem())
	{
		return;
	}

	const float BallRadius = Radius * FMath::Sqrt(Alpha) * 0.9f;
	const uint8 FadeAlpha = static_cast<uint8>(255.f * (1.f - Alpha));
	DrawDebugSphere(GetWorld(), GetActorLocation(), BallRadius, 16,
		FColor(255, 150 - FMath::Min<int32>(150, (int32)(Alpha * 150)), 40, FadeAlpha), false, -1.f, 0, 2.f);
#endif
}
