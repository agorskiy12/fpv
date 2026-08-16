#include "FPVDronePawn.h"
#include "ExplosionEffect.h"
#include "FPVDrone.h"
#include "FPVWarGameMode.h"
#include "RCChannelMapping.h"
#include "RCDeviceRegistry.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

AFPVDronePawn::AFPVDronePawn()
{
	PrimaryActorTick.bCanEverTick = true;

	DroneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DroneMesh"));
	SetRootComponent(DroneMesh);

	// A scaled cube stands in for the airframe so the project flies with zero imported content.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		DroneMesh->SetStaticMesh(CubeMesh.Object);
	}

	// 25 x 25 x 8 cm -- roughly a 5" freestyle quad.
	DroneMesh->SetRelativeScale3D(FVector(0.25f, 0.25f, 0.08f));
	DroneMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	DroneMesh->SetSimulatePhysics(true);
	DroneMesh->SetEnableGravity(true);
	DroneMesh->SetNotifyRigidBodyCollision(true);
	DroneMesh->SetLinearDamping(0.f);   // drag is modelled explicitly in UpdateFlight
	DroneMesh->SetAngularDamping(0.15f);
	DroneMesh->SetMassOverrideInKg(NAME_None, 0.65f, true);

	FPVCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FPVCamera"));
	FPVCamera->SetupAttachment(DroneMesh);
	FPVCamera->bUsePawnControlRotation = false;   // rigidly mounted -- this is the whole point

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void AFPVDronePawn::BeginPlay()
{
	Super::BeginPlay();

	SpawnTransform = GetActorTransform();

	DroneMesh->OnComponentHit.AddDynamic(this, &AFPVDronePawn::OnDroneHit);

	// The mesh is scaled, so place the camera in world-space centimetres rather than
	// letting the parent's 0.25/0.08 scale squash the offset.
	CameraBaseLocation = FVector(10.f, 0.f, 4.f) / DroneMesh->GetRelativeScale3D();
	CameraBaseRotation = FRotator(CameraTiltDegrees, 0.f, 0.f);

	FPVCamera->SetRelativeLocation(CameraBaseLocation);
	FPVCamera->SetRelativeRotation(CameraBaseRotation);
	FPVCamera->SetFieldOfView(CameraFOV);

	BuildInputAssets();
	AddInputMapping();
}

// Possession and BeginPlay can land in either order depending on how the pawn is spawned,
// so this runs from both. AddMappingContext is safe to call twice.
void AFPVDronePawn::AddInputMapping()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !InputMapping)
	{
		return;
	}

	if (bReleaseMouseCursor)
	{
		// Keep the cursor usable so the standalone game can be alt-tabbed, and so it is obvious
		// whether the window has focus -- RawInput only delivers to the focused window.
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(InputMapping, 0);
	}
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

