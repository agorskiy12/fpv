#include "DroneTarget.h"
#include "DebrisChunk.h"
#include "ExplosionEffect.h"
#include "FPVDrone.h"
#include "FPVWarGameMode.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ADroneTarget::ADroneTarget()
{
	PrimaryActorTick.bCanEverTick = false;

	TargetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TargetRoot"));
	SetRootComponent(TargetRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	for (int32 Index = 0; Index < NumBodyParts; ++Index)
	{
		const FName PartName = *FString::Printf(TEXT("BodyPart%d"), Index);
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(PartName);
		Part->SetupAttachment(TargetRoot);
		if (CubeMesh.Succeeded())
		{
			Part->SetStaticMesh(CubeMesh.Object);
		}
		Part->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		BodyParts.Add(Part);
	}

	OverrideMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OverrideMesh"));
	OverrideMesh->SetupAttachment(TargetRoot);
	OverrideMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	OverrideMesh->SetVisibility(false);
	OverrideMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ADroneTarget::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildBody();
}

void ADroneTarget::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	RebuildBody();

	if (AFPVWarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPVWarGameMode>() : nullptr)
	{
		GameMode->RegisterTarget(this);
	}

	// Imported meshes arrive at wildly different scales depending on how they were authored, and
	// a mesh hundreds of metres across is indistinguishable from a broken game when you are
	// inside it. Report the real extent so it can be checked rather than guessed at.
	if (BodyMesh && OverrideMesh)
	{
		const FVector MeshExtent = OverrideMesh->Bounds.BoxExtent * 2.f;
		UE_LOG(LogFPV, Log, TEXT("%s mesh '%s' actual size %.0f x %.0f x %.0f cm (BodySize says %.0f x %.0f x %.0f) at %s"),
			*GetDisplayName(), *BodyMesh->GetName(),
			MeshExtent.X, MeshExtent.Y, MeshExtent.Z,
			BodySize.X, BodySize.Y, BodySize.Z,
			*GetActorLocation().ToCompactString());
	}
}

// ---------------------------------------------------------------------------------------------
// Body construction
// ---------------------------------------------------------------------------------------------

void ADroneTarget::SetPart(int32 Index, const FVector& RelativeLocation, const FVector& SizeCm, bool /*bCylinder*/)
{
	if (!BodyParts.IsValidIndex(Index) || !BodyParts[Index])
	{
		return;
	}

	UStaticMeshComponent* Part = BodyParts[Index];
	Part->SetVisibility(true);
	Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Part->SetRelativeLocation(RelativeLocation);
	// The engine cube is 100 units per side, so scale is size-in-cm / 100.
	Part->SetRelativeScale3D(SizeCm / 100.f);

	PartsUsed = FMath::Max(PartsUsed, Index + 1);
}

