#include "FPVWarGameMode.h"
#include "BannerMarker.h"
#include "DroneTarget.h"
#include "EnemyDroneTarget.h"
#include "FPVDrone.h"
#include "FPVDronePawn.h"
#include "FPVHUD.h"
#include "FPVPlayerState.h"
#include "HelicopterTarget.h"
#include "OperatorTarget.h"
#include "SoldierTarget.h"
#include "StrikeCamera.h"
#include "TerrainScatter.h"
#include "VehicleTarget.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "TimerManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarSpawnGroundPlane(
		TEXT("fpv.SpawnGroundPlane"),
		1,
		TEXT("Spawn the placeholder ground slab with the test targets.\n")
		TEXT("Set to 0 once the level has a real Landscape, or the two will z-fight."),
		ECVF_Default);

	/** The drivable surface found on a scene mesh, in world space. */
	struct FDeckSurface
	{
		bool bValid = false;
		FBox Extent = FBox(ForceInit);
		float DeckZ = 0.f;
		bool bAlongX = true;
	};

	/**
	 * Locate the drivable deck by tracing the geometry, rather than trusting the bounding box.
	 *
	 * The runway asset is not a slab -- it is an entire scene, sea included, spanning 79 km. Its
	 * bounding box centre lands in open water, which is why anything positioned from those
	 * bounds ends up hovering offshore.
	 *
	 * So the surface is measured instead: cast a grid of downward traces, bin the hit heights,
	 * and take the highest plateau that covers a meaningful share of them. On an elevated
	 * concrete deck that is the deck; sea and terrain sit lower and get discarded.
	 */
	FDeckSurface FindDeckSurface(UWorld* World, AActor* SceneActor, const FBox& WorldBounds)
	{
		FDeckSurface Result;
		if (!World || !SceneActor)
		{
			return Result;
		}

		constexpr int32 SamplesX = 60;
		constexpr int32 SamplesY = 30;
		constexpr float HeightBinSize = 100.f;

		const float TraceTop = WorldBounds.Max.Z + 10000.f;
		const float TraceBottom = WorldBounds.Min.Z - 10000.f;

		// Complex traces hit the render geometry, which is what a scene mesh usually has. If the
		// asset only carries simple collision the first pass finds nothing, so both are tried.
		FCollisionQueryParams ComplexParams(SCENE_QUERY_STAT(FindDeckComplex), /*bTraceComplex=*/true);
		FCollisionQueryParams SimpleParams(SCENE_QUERY_STAT(FindDeckSimple), /*bTraceComplex=*/false);
		bool bUseComplex = true;

		// Height bin -> hit points that landed in it.
		TMap<int32, TArray<FVector>> Plateaus;
		int32 TotalHits = 0;

		for (int32 Pass = 0; Pass < 2 && TotalHits == 0; ++Pass)
		{
			bUseComplex = (Pass == 0);
			const FCollisionQueryParams& Params = bUseComplex ? ComplexParams : SimpleParams;

			for (int32 IX = 0; IX <= SamplesX; ++IX)
			{
				const float Alpha = static_cast<float>(IX) / SamplesX;
				const float X = FMath::Lerp(WorldBounds.Min.X, WorldBounds.Max.X, Alpha);

				for (int32 IY = 0; IY <= SamplesY; ++IY)
				{
					const float Beta = static_cast<float>(IY) / SamplesY;
					const float Y = FMath::Lerp(WorldBounds.Min.Y, WorldBounds.Max.Y, Beta);

					FHitResult Hit;
					const bool bHit = World->LineTraceSingleByChannel(
						Hit, FVector(X, Y, TraceTop), FVector(X, Y, TraceBottom), ECC_Visibility, Params);

					if (!bHit || Hit.GetActor() != SceneActor)
					{
						continue;   // sky, or something that is not the scene mesh
					}

					const int32 Bin = FMath::FloorToInt(Hit.ImpactPoint.Z / HeightBinSize);
					Plateaus.FindOrAdd(Bin).Add(Hit.ImpactPoint);
					++TotalHits;
				}
			}
		}

		UE_LOG(LogFPV, Log, TEXT("Deck search: %d hits using %s collision"),
			TotalHits, bUseComplex ? TEXT("complex") : TEXT("simple"));

		if (TotalHits == 0)
		{
			UE_LOG(LogFPV, Warning,
				TEXT("Deck search hit nothing -- the scene mesh probably has no collision."));
			return Result;
		}

		// The deck is the highest surface that still covers a decent share of the footprint.
		// A minimum share rejects railings, masts and other small high details.
		const int32 MinimumHits = FMath::Max(12, TotalHits / 40);

		int32 BestBin = TNumericLimits<int32>::Lowest();
		for (const TPair<int32, TArray<FVector>>& Pair : Plateaus)
		{
			if (Pair.Value.Num() >= MinimumHits && Pair.Key > BestBin)
			{
				BestBin = Pair.Key;
			}
		}

		if (BestBin == TNumericLimits<int32>::Lowest())
		{
			UE_LOG(LogFPV, Warning, TEXT("Deck search found no plateau across %d hits."), TotalHits);
			return Result;
		}

		// Merge the winning bin with its immediate neighbours, so a deck straddling a bin
		// boundary is not cut in half.
		TArray<FVector> DeckPoints;
		for (int32 Bin = BestBin - 1; Bin <= BestBin + 1; ++Bin)
		{
			if (const TArray<FVector>* Points = Plateaus.Find(Bin))
			{
				DeckPoints.Append(*Points);
			}
		}

		Result.Extent = FBox(ForceInit);
		float ZSum = 0.f;
		for (const FVector& Point : DeckPoints)
		{
			Result.Extent += Point;
			ZSum += Point.Z;
		}

		Result.DeckZ = ZSum / DeckPoints.Num();
		Result.bAlongX = Result.Extent.GetSize().X >= Result.Extent.GetSize().Y;
		Result.bValid = true;

		UE_LOG(LogFPV, Log,
			TEXT("Deck found: %d/%d hits, centre %s, %.0f x %.0f cm, surface Z=%.0f, long axis %s"),
			DeckPoints.Num(), TotalHits, *Result.Extent.GetCenter().ToCompactString(),
			Result.Extent.GetSize().X, Result.Extent.GetSize().Y, Result.DeckZ,
			Result.bAlongX ? TEXT("X") : TEXT("Y"));

		return Result;
	}

	/** Loads a set of meshes by path, dropping any the content pack did not ship. */
	TArray<UStaticMesh*> LoadMeshSet(const FString& Folder, const FString& Prefix, const TArray<FString>& Suffixes)
	{
		TArray<UStaticMesh*> Meshes;
		for (const FString& Suffix : Suffixes)
		{
			const FString Name = Prefix + Suffix;
			const FString Path = FString::Printf(TEXT("%s/%s.%s"), *Folder, *Name, *Name);
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path))
			{
				Meshes.Add(Mesh);
			}
		}
		return Meshes;
	}

	/**
	 * Dress the ground with rock, scrub and a distant skyline.
	 *
	 * A flat untextured plane to the horizon reads as a void no matter what else is in it: with
	 * nothing at intermediate distances there is no sense of scale or speed, and the horizon is a
	 * hard line. Cover breaks up the middle distance and the background mountains give the sky
	 * somewhere to stop.
	 *
	 * Everything here comes from the MWLandscapeAutoMaterial pack and degrades quietly if it is
	 * absent -- an empty mesh set simply scatters nothing.
	 */
	void GenerateTerrain(UWorld* World, const FVector& Centre, AActor* Runway)
	{
		ATerrainScatter* Scatter = World->SpawnActor<ATerrainScatter>(ATerrainScatter::StaticClass());
		if (!Scatter)
		{
			return;
		}

		const FString Root = TEXT("/Game/MWLandscapeAutoMaterial/Meshes");

		// The airfield is the one surface that must stay clear. Everything else -- flat slab or
		// sculpted landscape -- is fair ground.
		TArray<TWeakObjectPtr<AActor>> Airfield;
		if (Runway)
		{
			Airfield.Add(Runway);
		}

		// Distant skyline. Placed first so the far ring is stable regardless of what else is
		// scattered, and given no collision -- these are backdrop, not terrain you can hit.
		FScatterLayer Mountains;
		Mountains.Meshes = LoadMeshSet(Root / TEXT("Background"), TEXT("SM_MWAM_Mountain"), { TEXT("A"), TEXT("B") });
		Mountains.InnerRadius = 190000.f;
		Mountains.OuterRadius = 330000.f;
		Mountains.Count = 70;
		Mountains.TargetSize = 62000.f;      // ~600 m, enough to read as terrain at 2-3 km
		Mountains.ScaleVariance = 0.45f;
		Mountains.MaxSlopeDegrees = 25.f;   // they are enormous; a tilted one reads as broken
		Mountains.AvoidActors = Airfield;
		Mountains.bCollides = false;
		Scatter->ScatterLayer(Mountains, Centre, /*Seed=*/1701);

		// Rock across the approach. Leaned into the surface so it sits in the ground rather than
		// on it, and solid, because a boulder should end a low pass.
		FScatterLayer Rocks;
		Rocks.Meshes = LoadMeshSet(Root / TEXT("Cover"), TEXT("SM_MWAM_Stone"),
			{ TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") });
		Rocks.InnerRadius = 7000.f;
		Rocks.OuterRadius = 160000.f;
		Rocks.Count = 420;
		Rocks.TargetSize = 320.f;
		Rocks.ScaleVariance = 0.55f;
		Rocks.MaxSlopeDegrees = 55.f;      // rock is the one thing that belongs on a steep face
		Rocks.AvoidActors = Airfield;
		Rocks.NormalAlignment = 0.55f;
		Scatter->ScatterLayer(Rocks, Centre, /*Seed=*/2011);

		// Scrub, thickest close in where the drone spends most of its time low and fast. This is
		// the layer that actually conveys speed.
		FScatterLayer Scrub;
		Scrub.Meshes = LoadMeshSet(Root / TEXT("Plants"), TEXT("SM_MWAM_Grass"),
			{ TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") });
		Scrub.InnerRadius = 5000.f;
		Scrub.OuterRadius = 120000.f;
		Scrub.Count = 1600;
		Scrub.TargetSize = 150.f;
		Scrub.ScaleVariance = 0.4f;
		Scrub.MaxSlopeDegrees = 35.f;
		Scrub.AvoidActors = Airfield;
		Scrub.NormalAlignment = 0.2f;
		Scrub.bCollides = false;
		Scatter->ScatterLayer(Scrub, Centre, /*Seed=*/2311);
	}

	/**
	 * Drop a representative mission around the player.
	 *
	 * A scratch tool for tuning blast radius, damage and evade speeds without authoring a level
	 * first. Delete it once there are real maps.
	 */
	void SpawnTestTargets(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		const APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
		const FVector Origin = Player ? FVector(Player->GetActorLocation().X, Player->GetActorLocation().Y, 0.f)
									  : FVector::ZeroVector;

		// --- Ground ---------------------------------------------------------------------------
		// The Basic template floor is only a few tens of metres across, so everything placed
		// further out floats over nothing and the sky shows through underneath -- which reads
		// convincingly as water. A single large slab is enough to give the scene a floor.
		//
		// Skipped once the level has a real Landscape. The slab exists only so the scene is not
		// floating in void; a Landscape supersedes it entirely, and landscape materials cannot be
		// applied to a static mesh anyway.
		if (CVarSpawnGroundPlane.GetValueOnGameThread() != 0)
		{
			if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
			{
				constexpr float GroundExtent = 800000.f;   // 8 km across, comfortably past everything
				constexpr float GroundThickness = 400.f;

				// Top sits just below zero so it does not z-fight with the template floor, which
				// is coplanar at the origin.
				const FVector GroundCentre = Origin + FVector(22500.f, 0.f, -10.f - GroundThickness * 0.5f);

				FActorSpawnParameters GroundParams;
				GroundParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

				if (AStaticMeshActor* Ground = World->SpawnActor<AStaticMeshActor>(
					AStaticMeshActor::StaticClass(), GroundCentre, FRotator::ZeroRotator, GroundParams))
				{
					UStaticMeshComponent* GroundComponent = Ground->GetStaticMeshComponent();
					GroundComponent->SetMobility(EComponentMobility::Movable);
					GroundComponent->SetStaticMesh(CubeMesh);
					GroundComponent->SetWorldScale3D(FVector(
						GroundExtent / 100.f, GroundExtent / 100.f, GroundThickness / 100.f));
					GroundComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));

					if (UMaterialInterface* GroundMaterial = LoadObject<UMaterialInterface>(
						nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
					{
						GroundComponent->SetMaterial(0, GroundMaterial);
					}

					// Same trap as the runway: the mesh and scale are set after spawning, so the
					// collision body has to be rebuilt or nothing will land on it.
					GroundComponent->RecreatePhysicsState();

					UE_LOG(LogFPV, Log, TEXT("Ground plane: %.0f m across, top at Z=%.0f"),
						GroundExtent / 100.f, -10.f);
				}
			}
		}

		// --- Runway ---------------------------------------------------------------------------
		// Placed first, and everything else is laid out relative to it. The route is derived
		// from the mesh's measured bounds rather than hard-coded, so the layout holds whatever
		// size the asset turns out to be.
		float RunwayLength = 12000.f;
		float RunwayWidth = 3000.f;
		float RunwayDeckZ = 0.f;
		bool bRunwayAlongX = true;

		// Normalised to a playable length rather than trusted. The Fab runway measures 79 km
		// nose to tail as authored; at 130 km/h that is a thirty-five minute transit, and its
		// 846 m deck height put the vehicles in low orbit. 600 m gives a drone crossing of about
		// seventeen seconds and a vehicle lap of well under a minute.
		constexpr float TargetRunwayLength = 60000.f;

		// The runway is solid, and the drone must never start inside it. Offsetting the whole
		// scene puts the deck a comfortable flight away rather than under the launch point --
		// spawning inside collision looks like the game is broken, because the physics solver
		// spends every frame trying to push you out and you cannot move at all.
		FVector RunwayCentre = Origin + FVector(45000.f, 0.f, 0.f);

		// Held so the terrain pass can keep scrub and rock off the airfield.
		AStaticMeshActor* RunwayActor = nullptr;

		if (UStaticMesh* RunwayMesh = LoadObject<UStaticMesh>(nullptr,
			TEXT("/Game/Fab/Dubai_Skydive_Runway/bahn/StaticMeshes/bahn.bahn")))
		{
			const FBox MeshBounds = RunwayMesh->GetBoundingBox();
			const FVector RawSize = MeshBounds.GetSize();

			const float LongestAxis = FMath::Max(RawSize.X, RawSize.Y);
			const float RunwayScale = (LongestAxis > KINDA_SMALL_NUMBER)
				? TargetRunwayLength / LongestAxis
				: 1.f;

			const FVector MeshSize = RawSize * RunwayScale;

			// Centre the mesh on RunwayCentre in all three axes, not just vertically.
			//
			// Only Z was corrected before, which silently assumed the pivot sat at the middle of
			// the footprint. Pivots are just as often at a corner or one end, and when that is
			// the case the deck extends away from where the vehicles are driving -- so they run
			// down a strip of empty ground beside the runway rather than along it.
			const FVector BoundsCentre = MeshBounds.GetCenter() * RunwayScale;
			const FVector SpawnLocation = RunwayCentre - FVector(
				BoundsCentre.X,
				BoundsCentre.Y,
				MeshBounds.Min.Z * RunwayScale);

			FActorSpawnParameters RunwayParams;
			RunwayParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			RunwayActor = World->SpawnActor<AStaticMeshActor>(
				AStaticMeshActor::StaticClass(), SpawnLocation, FRotator::ZeroRotator, RunwayParams);

			if (AStaticMeshActor* Runway = RunwayActor)
			{
				UStaticMeshComponent* RunwayComponent = Runway->GetStaticMeshComponent();
				RunwayComponent->SetMobility(EComponentMobility::Movable);   // required when spawned at runtime
				RunwayComponent->SetStaticMesh(RunwayMesh);
				RunwayComponent->SetWorldScale3D(FVector(RunwayScale));
				RunwayComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));

				// Provisional values from the bounds; replaced below by what is actually there.
				bRunwayAlongX = MeshSize.X >= MeshSize.Y;
				RunwayLength = bRunwayAlongX ? MeshSize.X : MeshSize.Y;
				RunwayWidth = bRunwayAlongX ? MeshSize.Y : MeshSize.X;
				RunwayDeckZ = MeshSize.Z;

				// Physics state is built from the mesh and transform that were set *after*
				// spawning, so it has to be rebuilt before anything can trace against it.
				// Without this the deck search finds nothing and silently falls back.
				RunwayComponent->UpdateBounds();
				Runway->UpdateComponentTransforms();
				RunwayComponent->RecreatePhysicsState();

				const FBox WorldBounds = RunwayComponent->Bounds.GetBox();
				UE_LOG(LogFPV, Log, TEXT("Runway world bounds min %s max %s"),
					*WorldBounds.Min.ToCompactString(), *WorldBounds.Max.ToCompactString());

				// Whether the asset carries usable collision at all. Without it nothing can be
				// traced, measured, driven on, or crashed into.
				if (UBodySetup* Setup = RunwayMesh->GetBodySetup())
				{
					UE_LOG(LogFPV, Log,
						TEXT("Runway collision: trace flag %d, %d convex, %d box, %d sphere, tri-mesh %s"),
						static_cast<int32>(Setup->CollisionTraceFlag),
						Setup->AggGeom.ConvexElems.Num(),
						Setup->AggGeom.BoxElems.Num(),
						Setup->AggGeom.SphereElems.Num(),
						Setup->bHasCookedCollisionData ? TEXT("yes") : TEXT("no"));
				}
				else
				{
					UE_LOG(LogFPV, Warning, TEXT("Runway mesh has NO body setup -- no collision whatsoever."));
				}
				const FDeckSurface Deck = FindDeckSurface(World, Runway, WorldBounds);

				if (Deck.bValid)
				{
					// Everything is positioned from the measured surface from here on. The
					// bounding box describes an entire island; only this describes the road.
					RunwayCentre = FVector(Deck.Extent.GetCenter().X, Deck.Extent.GetCenter().Y, 0.f);
					RunwayLength = Deck.bAlongX ? Deck.Extent.GetSize().X : Deck.Extent.GetSize().Y;
					RunwayWidth = Deck.bAlongX ? Deck.Extent.GetSize().Y : Deck.Extent.GetSize().X;
					RunwayDeckZ = Deck.DeckZ;
					bRunwayAlongX = Deck.bAlongX;
				}
				else
				{
					UE_LOG(LogFPV, Warning,
						TEXT("Falling back to bounding-box layout; vehicles may not sit on the deck."));
				}

				UE_LOG(LogFPV, Log,
					TEXT("Runway at %s -- scaled %.4f to %.0f x %.0f x %.0f cm, long axis %s, deck Z=%.0f"),
					*SpawnLocation.ToCompactString(), RunwayScale,
					MeshSize.X, MeshSize.Y, MeshSize.Z,
					bRunwayAlongX ? TEXT("X") : TEXT("Y"), RunwayDeckZ);

				// Pivot offset relative to the footprint. Far from zero means the pivot is not
				// centred, which is exactly what threw the vehicle routes off the deck.
				UE_LOG(LogFPV, Log,
					TEXT("Runway raw bounds min %s max %s -- pivot offset from centre %s (scaled)"),
					*MeshBounds.Min.ToCompactString(), *MeshBounds.Max.ToCompactString(),
					*BoundsCentre.ToCompactString());

				// Where the deck actually spans in world space, for checking the routes against.
				UE_LOG(LogFPV, Log, TEXT("Runway deck spans X %.0f..%.0f, Y %.0f..%.0f"),
					RunwayCentre.X - MeshSize.X * 0.5f, RunwayCentre.X + MeshSize.X * 0.5f,
					RunwayCentre.Y - MeshSize.Y * 0.5f, RunwayCentre.Y + MeshSize.Y * 0.5f);
			}
		}
		else
		{
			UE_LOG(LogFPV, Warning, TEXT("Runway mesh not found -- vehicles will use a default route."));
		}

		// --- Terrain --------------------------------------------------------------------------
		// Run before any targets exist, so the ground traces can only land on the ground and the
		// runway. Scattering afterwards would perch rock on the roof of a van.
		GenerateTerrain(World, RunwayCentre, RunwayActor);

		// Along the runway, centred, leaving a margin at each end so they turn on the deck.
		const FVector RunwayAxis = bRunwayAlongX ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
		const FVector RunwayCross = bRunwayAlongX ? FVector(0.f, 1.f, 0.f) : FVector(1.f, 0.f, 0.f);
		const float HalfRun = RunwayLength * 0.42f;
		const float LaneOffset = FMath::Min(RunwayWidth * 0.18f, 600.f);

		auto SpawnTarget = [World](TSubclassOf<ADroneTarget> Class, const FVector& Location,
			const FRotator& Rotation, TFunctionRef<void(ADroneTarget*)> Configure) -> ADroneTarget*
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			// Deferred so properties are set before OnConstruction builds the body.
			ADroneTarget* Target = World->SpawnActorDeferred<ADroneTarget>(
				Class, FTransform(Rotation, Location), nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

			if (Target)
			{
				Configure(Target);
				Target->FinishSpawning(FTransform(Rotation, Location));
			}
			return Target;
		};

		// Everything below is positioned from the runway, not from the player. The scene needs a
		// single centre -- targets left near the spawn point are simply not in the same place as
		// the fight, and read as missing rather than distant.
		//
		// Structures sit off the deck edge rather than on it, so the vehicles have a clear run.
		SpawnTarget(ADroneTarget::StaticClass(), RunwayCentre + FVector(-16000.f, -11000.f, 0.f), FRotator::ZeroRotator,
			[](ADroneTarget* T) { T->Kind = ETargetKind::Structure; T->BodySize = FVector(900.f, 700.f, 500.f); T->MaxHealth = 140.f; });

		SpawnTarget(ADroneTarget::StaticClass(), RunwayCentre + FVector(13000.f, 12000.f, 0.f), FRotator(0.f, 35.f, 0.f),
			[](ADroneTarget* T) { T->Kind = ETargetKind::Structure; T->BodySize = FVector(1200.f, 800.f, 700.f); T->MaxHealth = 200.f; });

		// Gas line: thin and chains hard, so it rewards precision.
		SpawnTarget(ADroneTarget::StaticClass(), RunwayCentre + FVector(-4000.f, 10500.f, 0.f), FRotator(0.f, 90.f, 0.f),
			[](ADroneTarget* T)
			{
				T->Kind = ETargetKind::GasLine;
				T->BodySize = FVector(3000.f, 200.f, 240.f);
				T->MaxHealth = 45.f;
				T->bSecondaryExplosion = true;
				T->SecondaryBlastRadius = 2200.f;
				T->SecondaryBlastDamage = 220.f;
				T->ScoreValue = 300;
			});

		// Substation sits near the gas line, so a good pipeline hit should take it too.
		SpawnTarget(ADroneTarget::StaticClass(), RunwayCentre + FVector(1800.f, 12000.f, 0.f), FRotator::ZeroRotator,
			[](ADroneTarget* T) { T->Kind = ETargetKind::ElectricalStation; T->BodySize = FVector(800.f, 800.f, 600.f); T->MaxHealth = 110.f; T->ScoreValue = 350; });

		// Vehicles run the length of the runway in opposing lanes. Having one coming towards you
		// and one going away matters: a head-on pass and a stern chase are completely different
		// problems, and the runway should always be offering both.
		const float RunwayYaw = bRunwayAlongX ? 0.f : 90.f;

		struct FRunwayVehicle { float Lane; float Speed; bool bReversed; bool bPrimary; };
		static const FRunwayVehicle RunwayVehicles[] = {
			{  1.f, 1500.f, false, true  },
			{ -1.f, 1900.f, true,  false },
			{  0.f, 1250.f, false, false },
		};

		// Both van variants from the imported FBX, so the traffic is not three of the same thing.
		UStaticMesh* VanMeshes[2] = {
			LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Fab/Utility_Vans/Object016.Object016")),
			LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Fab/Utility_Vans/Object017.Object017"))
		};

		int32 VanIndex = 0;
		for (const FRunwayVehicle& Spec : RunwayVehicles)
		{
			const FVector Lane = RunwayCross * (LaneOffset * Spec.Lane);
			const FVector StartAlong = RunwayAxis * (Spec.bReversed ? HalfRun : -HalfRun);
			const FVector Start = RunwayCentre + StartAlong + Lane + FVector(0.f, 0.f, RunwayDeckZ);

			// Route points are actor-local, so this is a straight there-and-back along the deck.
			const float RunDistance = HalfRun * 2.f;
			const FVector Far = FVector(RunDistance, 0.f, 0.f);

			SpawnTarget(AVehicleTarget::StaticClass(), Start,
				FRotator(0.f, RunwayYaw + (Spec.bReversed ? 180.f : 0.f), 0.f),
				[&Spec, Far, &VanMeshes, VanIndex](ADroneTarget* T)
				{
					if (AVehicleTarget* V = Cast<AVehicleTarget>(T))
					{
						V->RoutePoints = { FVector::ZeroVector, Far };
						V->Speed = Spec.Speed;
						V->bPrimaryObjective = Spec.bPrimary;
						V->bLoopRoute = true;
						// Wide turns at the ends, so they sweep round instead of pivoting on the spot.
						V->TurnRateDegrees = 55.f;

						if (UStaticMesh* Chosen = VanMeshes[VanIndex % 2])
						{
							V->BodyMesh = Chosen;
						}
					}
				});

			++VanIndex;
		}

		// Infantry. Patrols along the deck edge and around the structures, so the place reads as
		// occupied rather than as a collection of props. Mostly guards; a couple are objectives.
		struct FPatrol { FVector Offset; float Yaw; float Length; bool bObjective; };
		static const FPatrol Patrols[] = {
			{ FVector(-18000.f,  5200.f, 0.f),    0.f, 6000.f, false },
			{ FVector(  6000.f, -5200.f, 0.f),  180.f, 7000.f, false },
			{ FVector( 22000.f,  4600.f, 0.f),   90.f, 3000.f, true  },
			{ FVector(-24000.f, -3800.f, 0.f),  -90.f, 2400.f, false },
			{ FVector( 14000.f,  2000.f, 0.f),   45.f, 4200.f, true  },
			{ FVector( -8000.f, -1500.f, 0.f),  135.f,    0.f, false },   // standing watch
		};

		// Infantry split between the two sides, so faction colours and attitude can be read at a
		// glance while the identification design is still being worked out.
		int32 PatrolIndex = 0;
		for (const FPatrol& Patrol : Patrols)
		{
			const FVector Location = RunwayCentre + Patrol.Offset + FVector(0.f, 0.f, RunwayDeckZ);
			const EFaction PatrolFaction = (PatrolIndex % 2 == 0) ? EFaction::Russia : EFaction::NATO;

			SpawnTarget(ASoldierTarget::StaticClass(), Location, FRotator(0.f, Patrol.Yaw, 0.f),
				[&Patrol, PatrolFaction](ADroneTarget* T)
				{
					if (ASoldierTarget* S = Cast<ASoldierTarget>(T))
					{
						S->Faction = PatrolFaction;
						S->SoldierRole = Patrol.bObjective ? ESoldierRole::Objective : ESoldierRole::Guard;

						// A zero-length beat means they hold position rather than walk.
						S->PatrolPoints = (Patrol.Length > 0.f)
							? TArray<FVector>{ FVector::ZeroVector, FVector(Patrol.Length, 0.f, 0.f) }
							: TArray<FVector>{ FVector::ZeroVector };
					}
				});

			++PatrolIndex;
		}

		// Banners at each end of the deck, marking whose ground is whose. Deliberately offset
		// from the operators -- a banner sitting on top of one would turn the hunt into flying
		// to the nearest flag.
		{
			FActorSpawnParameters BannerParams;
			BannerParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			struct FBannerSpawn { FVector Offset; float Yaw; EFaction Faction; };
			static const FBannerSpawn Banners[] = {
				{ FVector(-27000.f,  2500.f, 0.f),  90.f, EFaction::Russia },
				{ FVector( 27000.f, -2500.f, 0.f), -90.f, EFaction::NATO   },
			};

			for (const FBannerSpawn& Spawn : Banners)
			{
				const FVector Location = RunwayCentre + Spawn.Offset + FVector(0.f, 0.f, RunwayDeckZ);
				if (ABannerMarker* Banner = World->SpawnActorDeferred<ABannerMarker>(
					ABannerMarker::StaticClass(), FTransform(FRotator(0.f, Spawn.Yaw, 0.f), Location),
					nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
				{
					Banner->Faction = Spawn.Faction;
					Banner->FinishSpawning(FTransform(FRotator(0.f, Spawn.Yaw, 0.f), Location));
				}
			}
		}

		// One operator per side, at opposite ends of the deck and well apart, so finding one is
		// a search rather than a coincidence.
		SpawnTarget(AOperatorTarget::StaticClass(),
			RunwayCentre + FVector(-24000.f, -7000.f, RunwayDeckZ), FRotator(0.f, 45.f, 0.f),
			[](ADroneTarget* T) { T->Faction = EFaction::Russia; });

		SpawnTarget(AOperatorTarget::StaticClass(),
			RunwayCentre + FVector(23000.f, 7500.f, RunwayDeckZ), FRotator(0.f, 225.f, 0.f),
			[](ADroneTarget* T) { T->Faction = EFaction::NATO; });

		// A flight of loitering munitions, spread across heights and speeds so the sky is never
		// empty and there is always something to climb after.
		struct FUAVSpawn { FVector Offset; float Yaw; float Speed; bool bPrimary; };
		// Kept close in and low. Spread across half a kilometre they were technically present but
		// effectively absent -- at 160 cm across, a UAV a few hundred metres out is a couple of
		// pixels, so the sky read as empty.
		static const FUAVSpawn UAVs[] = {
			{ FVector(     0.f,      0.f, 1200.f),   0.f,  900.f, true  },
			{ FVector( -3500.f, -1600.f, 1750.f), 120.f, 1100.f, false },
			{ FVector(  3200.f, -2600.f, 1400.f), 210.f, 1000.f, false },
			{ FVector( -2000.f,  3000.f, 2150.f),  60.f,  800.f, true  },
			{ FVector(  4500.f,  2300.f, 1550.f), 300.f, 1200.f, false },
		};

		for (const FUAVSpawn& Spawn : UAVs)
		{
			// Over the runway, and at altitudes measured from the deck rather than world zero.
			SpawnTarget(AEnemyDroneTarget::StaticClass(),
				RunwayCentre + Spawn.Offset + FVector(0.f, 0.f, RunwayDeckZ), FRotator(0.f, Spawn.Yaw, 0.f),
				[&Spawn](ADroneTarget* T)
				{
					if (AEnemyDroneTarget* D = Cast<AEnemyDroneTarget>(T))
					{
						D->PatrolSpeed = Spawn.Speed;
						D->bPrimaryObjective = Spawn.bPrimary;

						// Shorter arcs and a tight leash, so they work the airfield rather than
						// crossing it and leaving.
						D->ArcLength = 5000.f;
						D->ArcHeight = 1000.f;
						D->LoiterRadius = 6000.f;
					}
				});
		}

		// The transport heli orbits the runway, so the scene has a centre instead of a target
		// scattered off in empty ground. Spawned well clear of the player start -- it is large,
		// and starting inside it would be indistinguishable from a broken game.
		// Altitude is measured from the deck, not from world zero -- the deck may be well above it.
		SpawnTarget(AHelicopterTarget::StaticClass(),
			RunwayCentre + FVector(0.f, 0.f, RunwayDeckZ + 2200.f), FRotator(0.f, 45.f, 0.f),
			[RunwayWidth, RunwayLength](ADroneTarget* T)
			{
				if (AHelicopterTarget* H = Cast<AHelicopterTarget>(T))
				{
					H->bOrbit = true;
					H->OrbitRadius = FMath::Max(RunwayWidth * 0.8f, 7000.f);
					H->OrbitAltitude = 2000.f;

					// The wander is scaled to the runway, so it works the length of the deck
					// over a couple of minutes rather than hovering above one spot.
					H->CentreDriftDistance = FMath::Max(RunwayLength * 0.28f, 10000.f);
					H->CentreDriftPeriod = 95.f;
				}
			});

		UE_LOG(LogFPV, Log, TEXT("Spawned test targets around %s"), *Origin.ToCompactString());
	}

	/** Parses a faction name, tolerating case and the obvious abbreviations. */
	bool ParseFaction(const FString& Text, EFaction& OutFaction)
	{
		const FString Lower = Text.ToLower();
		if (Lower == TEXT("russia") || Lower == TEXT("ru") || Lower == TEXT("red"))
		{
			OutFaction = EFaction::Russia;
			return true;
		}
		if (Lower == TEXT("nato") || Lower == TEXT("blue"))
		{
			OutFaction = EFaction::NATO;
			return true;
		}
		if (Lower == TEXT("neutral") || Lower == TEXT("none"))
		{
			OutFaction = EFaction::Neutral;
			return true;
		}
		return false;
	}

	FAutoConsoleCommandWithWorldAndArgs CmdSetPlayerFaction(
		TEXT("fpv.SetFaction"),
		TEXT("Set the operator's side: russia, nato or neutral."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			[](const TArray<FString>& Args, UWorld* World)
			{
				EFaction Faction;
				if (Args.Num() < 1 || !ParseFaction(Args[0], Faction))
				{
					UE_LOG(LogFPV, Warning, TEXT("Usage: fpv.SetFaction <russia|nato|neutral>"));
					return;
				}

				APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
				if (AFPVPlayerState* State = PC ? PC->GetPlayerState<AFPVPlayerState>() : nullptr)
				{
					State->SetFaction(Faction);
				}
			}));

	FAutoConsoleCommandWithWorld CmdReportFactions(
		TEXT("fpv.Factions"),
		TEXT("List every unit's faction and its attitude toward the operator."),
		FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
		{
			if (!World)
			{
				return;
			}

			const APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
			UE_LOG(LogFPV, Log, TEXT("Operator: %s"),
				FPVFaction::GetDisplayName(FPVFaction::GetFactionOf(Player)));

			int32 Friendly = 0, Hostile = 0, NeutralCount = 0;
			for (TActorIterator<ADroneTarget> It(World); It; ++It)
			{
				switch (FPVFaction::GetAttitude(Player, *It))
				{
				case EFactionAttitude::Friendly: ++Friendly; break;
				case EFactionAttitude::Hostile:  ++Hostile;  break;
				default:                         ++NeutralCount; break;
				}
			}

			UE_LOG(LogFPV, Log, TEXT("Units -- hostile %d, friendly %d, neutral %d"),
				Hostile, Friendly, NeutralCount);
		}));

	FAutoConsoleCommandWithWorld CmdSpawnTestTargets(
		TEXT("fpv.SpawnTestTargets"),
		TEXT("Drop a mixed set of targets around the player: structures, a gas line, a substation, two vehicles and two UAVs."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&SpawnTestTargets));
}

AFPVWarGameMode::AFPVWarGameMode()
{
	DefaultPawnClass = AFPVDronePawn::StaticClass();
	HUDClass = AFPVHUD::StaticClass();
	PlayerStateClass = AFPVPlayerState::StaticClass();

	// Ticks to drive the post-strike sequence.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AFPVWarGameMode::BeginPlay()
{
	Super::BeginPlay();

	AllTargets.Reset();
	for (TActorIterator<ADroneTarget> It(GetWorld()); It; ++It)
	{
		AllTargets.Add(*It);
	}

	DronesRemaining = StartingDrones;

	int32 PrimaryCount = 0;
	for (const ADroneTarget* Target : AllTargets)
	{
		if (Target && Target->bPrimaryObjective)
		{
			++PrimaryCount;
		}
	}

	if (AllTargets.Num() == 0)
	{
		UE_LOG(LogFPV, Warning,
			TEXT("No ADroneTarget actors in the level -- free flight only. Place targets to build a mission."));
	}
	else
	{
		UE_LOG(LogFPV, Log, TEXT("Sortie ready: %d targets (%d primary), %s drones."),
			AllTargets.Num(), PrimaryCount,
			DronesRemaining < 0 ? TEXT("unlimited") : *FString::FromInt(DronesRemaining));
	}
}

// ---------------------------------------------------------------------------------------------
// Blast
// ---------------------------------------------------------------------------------------------

void AFPVWarGameMode::ApplyBlast(const FVector& Origin, float Radius, float Damage, AActor* DamageCauser, AActor* IgnoreActor)
{
	if (Radius <= 0.f || Damage <= 0.f)
	{
		return;
	}

	const float RadiusSq = FMath::Square(Radius);

	// Snapshotted because a chain reaction can destroy targets while we iterate.
	TArray<TObjectPtr<ADroneTarget>> Snapshot = AllTargets;

	for (ADroneTarget* Target : Snapshot)
	{
		if (!Target || Target == IgnoreActor || Target->IsDestroyed())
		{
			continue;
		}

		const FVector TargetPoint = Target->GetAimPoint();
		const float DistanceSq = FVector::DistSquared(Origin, TargetPoint);

		// Measured to the body surface rather than its centre, so large structures are not
		// unfairly hard to damage just for being big.
		const float EffectiveDistance = FMath::Max(0.f, FMath::Sqrt(DistanceSq) - Target->GetApproximateRadius());
		if (EffectiveDistance > Radius)
		{
			continue;
		}

		// Linear falloff. Physically it should be steeper, but linear is far easier to read as
		// a player -- "closer is better" without a cliff edge.
		const float Falloff = 1.f - FMath::Clamp(EffectiveDistance / Radius, 0.f, 1.f);
		Target->ApplyBlastDamage(Damage * Falloff, Origin, DamageCauser);
	}

	// Shake the operator's camera. Felt well beyond the lethal radius on purpose -- explosions
	// you survive are most of what makes the ones you do not survive feel dangerous.
	if (AFPVDronePawn* Drone = Cast<AFPVDronePawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)))
	{
		const float Distance = FVector::Dist(Drone->GetActorLocation(), Origin);
		const float Trauma = FImpactShake::TraumaFromBlast(Distance, Radius, 1.8f);
		if (Trauma > 0.f)
		{
			Drone->AddImpactShake(Trauma);
		}
	}
}

void AFPVWarGameMode::RegisterTarget(ADroneTarget* Target)
{
	if (!Target)
	{
		return;
	}

	// AddUnique: BeginPlay ordering means a level-placed target can be swept up by the game
	// mode's own scan as well as registering itself.
	const int32 PreviousCount = AllTargets.Num();
	AllTargets.AddUnique(Target);

	if (AllTargets.Num() != PreviousCount)
	{
		// A target arriving after the mission was already cleared re-opens it.
		if (bMissionComplete && Target->bPrimaryObjective && !Target->IsDestroyed())
		{
			bMissionComplete = false;
		}
	}
}

void AFPVWarGameMode::NotifyTargetDestroyed(ADroneTarget* Target, AActor* /*Killer*/)
{
	if (!Target)
	{
		return;
	}

	Score += Target->ScoreValue;
	++TargetsDestroyed;

	if (bRecordingStrike)
	{
		FStrikeKill Kill;
		Kill.TargetName = Target->GetDisplayName();
		Kill.Score = Target->ScoreValue;
		Kill.bSecondary = bPrimaryBlastResolved;
		StrikeKills.Add(Kill);
		StrikeScore += Target->ScoreValue;
	}

	if (GetPrimaryTargetsRemaining() == 0 && !bMissionComplete)
	{
		bMissionComplete = true;
		UE_LOG(LogFPV, Log, TEXT("Mission complete. Score %d, %d targets destroyed."), Score, TargetsDestroyed);
	}
}

// ---------------------------------------------------------------------------------------------
// Drone supply
// ---------------------------------------------------------------------------------------------

void AFPVWarGameMode::NotifyDroneDetonated(AFPVDronePawn* Drone, const FVector& BlastLocation,
	const FVector& ApproachDirection, float BlastRadius, float BlastDamage)
{
	if (StrikeState != EStrikeState::Flying)
	{
		return;   // already mid-sequence; a second trigger in the same frame changes nothing
	}

	// Record everything this warhead is responsible for. Chain reactions resolve synchronously
	// inside ApplyBlast, so the whole causal chain lands inside this window.
	StrikeKills.Reset();
	StrikeScore = 0;
	bRecordingStrike = true;
	bPrimaryBlastResolved = false;

	ApplyBlast(BlastLocation, BlastRadius, BlastDamage, Drone);

	// Anything destroyed from here on is a knock-on effect rather than the warhead itself.
	bPrimaryBlastResolved = true;
	bRecordingStrike = false;

	if (DronesRemaining > 0)
	{
		--DronesRemaining;
	}

	// Brief slowdown at the moment of impact. Timed against real seconds, since dilated time
	// cannot be used to measure its own recovery.
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), ImpactTimeDilation);
	SlowMoEndRealTime = GetWorld()->GetRealTimeSeconds() + ImpactSlowMoDuration;
	bSlowMoActive = true;

	ActiveStrikeCamera = AStrikeCamera::Spawn(GetWorld(), BlastLocation, ApproachDirection, BlastRadius);

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (ActiveStrikeCamera)
		{
			// Short blend so the cut registers as a deliberate change of view rather than a
			// glitch, without making the player wait to see the blast.
			PC->SetViewTargetWithBlend(ActiveStrikeCamera, 0.25f);
		}
	}

	EnterStrikeState(EStrikeState::KillCam, KillCamDuration);

	UE_LOG(LogFPV, Log, TEXT("Strike: %d kill(s), %d points."), StrikeKills.Num(), StrikeScore);
}