void AFPVDronePawn::BuildInputAssets()
{
	if (InputMapping)
	{
		return;   // already built
	}

	auto MakeAxisAction = [this](const TCHAR* Name) -> UInputAction*
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = EInputActionValueType::Axis1D;
		return Action;
	};

	ThrottleAction = MakeAxisAction(TEXT("IA_Throttle"));
	RollAction     = MakeAxisAction(TEXT("IA_Roll"));
	PitchAction    = MakeAxisAction(TEXT("IA_Pitch"));
	YawAction      = MakeAxisAction(TEXT("IA_Yaw"));

	ResetAction = NewObject<UInputAction>(this, TEXT("IA_Reset"));
	ResetAction->ValueType = EInputActionValueType::Boolean;

	DetonateAction = NewObject<UInputAction>(this, TEXT("IA_Detonate"));
	DetonateAction->ValueType = EInputActionValueType::Boolean;

	InputMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Drone"));

	// Gamepad sticks come in as -1..1 already. A small dead zone keeps a worn stick from drifting.
	auto MapStick = [this](UInputAction* Action, const FKey& Key)
	{
		FEnhancedActionKeyMapping& Mapping = InputMapping->MapKey(Action, Key);
		UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(this);
		DeadZone->LowerThreshold = 0.08f;
		Mapping.Modifiers.Add(DeadZone);
	};

	// Keyboard keys are digital, so the negative direction needs an explicit Negate.
	auto MapKeyDirection = [this](UInputAction* Action, const FKey& Key, bool bNegate)
	{
		FEnhancedActionKeyMapping& Mapping = InputMapping->MapKey(Action, Key);
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(this));
		}
	};

	// Mode 2, the standard FPV layout:
	//   left stick  -- throttle (Y), yaw (X)
	//   right stick -- pitch (Y), roll (X)
	MapStick(ThrottleAction, EKeys::Gamepad_LeftY);
	MapStick(YawAction,      EKeys::Gamepad_LeftX);
	MapStick(PitchAction,    EKeys::Gamepad_RightY);
	MapStick(RollAction,     EKeys::Gamepad_RightX);

	// Keyboard fallback. Flyable, but a gamepad or a real TX in joystick mode is far better.
	MapKeyDirection(ThrottleAction, EKeys::W, false);
	MapKeyDirection(ThrottleAction, EKeys::S, true);
	MapKeyDirection(YawAction,      EKeys::D, false);
	MapKeyDirection(YawAction,      EKeys::A, true);
	MapKeyDirection(PitchAction,    EKeys::Up, false);
	MapKeyDirection(PitchAction,    EKeys::Down, true);
	MapKeyDirection(RollAction,     EKeys::Right, false);
	MapKeyDirection(RollAction,     EKeys::Left, true);

	InputMapping->MapKey(ResetAction, EKeys::R);
	InputMapping->MapKey(ResetAction, EKeys::Gamepad_FaceButton_Top);

	// Manual airburst. Deliberately not SpaceBar, which the calibration wizard uses to confirm.
	InputMapping->MapKey(DetonateAction, EKeys::F);
	InputMapping->MapKey(DetonateAction, EKeys::LeftMouseButton);
	InputMapping->MapKey(DetonateAction, EKeys::Gamepad_FaceButton_Right);
}

void AFPVDronePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	BuildInputAssets();
	AddInputMapping();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogFPV, Error,
			TEXT("Input component is not a UEnhancedInputComponent. Set DefaultInputComponentClass ")
			TEXT("to /Script/EnhancedInput.EnhancedInputComponent in Config/DefaultEngine.ini."));
		return;
	}

	// Triggered fires every frame the axis is non-neutral; Completed fires once as it returns
	// to neutral, which is what zeroes the stick when you let go.
	EIC->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &AFPVDronePawn::OnThrottle);
	EIC->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &AFPVDronePawn::OnThrottle);
	EIC->BindAction(RollAction,     ETriggerEvent::Triggered, this, &AFPVDronePawn::OnRoll);
	EIC->BindAction(RollAction,     ETriggerEvent::Completed, this, &AFPVDronePawn::OnRoll);
	EIC->BindAction(PitchAction,    ETriggerEvent::Triggered, this, &AFPVDronePawn::OnPitch);
	EIC->BindAction(PitchAction,    ETriggerEvent::Completed, this, &AFPVDronePawn::OnPitch);
	EIC->BindAction(YawAction,      ETriggerEvent::Triggered, this, &AFPVDronePawn::OnYaw);
	EIC->BindAction(YawAction,      ETriggerEvent::Completed, this, &AFPVDronePawn::OnYaw);
	EIC->BindAction(ResetAction,    ETriggerEvent::Started,   this, &AFPVDronePawn::OnReset);
	EIC->BindAction(DetonateAction, ETriggerEvent::Started,   this, &AFPVDronePawn::OnDetonate);
}

void AFPVDronePawn::OnThrottle(const FInputActionValue& Value) { RawThrottle = Value.Get<float>(); }
void AFPVDronePawn::OnRoll(const FInputActionValue& Value)     { StickRoll  = Value.Get<float>(); }
void AFPVDronePawn::OnYaw(const FInputActionValue& Value)      { StickYaw   = Value.Get<float>(); }

