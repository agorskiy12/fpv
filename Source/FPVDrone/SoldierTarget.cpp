#include "SoldierTarget.h"
#include "FPVDrone.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// The pack folder contains an en dash, which is awkward to embed directly in source. The
	// escape keeps this file plain ASCII and avoids depending on the compiler's source encoding.
	const TCHAR* SoldierPackRoot =
		TEXT("/Game/Fab/Middle_Eastern_Armed_Fighter_–_Realistic_Soldier___Game-Ready__Rigged___Animated/");
}

ASoldierTarget::ASoldierTarget()
{
	PrimaryActorTick.bCanEverTick = true;

	Kind = ETargetKind::Structure;   // only affects the unused primitive fallback
	MaxHealth = 30.f;
	ScoreValue = 120;
	bPrimaryObjective = false;
	bSecondaryExplosion = false;

	// Roughly a standing person. Drives blast falloff and HUD marker sizing.
	BodySize = FVector(60.f, 60.f, 180.f);

	// No debris burst: a ragdoll is both cheaper and far more appropriate than throwing chunks.
	DebrisCount = 0;

	SoldierMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SoldierMesh"));
	SoldierMesh->SetupAttachment(TargetRoot);
	SoldierMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	SoldierMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	SoldierMesh->SetGenerateOverlapEvents(false);

	const FString MeshPath = FString(SoldierPackRoot) + TEXT("Afghan_Vest_Low.Afghan_Vest_Low");
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BodyFinder(*MeshPath);
	if (BodyFinder.Succeeded())
	{
		SoldierMesh->SetSkeletalMesh(BodyFinder.Object);
	}

	const FString WalkPath = FString(SoldierPackRoot) + TEXT("afghanbetter_fbxWalk__1_24_.afghanbetter_fbxWalk__1_24_");
	static ConstructorHelpers::FObjectFinder<UAnimSequence> WalkFinder(*WalkPath);
	if (WalkFinder.Succeeded())
	{
		WalkAnimation = WalkFinder.Object;
	}

	const FString RunPath = FString(SoldierPackRoot) + TEXT("afghanbetter_fbxRun__1_18_.afghanbetter_fbxRun__1_18_");
	static ConstructorHelpers::FObjectFinder<UAnimSequence> RunFinder(*RunPath);
	if (RunFinder.Succeeded())
	{
		RunAnimation = RunFinder.Object;
	}

	PlaceholderCube = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderCube"));
	PlaceholderCube->SetupAttachment(TargetRoot);
	PlaceholderCube->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderCube->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		PlaceholderCube->SetStaticMesh(CubeMesh.Object);
	}

	// A short there-and-back, so a freshly placed soldier already walks a beat.
	PatrolPoints = { FVector::ZeroVector, FVector(1600.f, 0.f, 0.f) };
}

FLinearColor ASoldierTarget::GetPlaceholderColour() const
{
	switch (Faction)
	{
	case EFaction::Russia: return FLinearColor(1.f, 0.05f, 0.05f);
	case EFaction::NATO:   return FLinearColor(0.1f, 1.f, 0.15f);
	default:               return FLinearColor(0.45f, 0.45f, 0.45f);   // civilians and scenery
	}
}