void AFPVWarGameMode::EnterStrikeState(EStrikeState NewState, float Duration)
{
	StrikeState = NewState;
	PhaseEndTime = GetWorld()->GetTimeSeconds() + Duration;
}

bool AFPVWarGameMode::WantsToSkipStrikeSequence() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return false;
	}

	// Deliberately not SpaceBar, which the calibration wizard uses.
	return PC->WasInputKeyJustPressed(EKeys::R)
		|| PC->WasInputKeyJustPressed(EKeys::F)
		|| PC->WasInputKeyJustPressed(EKeys::Enter)
		|| PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
}

void AFPVWarGameMode::GetOperators(TArray<AOperatorTarget*>& OutOperators) const
{
	OutOperators.Reset();
	for (ADroneTarget* Target : AllTargets)
	{
		if (AOperatorTarget* Operator = Cast<AOperatorTarget>(Target))
		{
			OutOperators.Add(Operator);
		}
	}
}

float AFPVWarGameMode::GetHostileSignalStrengthAt(const FVector& SampleLocation, EFaction SensingFaction) const
{
	float Strongest = 0.f;

	for (ADroneTarget* Target : AllTargets)
	{
		const AOperatorTarget* Operator = Cast<AOperatorTarget>(Target);
		if (!Operator || Operator->IsDestroyed())
		{
			continue;
		}

		// Only hostile transmissions register. Your own side's traffic is not what you are
		// listening for, and Neutral never registers at all.
		if (FPVFaction::GetAttitude(SensingFaction, Operator->GetFaction()) != EFactionAttitude::Hostile)
		{
			continue;
		}

		Strongest = FMath::Max(Strongest, Operator->GetSignalStrengthAt(SampleLocation));
	}

	return Strongest;
}

void AFPVWarGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// An operator transmits while their side has a drone in the air. For the player that is the
	// pawn, whenever its warhead has not yet been spent -- flying is what gives you away.
	{
		const AFPVDronePawn* PlayerDrone = Cast<AFPVDronePawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
		const bool bPlayerAirborne = PlayerDrone && !PlayerDrone->IsWarheadSpent();
		const EFaction PlayerFaction = FPVFaction::GetFactionOf(PlayerDrone);

		TArray<AOperatorTarget*> Operators;
		GetOperators(Operators);

		for (AOperatorTarget* Operator : Operators)
		{
			if (Operator->IsDestroyed())
			{
				continue;
			}

			// The player's own operator transmits with the player's drone. Enemy operators are
			// assumed to be flying continuously until their own drone logic exists.
			const bool bIsPlayerSide = (Operator->GetFaction() == PlayerFaction) && PlayerFaction != EFaction::Neutral;
			Operator->SetTransmitting(bIsPlayerSide ? bPlayerAirborne : true);
		}
	}

	if (bSlowMoActive && GetWorld()->GetRealTimeSeconds() >= SlowMoEndRealTime)
	{
		bSlowMoActive = false;
		UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f);
	}

	if (StrikeState == EStrikeState::Flying)
	{
		return;
	}

	const bool bPhaseOver = GetWorld()->GetTimeSeconds() >= PhaseEndTime;
	const bool bSkip = WantsToSkipStrikeSequence();

	switch (StrikeState)
	{
	case EStrikeState::KillCam:
		if (bPhaseOver || bSkip)
		{
			EnterStrikeState(EStrikeState::Report, ReportDuration);
		}
		break;

	case EStrikeState::Report:
		if (bPhaseOver || bSkip)
		{
			// Out of drones ends the run rather than looping it.
			if (DronesRemaining == 0)
			{
				StrikeState = EStrikeState::Flying;
				UE_LOG(LogFPV, Log, TEXT("Out of drones. Score %d."), Score);
			}
			else
			{
				EnterStrikeState(EStrikeState::Respawning, 0.35f);
				FinishStrikeSequence();
			}
		}
		break;

	case EStrikeState::Respawning:
		if (bPhaseOver)
		{
			StrikeState = EStrikeState::Flying;
		}
		break;

	default:
		break;
	}
}