void AFPVDronePawn::OnPitch(const FInputActionValue& Value)
{
	// Real FPV: stick forward = nose down. Flip bInvertPitchStick if you prefer it the other way.
	StickPitch = bInvertPitchStick ? -Value.Get<float>() : Value.Get<float>();
}

void AFPVDronePawn::OnReset(const FInputActionValue& /*Value*/)
{
	ResetToStart();
	RearmWarhead();
}

void AFPVDronePawn::OnDetonate(const FInputActionValue& /*Value*/)
{
	Detonate();
}

// ---------------------------------------------------------------------------------------------
// Warhead
// ---------------------------------------------------------------------------------------------

void AFPVDronePawn::OnDroneHit(
	UPrimitiveComponent* /*HitComponent*/,
	AActor* /*OtherActor*/,
	UPrimitiveComponent* /*OtherComp*/,
	FVector /*NormalImpulse*/,
	const FHitResult& /*Hit*/)
{
	if (!IsWarheadArmed())
	{
		return;
	}

	// A gentle scrape along a wall is survivable; a real impact is not. Judged on the speed
	// carried into the hit rather than the impulse, which varies with what was struck.
	const float ImpactSpeed = DroneMesh ? DroneMesh->GetPhysicsLinearVelocity().Size() : 0.f;
	if (ImpactSpeed >= ArmingImpactSpeed)
	{
		Detonate();
	}
}

void AFPVDronePawn::Detonate()
{
	if (bWarheadSpent || !bWarheadArmed)
	{
		return;
	}

	bWarheadSpent = true;
	bWarheadArmed = false;

	const FVector BlastOrigin = GetActorLocation();

	AExplosionEffect::Spawn(GetWorld(), BlastOrigin, BlastRadius, 1.2f);

	// Heading at impact, so the kill cam can set up side-on to the run rather than behind it.
	const FVector Approach = DroneMesh ? DroneMesh->GetPhysicsLinearVelocity().GetSafeNormal()
									   : GetActorForwardVector();

	if (AFPVWarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPVWarGameMode>() : nullptr)
	{
		// The game mode applies the blast itself so it can attribute kills to this strike.
		GameMode->NotifyDroneDetonated(this, BlastOrigin, Approach, BlastRadius, BlastDamage);
	}

	UE_LOG(LogFPV, Log, TEXT("Warhead detonated at %s"), *BlastOrigin.ToCompactString());

	// Drop out of the sky rather than vanish, so the detonation reads before the reset.
	if (DroneMesh)
	{
		DroneMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		DroneMesh->SetPhysicsAngularVelocityInDegrees(FVector(0.f, 0.f, 720.f));
	}
}

void AFPVDronePawn::RearmWarhead()
{
	bWarheadSpent = false;
	bWarheadArmed = false;   // re-arms once clear of the ground, see UpdateFlight
}

// ---------------------------------------------------------------------------------------------
// Flight
// ---------------------------------------------------------------------------------------------

float AFPVDronePawn::ApplyActualRates(float Stick, float Center, float Max, float Expo)
{
	// Betaflight's applyActualRates, ported directly. Centre sensitivity sets the slope around
	// neutral; expo bends the curve between there and the maximum rate at full deflection.
	Stick = FMath::Clamp(Stick, -1.f, 1.f);
	Expo = FMath::Clamp(Expo, 0.f, 1.f);

	const float StickAbs = FMath::Abs(Stick);
	const float ExpoF = StickAbs * (FMath::Pow(Stick, 5.f) * Expo + Stick * (1.f - Expo));
	const float StickMovement = FMath::Max(0.f, Max - Center);

	return Stick * Center + StickMovement * ExpoF;
}

void AFPVDronePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateFlight(DeltaSeconds);
	UpdateCameraShake(DeltaSeconds);
}

void AFPVDronePawn::AddImpactShake(float Trauma)
{
	CameraShake.Add(Trauma);
}