void ASoldierTarget::ApplyPlaceholderCube()
{
	if (!PlaceholderCube || !bUseFactionCube)
	{
		return;
	}

	// Person-sized, standing on the ground rather than centred on it.
	PlaceholderCube->SetRelativeScale3D(FVector(
		BodySize.X / 100.f, BodySize.Y / 100.f, BodySize.Z / 100.f));
	PlaceholderCube->SetRelativeLocation(FVector(0.f, 0.f, BodySize.Z * 0.5f));
	PlaceholderCube->SetVisibility(true);

	// The character model is hidden rather than removed, so turning the cube off restores it.
	if (SoldierMesh)
	{
		SoldierMesh->SetVisibility(false);
	}

	UMaterialInterface* Source = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Source)
	{
		return;
	}

	// Which parameter carries the colour varies between engine materials, so the names are
	// logged once and every plausible one is set. Setting a parameter a material does not have
	// is a silent no-op, so trying several costs nothing.
	static bool bLoggedParameters = false;
	if (!bLoggedParameters)
	{
		bLoggedParameters = true;
		TArray<FMaterialParameterInfo> ParameterInfos;
		TArray<FGuid> Guids;
		Source->GetAllVectorParameterInfo(ParameterInfos, Guids);

		FString Names;
		for (const FMaterialParameterInfo& Info : ParameterInfos)
		{
			Names += Info.Name.ToString() + TEXT(" ");
		}
		UE_LOG(LogFPV, Log, TEXT("BasicShapeMaterial vector parameters: %s"),
			Names.IsEmpty() ? TEXT("(none)") : *Names);
	}

	if (UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Source, this))
	{
		const FLinearColor Colour = GetPlaceholderColour();
		Dynamic->SetVectorParameterValue(TEXT("Color"), Colour);
		Dynamic->SetVectorParameterValue(TEXT("BaseColor"), Colour);
		Dynamic->SetVectorParameterValue(TEXT("Tint"), Colour);
		PlaceholderCube->SetMaterial(0, Dynamic);
	}
}

void ASoldierTarget::BeginPlay()
{
	Super::BeginPlay();

	bPrimaryObjective = (SoldierRole == ESoldierRole::Objective);
	ScoreValue = (SoldierRole == ESoldierRole::Objective) ? 200 : 120;

	if (SoldierMesh)
	{
		SoldierMesh->SetRelativeRotation(MeshFacingCorrection);

		// Character meshes usually sit with feet at the origin, so no vertical offset is needed
		// -- only the scale, which varies wildly between imports.
		if (bAutoScaleMesh && SoldierMesh->GetSkeletalMeshAsset())
		{
			const float MeshHeight = SoldierMesh->GetSkeletalMeshAsset()->GetBounds().BoxExtent.Z * 2.f;
			if (MeshHeight > KINDA_SMALL_NUMBER)
			{
				SoldierMesh->SetRelativeScale3D(FVector(StandingHeight / MeshHeight));
			}

			UE_LOG(LogFPV, Log, TEXT("Soldier mesh height %.0f cm, scaled to %.0f cm"),
				MeshHeight, StandingHeight);
		}
	}

	ApplyPlaceholderCube();

	WorldRoute.Reset();
	const FTransform SpawnTransform = GetActorTransform();
	for (const FVector& Point : PatrolPoints)
	{
		WorldRoute.Add(SpawnTransform.TransformPosition(Point));
	}
	CurrentPoint = (WorldRoute.Num() > 1) ? 1 : 0;

	UpdateLocomotionAnimation();
}

void ASoldierTarget::UpdateLocomotionAnimation()
{
	if (!SoldierMesh)
	{
		return;
	}

	// A looping single-node animation is enough here; nothing needs blending, so an Anim
	// Blueprint would be all overhead and no benefit.
	UAnimSequence* Desired = bAlerted ? RunAnimation : WalkAnimation;
	if (!Desired)
	{
		Desired = WalkAnimation ? WalkAnimation : RunAnimation;
	}

	if (Desired && bRunningAnimActive != bAlerted)
	{
		SoldierMesh->PlayAnimation(Desired, /*bLooping=*/true);
		bRunningAnimActive = bAlerted;
	}
	else if (Desired && !SoldierMesh->IsPlaying())
	{
		SoldierMesh->PlayAnimation(Desired, /*bLooping=*/true);
	}
}

FVector ASoldierTarget::GetCurrentVelocity() const
{
	if (IsDestroyed() || WorldRoute.Num() < 2)
	{
		return FVector::ZeroVector;
	}
	return GetActorForwardVector() * (bAlerted ? RunSpeed : WalkSpeed);
}