void ADroneTarget::HideUnusedParts()
{
	for (int32 Index = PartsUsed; Index < BodyParts.Num(); ++Index)
	{
		if (BodyParts[Index])
		{
			BodyParts[Index]->SetVisibility(false);
			BodyParts[Index]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void ADroneTarget::RebuildBody()
{
	PartsUsed = 0;

	// A real mesh replaces the placeholder outright. Gameplay still reads BodySize, so swapping
	// art in does not silently change blast falloff or HUD marker sizing.
	if (BodyMesh)
	{
		OverrideMesh->SetStaticMesh(BodyMesh);
		OverrideMesh->SetRelativeLocation(MeshOffset);
		OverrideMesh->SetRelativeRotation(MeshRotation);

		FVector AppliedScale = MeshScale;
		if (bAutoScaleMeshToBodySize)
		{
			// Match the mesh's longest axis to the longest axis of the gameplay footprint.
			// Uniform, so the model is never stretched -- only resized.
			const FVector MeshSize = BodyMesh->GetBoundingBox().GetSize();
			const float LongestMesh = MeshSize.GetMax();
			const float LongestBody = BodySize.GetMax();

			if (LongestMesh > KINDA_SMALL_NUMBER && LongestBody > KINDA_SMALL_NUMBER)
			{
				AppliedScale = FVector(LongestBody / LongestMesh);
			}
		}
		OverrideMesh->SetRelativeScale3D(AppliedScale);
		OverrideMesh->SetVisibility(true);
		OverrideMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

		if (BodyMaterial)
		{
			const int32 NumMaterials = OverrideMesh->GetNumMaterials();
			for (int32 Slot = 0; Slot < NumMaterials; ++Slot)
			{
				OverrideMesh->SetMaterial(Slot, BodyMaterial);
			}
		}

		HideUnusedParts();   // PartsUsed is 0, so this hides all of them
		return;
	}

	OverrideMesh->SetVisibility(false);
	OverrideMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	const FVector Size = BodySize.ComponentMax(FVector(20.f));
	const float HalfZ = Size.Z * 0.5f;

	switch (Kind)
	{
	case ETargetKind::Structure:
	{
		// Main block with a slightly narrower roof, so orientation reads from the air.
		SetPart(0, FVector(0.f, 0.f, HalfZ), Size);
		SetPart(1, FVector(0.f, 0.f, Size.Z + 20.f), FVector(Size.X * 0.85f, Size.Y * 0.85f, 40.f));
		break;
	}

	case ETargetKind::GasLine:
	{
		// A long run of pipe on short supports. Deliberately thin -- it is a hard hit.
		const float Length = Size.X;
		const float Bore = FMath::Max(Size.Z * 0.35f, 40.f);
		SetPart(0, FVector(0.f, 0.f, HalfZ), FVector(Length, Bore, Bore));
		SetPart(1, FVector(-Length * 0.35f, 0.f, HalfZ * 0.5f), FVector(Bore * 0.6f, Bore * 0.6f, Size.Z * 0.5f));
		SetPart(2, FVector(Length * 0.35f, 0.f, HalfZ * 0.5f), FVector(Bore * 0.6f, Bore * 0.6f, Size.Z * 0.5f));
		break;
	}

	case ETargetKind::ElectricalStation:
	{
		// Transformer housing plus pylons -- a cluster, so near misses still connect.
		SetPart(0, FVector(0.f, 0.f, Size.Z * 0.3f), FVector(Size.X * 0.6f, Size.Y * 0.6f, Size.Z * 0.6f));
		SetPart(1, FVector(-Size.X * 0.35f, -Size.Y * 0.3f, HalfZ), FVector(50.f, 50.f, Size.Z));
		SetPart(2, FVector(-Size.X * 0.35f,  Size.Y * 0.3f, HalfZ), FVector(50.f, 50.f, Size.Z));
		SetPart(3, FVector( Size.X * 0.35f, -Size.Y * 0.3f, HalfZ), FVector(50.f, 50.f, Size.Z));
		SetPart(4, FVector( Size.X * 0.35f,  Size.Y * 0.3f, HalfZ), FVector(50.f, 50.f, Size.Z));
		SetPart(5, FVector(0.f, 0.f, Size.Z + 25.f), FVector(Size.X * 0.9f, 60.f, 50.f));
		break;
	}

	case ETargetKind::Vehicle:
	{
		// Body, cab, and a suggestion of wheels.
		SetPart(0, FVector(0.f, 0.f, Size.Z * 0.45f), FVector(Size.X, Size.Y, Size.Z * 0.5f));
		SetPart(1, FVector(-Size.X * 0.1f, 0.f, Size.Z * 0.85f), FVector(Size.X * 0.45f, Size.Y * 0.85f, Size.Z * 0.45f));
		SetPart(2, FVector(Size.X * 0.32f, 0.f, Size.Z * 0.2f), FVector(Size.X * 0.18f, Size.Y * 1.05f, Size.Z * 0.3f));
		SetPart(3, FVector(-Size.X * 0.32f, 0.f, Size.Z * 0.2f), FVector(Size.X * 0.18f, Size.Y * 1.05f, Size.Z * 0.3f));
		break;
	}

	case ETargetKind::Helicopter:
	{
		// Placeholder only -- a real airframe mesh replaces all of this. Fuselage, tail boom,
		// tail fin and a rotor disc, enough to be identifiable if the mesh ever fails to load.
		SetPart(0, FVector(0.f, 0.f, 0.f), FVector(Size.X * 0.55f, Size.Y, Size.Z * 0.8f));
		SetPart(1, FVector(-Size.X * 0.45f, 0.f, Size.Z * 0.1f), FVector(Size.X * 0.5f, Size.Y * 0.25f, Size.Z * 0.25f));
		SetPart(2, FVector(-Size.X * 0.65f, 0.f, Size.Z * 0.4f), FVector(Size.Y * 0.2f, Size.Y * 0.2f, Size.Z * 0.6f));
		SetPart(3, FVector(0.f, 0.f, Size.Z * 0.6f), FVector(Size.X * 1.15f, Size.X * 1.15f, 12.f));
		break;
	}

	case ETargetKind::Drone:
	{
		// Small centre body with four arms. Tiny on purpose -- interception should be hard.
		const float Arm = Size.X * 0.5f;
		SetPart(0, FVector::ZeroVector, FVector(Size.X * 0.35f, Size.Y * 0.35f, Size.Z * 0.4f));
		SetPart(1, FVector( Arm * 0.6f,  Arm * 0.6f, 0.f), FVector(Arm * 0.7f, 30.f, 20.f));
		SetPart(2, FVector( Arm * 0.6f, -Arm * 0.6f, 0.f), FVector(Arm * 0.7f, 30.f, 20.f));
		SetPart(3, FVector(-Arm * 0.6f,  Arm * 0.6f, 0.f), FVector(Arm * 0.7f, 30.f, 20.f));
		SetPart(4, FVector(-Arm * 0.6f, -Arm * 0.6f, 0.f), FVector(Arm * 0.7f, 30.f, 20.f));
		break;
	}
	}

	HideUnusedParts();
}

// ---------------------------------------------------------------------------------------------
// Damage
// ---------------------------------------------------------------------------------------------

void ADroneTarget::ApplyBlastDamage(float Damage, const FVector& BlastOrigin, AActor* DamageCauser)
{
	if (bDestroyed || Damage <= 0.f)
	{
		return;
	}

	Health -= Damage;

	UE_LOG(LogFPV, Verbose, TEXT("%s took %.0f damage (%.0f/%.0f left)"),
		*GetDisplayName(), Damage, FMath::Max(Health, 0.f), MaxHealth);

	if (Health <= 0.f)
	{
		bDestroyed = true;
		OnDestroyed_Internal(DamageCauser);
	}
}

void ADroneTarget::OnDestroyed_Internal(AActor* Killer)
{
	const FVector Centre = GetAimPoint();

	AExplosionEffect::Spawn(GetWorld(), Centre, GetApproximateRadius() * 1.6f, 1.f);

	if (AFPVWarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPVWarGameMode>() : nullptr)
	{
		GameMode->NotifyTargetDestroyed(this, Killer);
	}

	// Chain reaction. Applied before the body is hidden so the explosion reads as coming from
	// the structure itself.
	if (bSecondaryExplosion)
	{
		AExplosionEffect::Spawn(GetWorld(), Centre, SecondaryBlastRadius, 1.8f);

		if (AFPVWarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPVWarGameMode>() : nullptr)
		{
			GameMode->ApplyBlast(Centre, SecondaryBlastRadius, SecondaryBlastDamage, Killer, this);
		}
	}

	// Throw physical debris. This is what actually reads as the target coming apart.
	{
		TArray<UStaticMesh*> Chunks;
		Chunks.Reserve(DebrisMeshes.Num());
		for (const TObjectPtr<UStaticMesh>& Entry : DebrisMeshes)
		{
			if (Entry)
			{
				Chunks.Add(Entry);
			}
		}

		ADebrisChunk::SpawnBurst(GetWorld(), Centre, GetCurrentVelocity(),
			DebrisCount, DebrisSpeed, DebrisChunkSize, Chunks);
	}

	// Airborne targets fall rather than vanish, and everything below is skipped -- the body is
	// still needed to tumble.
	if (bFallOnDestruction && OverrideMesh && BodyMesh)
	{
		BeginFallingWreck();
		UE_LOG(LogFPV, Log, TEXT("Destroyed: %s (+%d) -- going down"), *GetDisplayName(), ScoreValue);
		return;
	}

	// A wreck mesh leaves something behind to fly past; without one the target simply goes away.
	if (DestroyedMesh)
	{
		OverrideMesh->SetStaticMesh(DestroyedMesh);
		OverrideMesh->SetRelativeLocation(MeshOffset);
		OverrideMesh->SetRelativeRotation(MeshRotation);
		OverrideMesh->SetRelativeScale3D(MeshScale);
		OverrideMesh->SetVisibility(true);
		OverrideMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else
	{
		OverrideMesh->SetVisibility(false);
		OverrideMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	for (UStaticMeshComponent* Part : BodyParts)
	{
		if (Part)
		{
			Part->SetVisibility(false);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	UE_LOG(LogFPV, Log, TEXT("Destroyed: %s (+%d)"), *GetDisplayName(), ScoreValue);
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

void ADroneTarget::BeginFallingWreck()
{
	// Hand the body over to the physics solver, carrying the velocity it was flying at. Dead
	// engines mean it keeps its momentum and loses its lift, which is exactly what falling is.
	OverrideMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	OverrideMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	OverrideMesh->SetSimulatePhysics(true);
	OverrideMesh->SetEnableGravity(true);
	OverrideMesh->SetNotifyRigidBodyCollision(true);

	OverrideMesh->SetPhysicsLinearVelocity(GetCurrentVelocity());

	// Tumble hard enough to read as out of control, but not so fast it becomes a blur.
	OverrideMesh->SetPhysicsAngularVelocityInDegrees(
		FVector(FMath::FRandRange(-90.f, 90.f), FMath::FRandRange(-60.f, 60.f), FMath::FRandRange(-160.f, 160.f)));

	OverrideMesh->OnComponentHit.AddDynamic(this, &ADroneTarget::OnWreckHit);

	// Trail smoke the whole way down so it is trackable against the sky.
	AExplosionEffect::Spawn(GetWorld(), GetAimPoint(), GetApproximateRadius() * 0.8f, 0.5f);
}

void ADroneTarget::OnWreckHit(UPrimitiveComponent* /*HitComponent*/, AActor* /*OtherActor*/,
	UPrimitiveComponent* /*OtherComp*/, FVector /*NormalImpulse*/, const FHitResult& Hit)
{
	if (bWreckImpacted)
	{
		return;
	}
	bWreckImpacted = true;

	const FVector ImpactPoint = Hit.ImpactPoint;

	AExplosionEffect::Spawn(GetWorld(), ImpactPoint, WreckImpactBlastRadius, 1.6f);

	if (AFPVWarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPVWarGameMode>() : nullptr)
	{
		GameMode->ApplyBlast(ImpactPoint, WreckImpactBlastRadius, WreckImpactBlastDamage, this, this);
	}

	// A second burst on impact, so the ground hit is its own event rather than a quiet landing.
	TArray<UStaticMesh*> Chunks;
	for (const TObjectPtr<UStaticMesh>& Entry : DebrisMeshes)
	{
		if (Entry)
		{
			Chunks.Add(Entry);
		}
	}
	ADebrisChunk::SpawnBurst(GetWorld(), ImpactPoint, FVector::ZeroVector,
		DebrisCount, DebrisSpeed * 0.8f, DebrisChunkSize, Chunks);

	UE_LOG(LogFPV, Log, TEXT("%s wreck impacted at %s"), *GetDisplayName(), *ImpactPoint.ToCompactString());

	// Leave the hull lying there for a while, then clear it.
	OverrideMesh->SetSimulatePhysics(false);
	SetLifeSpan(25.f);
}

FVector ADroneTarget::GetAimPoint() const
{
	// Centre of the body rather than the actor origin, which sits on the ground for most kinds.
	const float Lift = (Kind == ETargetKind::Drone) ? 0.f : BodySize.Z * 0.5f;
	return GetActorLocation() + GetActorUpVector() * Lift;
}

float ADroneTarget::GetApproximateRadius() const
{
	return FMath::Max3(BodySize.X, BodySize.Y, BodySize.Z) * 0.5f;
}

FString ADroneTarget::GetDisplayName() const
{
	switch (Kind)
	{
	case ETargetKind::Structure:         return TEXT("STRUCTURE");
	case ETargetKind::GasLine:           return TEXT("GAS LINE");
	case ETargetKind::ElectricalStation: return TEXT("SUBSTATION");
	case ETargetKind::Vehicle:           return TEXT("VEHICLE");
	case ETargetKind::Drone:             return TEXT("UAV");
	case ETargetKind::Helicopter:        return TEXT("HELICOPTER");
	default:                             return TEXT("TARGET");
	}
}