void AFPVDronePawn::UpdateCameraShake(float DeltaSeconds)
{
	if (!FPVCamera)
	{
		return;
	}

	const bool bWasActive = CameraShake.IsActive();
	CameraShake.Update(DeltaSeconds);

	if (!CameraShake.IsActive())
	{
		// Snap back to the mount exactly once, rather than every frame forever.
		if (bWasActive)
		{
			FPVCamera->SetRelativeLocation(CameraBaseLocation);
			FPVCamera->SetRelativeRotation(CameraBaseRotation);
		}
		return;
	}

	FPVCamera->SetRelativeLocation(CameraBaseLocation + CameraShake.GetLocationOffset(MaxShakeOffset));
	FPVCamera->SetRelativeRotation(CameraBaseRotation + CameraShake.GetRotationOffset(MaxShakeAngle));
}

void AFPVDronePawn::ApplyRCTransmitterInput()
{
	bRCInputActive = false;

	if (!bUseRCTransmitter)
	{
		return;
	}

	const FRCChannelMapping& Mapping = FRCChannelMapping::Get();
	if (!Mapping.IsConfigured() || !FRCDeviceRegistry::Get().HasParsedReport())
	{
		return;   // no radio: keyboard and gamepad continue to work untouched
	}

	bRCInputActive = true;

	StickRoll = Mapping.GetChannelValue(ERCChannel::Roll);
	StickYaw = Mapping.GetChannelValue(ERCChannel::Yaw);

	const float PitchValue = Mapping.GetChannelValue(ERCChannel::Pitch);
	StickPitch = bInvertPitchStick ? -PitchValue : PitchValue;

	// The rest of the flight model expects a -1..1 throttle stick, which it re-centres to
	// 0..1. The transmitter channel is already 0..1, so convert back rather than special-case it.
	RawThrottle = Mapping.GetChannelValue(ERCChannel::Throttle) * 2.f - 1.f;
}