void ASoldierTarget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsDestroyed() || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Alert state, with hysteresis so standing at the boundary does not flicker the animation
	// between walk and run every frame.
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		const float DistanceSq = FVector::DistSquared(Player->GetActorLocation(), GetActorLocation());
		if (!bAlerted && DistanceSq < FMath::Square(AlertRadius))
		{
			bAlerted = true;
		}
		else if (bAlerted && DistanceSq > FMath::Square(AlertRadius * 1.4f))
		{
			bAlerted = false;
		}
	}

	UpdateLocomotionAnimation();

	if (bPatrolFinished || WorldRoute.Num() < 2)
	{
		return;   // standing watch
	}

	const FVector Location = GetActorLocation();
	FVector ToPoint = WorldRoute[CurrentPoint] - Location;
	ToPoint.Z = 0.f;

	if (ToPoint.SizeSquared() < FMath::Square(WaypointTolerance))
	{
		++CurrentPoint;
		if (CurrentPoint >= WorldRoute.Num())
		{
			if (bLoopPatrol)
			{
				CurrentPoint = 0;
			}
			else
			{
				CurrentPoint = WorldRoute.Num() - 1;
				bPatrolFinished = true;
				return;
			}
		}
		ToPoint = WorldRoute[CurrentPoint] - Location;
		ToPoint.Z = 0.f;
	}

	if (ToPoint.IsNearlyZero())
	{
		return;
	}

	const FRotator Desired = FRotator(0.f, ToPoint.Rotation().Yaw, 0.f);
	SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Desired, DeltaSeconds, TurnRateDegrees));

	const float Speed = bAlerted ? RunSpeed : WalkSpeed;
	SetActorLocation(Location + GetActorForwardVector() * Speed * DeltaSeconds, true);
}

void ASoldierTarget::OnDestroyed_Internal(AActor* Killer)
{
	const FVector BlastOrigin = GetAimPoint();

	// Base handles score, the explosion and mission bookkeeping. Debris is disabled for
	// soldiers, so nothing inappropriate is thrown.
	Super::OnDestroyed_Internal(Killer);

	CollapseIntoRagdoll(BlastOrigin);
}

void ASoldierTarget::CollapseIntoRagdoll(const FVector& BlastOrigin)
{
	SetActorTickEnabled(false);

	// With the placeholder cube there is no skeleton to go limp, so it is thrown as a single
	// rigid body instead. Cruder, but it still reads as a hit rather than a disappearance.
	if (bUseFactionCube && PlaceholderCube && PlaceholderCube->IsVisible())
	{
		PlaceholderCube->SetCollisionProfileName(TEXT("PhysicsActor"));
		PlaceholderCube->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		PlaceholderCube->SetSimulatePhysics(true);

		const FVector Impulse = (GetActorLocation() - BlastOrigin).GetSafeNormal() + FVector(0.f, 0.f, 0.7f);
		PlaceholderCube->AddImpulse(Impulse.GetSafeNormal() * 30000.f, NAME_None, false);
		PlaceholderCube->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * 400.f);

		SetLifeSpan(20.f);
		return;
	}

	if (!SoldierMesh || !SoldierMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	// The base class hid the primitive body; the skeletal mesh stays and goes limp instead.
	// The pack ships a physics asset, which is what makes this possible at all.
	SoldierMesh->SetVisibility(true);
	SoldierMesh->SetCollisionProfileName(TEXT("Ragdoll"));
	SoldierMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	SoldierMesh->SetAllBodiesSimulatePhysics(true);
	SoldierMesh->SetSimulatePhysics(true);
	SoldierMesh->WakeAllRigidBodies();

	// Thrown by the blast rather than simply dropping, so the direction of the hit is legible.
	const FVector Impulse = (GetActorLocation() - BlastOrigin).GetSafeNormal() + FVector(0.f, 0.f, 0.6f);
	SoldierMesh->AddImpulse(Impulse.GetSafeNormal() * 42000.f, NAME_None, /*bVelChange=*/false);

	SetLifeSpan(20.f);
}