void AFPVWarGameMode::FinishStrikeSequence()
{
	RespawnDrone();

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(Pawn, 0.3f);
		}
	}

	if (ActiveStrikeCamera)
	{
		// Outlive the blend, or the view snaps as the camera is torn out from under it.
		ActiveStrikeCamera->SetLifeSpan(0.6f);
		ActiveStrikeCamera = nullptr;
	}
}

void AFPVWarGameMode::RespawnDrone()
{
	// The pawn is reused rather than respawned: it keeps the possession, camera and input
	// bindings intact, which matters because input setup is built in C++ at runtime.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (AFPVDronePawn* Drone = Cast<AFPVDronePawn>(PC->GetPawn()))
		{
			Drone->ResetToStart();
			Drone->RearmWarhead();
		}
	}
}

float AFPVWarGameMode::GetRespawnCountdown() const
{
	// The strike sequence owns the wait now, and it shows its own progress.
	return 0.f;
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

int32 AFPVWarGameMode::GetPrimaryTargetsRemaining() const
{
	int32 Count = 0;
	for (const ADroneTarget* Target : AllTargets)
	{
		if (Target && Target->bPrimaryObjective && !Target->IsDestroyed())
		{
			++Count;
		}
	}
	return Count;
}

ADroneTarget* AFPVWarGameMode::FindNearestLiveTarget(const FVector& FromLocation) const
{
	ADroneTarget* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (ADroneTarget* Target : AllTargets)
	{
		if (!Target || Target->IsDestroyed())
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(FromLocation, Target->GetAimPoint());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Target;
		}
	}

	return Nearest;
}

void AFPVWarGameMode::GetLiveTargets(TArray<ADroneTarget*>& OutTargets) const
{
	OutTargets.Reset();
	for (ADroneTarget* Target : AllTargets)
	{
		if (Target && !Target->IsDestroyed())
		{
			OutTargets.Add(Target);
		}
	}
}