void AFPVDronePawn::UpdateFlight(float DeltaSeconds)
{
	if (DeltaSeconds <= KINDA_SMALL_NUMBER || !DroneMesh || !DroneMesh->IsSimulatingPhysics())
	{
		return;
	}

	ApplyRCTransmitterInput();

	// Arm once clear of the launch point, mirroring a real safe-separation distance. Without
	// this, spawning on the ground can set the warhead off against the floor immediately.
	if (!bWarheadSpent && !bWarheadArmed && GetAltitudeMeters() * 100.f > ArmingAltitude)
	{
		bWarheadArmed = true;
		UE_LOG(LogFPV, Verbose, TEXT("Warhead armed."));
	}

	// A spent drone is falling debris -- no thrust, no control.
	if (bWarheadSpent)
	{
		return;
	}

	const FTransform BodyTransform = GetActorTransform();

	// Pilot convention -> physics convention. See bInvert* in the header for why this exists.
	const FVector AxisSign(
		bInvertRollAxis  ? -1.f : 1.f,
		bInvertPitchAxis ? -1.f : 1.f,
		bInvertYawAxis   ? -1.f : 1.f);

	// --- Rate command -------------------------------------------------------------------
	const FVector DesiredRates(
		ApplyActualRates(StickRoll,  CenterSensitivity.X, MaxRates.X, RateExpo.X),
		ApplyActualRates(StickPitch, CenterSensitivity.Y, MaxRates.Y, RateExpo.Y),
		ApplyActualRates(StickYaw,   CenterSensitivity.Z, MaxRates.Z, RateExpo.Z));

	// --- Measured rate, brought into the same convention --------------------------------
	const FVector WorldAngVel = DroneMesh->GetPhysicsAngularVelocityInDegrees();
	const FVector BodyAngVel = BodyTransform.InverseTransformVectorNoScale(WorldAngVel);
	const FVector MeasuredRates = BodyAngVel * AxisSign;

	// --- PID ----------------------------------------------------------------------------
	const FVector Error = DesiredRates - MeasuredRates;

	RateIntegral += Error * DeltaSeconds;
	RateIntegral = RateIntegral.BoundToCube(IntegralLimit);

	const FVector Derivative = (Error - PreviousRateError) / DeltaSeconds;
	PreviousRateError = Error;

	FVector AngularAccel(
		Error.X * PID_P.X + RateIntegral.X * PID_I.X + Derivative.X * PID_D.X,
		Error.Y * PID_P.Y + RateIntegral.Y * PID_I.Y + Derivative.Y * PID_D.Y,
		Error.Z * PID_P.Z + RateIntegral.Z * PID_I.Z + Derivative.Z * PID_D.Z);

	AngularAccel = AngularAccel.BoundToCube(MaxAngularAccel);

	// Back to the physics convention, then into world space.
	const FVector WorldAngularAccel = BodyTransform.TransformVectorNoScale(AngularAccel * AxisSign);
	DroneMesh->AddTorqueInRadians(WorldAngularAccel, NAME_None, /*bAccelChange=*/true);

	// --- Thrust -------------------------------------------------------------------------
	// The stick reads -1..1; a real transmitter's throttle sits at -1 at the bottom of its throw.
	const float TargetThrottle = FMath::Clamp((RawThrottle + 1.f) * 0.5f, 0.f, 1.f);

	const float Alpha = (MotorResponseTime > KINDA_SMALL_NUMBER)
		? 1.f - FMath::Exp(-DeltaSeconds / MotorResponseTime)
		: 1.f;
	SmoothedThrottle = FMath::Lerp(SmoothedThrottle, TargetThrottle, Alpha);

	const float GravityMagnitude = FMath::Abs(GetWorld()->GetGravityZ());   // cm/s^2
	const float ThrustAccel = SmoothedThrottle * ThrustToWeightRatio * GravityMagnitude;

	DroneMesh->AddForce(GetActorUpVector() * ThrustAccel, NAME_None, /*bAccelChange=*/true);

	// --- Aerodynamic drag ---------------------------------------------------------------
	const FVector WorldVelocity = DroneMesh->GetPhysicsLinearVelocity();
	const FVector BodyVelocityMS = BodyTransform.InverseTransformVectorNoScale(WorldVelocity) / 100.f;
	const float SpeedMS = BodyVelocityMS.Size();

	if (SpeedMS > KINDA_SMALL_NUMBER)
	{
		// Quadratic drag per body axis, in m/s^2, converted back to cm/s^2.
		const FVector DragAccelBody = FVector(
			-DragCoefficients.X * BodyVelocityMS.X * SpeedMS,
			-DragCoefficients.Y * BodyVelocityMS.Y * SpeedMS,
			-DragCoefficients.Z * BodyVelocityMS.Z * SpeedMS) * 100.f;

		const FVector DragAccelWorld = BodyTransform.TransformVectorNoScale(DragAccelBody);
		DroneMesh->AddForce(DragAccelWorld, NAME_None, /*bAccelChange=*/true);
	}
}

// ---------------------------------------------------------------------------------------------
// Telemetry and reset
// ---------------------------------------------------------------------------------------------

float AFPVDronePawn::GetSpeedKPH() const
{
	return DroneMesh ? DroneMesh->GetPhysicsLinearVelocity().Size() * 0.036f : 0.f;   // cm/s -> km/h
}

float AFPVDronePawn::GetAltitudeMeters() const
{
	return (GetActorLocation().Z - SpawnTransform.GetLocation().Z) / 100.f;
}

void AFPVDronePawn::ResetToStart()
{
	if (!DroneMesh)
	{
		return;
	}

	DroneMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	DroneMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);

	RateIntegral = FVector::ZeroVector;
	PreviousRateError = FVector::ZeroVector;
	SmoothedThrottle = 0.f;
	RawThrottle = -1.f;

	bWarheadSpent = false;
	bWarheadArmed = false;

	DroneMesh->WakeRigidBody();
}
