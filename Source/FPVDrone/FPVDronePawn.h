#pragma once

#include "CoreMinimal.h"
#include "FPVFactions.h"
#include "GameFramework/Pawn.h"
#include "ImpactShake.h"
#include "FPVDronePawn.generated.h"

class UStaticMeshComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * Acro-mode ("rate mode") FPV quadcopter.
 *
 * There is no self-levelling. The sticks command angular RATES, not angles, exactly like a
 * real quad in acro. Let go of the sticks and the craft holds whatever attitude it is in --
 * which is what makes FPV hard, and what makes it feel right.
 *
 * Control chain, once per tick:
 *   stick (-1..1) -> Betaflight "actual rates" curve -> desired body rate (deg/s)
 *                 -> PID against measured body rate -> angular acceleration
 *   throttle (0..1) -> thrust acceleration along the airframe's local up axis
 *   velocity -> quadratic aerodynamic drag (per body axis)
 *
 * Torque and thrust are applied as accelerations (bAccelChange = true), so the handling is
 * independent of the mesh's mass and inertia tensor. Swap the placeholder cube for a real
 * quad model and it will still fly identically.
 */
UCLASS()
class FPVDRONE_API AFPVDronePawn : public APawn, public IFactionMember
{
	GENERATED_BODY()

public:
	AFPVDronePawn();

	/**
	 * Read from the player state rather than stored here.
	 *
	 * The pawn is expendable by design -- it is destroyed on every strike -- so it cannot be
	 * where allegiance lives. Falls back to Neutral when unpossessed.
	 */
	virtual EFaction GetFaction() const override;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/** Airframe. Simulates physics; everything else is attached to it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UStaticMeshComponent> DroneMesh;

	/** Rigidly bolted to the airframe -- no spring arm, no control rotation. That rigidity IS the FPV look. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UCameraComponent> FPVCamera;

	/**
	 * Visual airframe, deliberately separate from the physics body.
	 *
	 * DroneMesh stays a simple box for collision and inertia; swapping the model must not change
	 * how the thing flies. You barely see your own airframe in FPV anyway -- this exists almost
	 * entirely for the kill cam, which is the one time the drone is actually on screen.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone|Visuals")
	TObjectPtr<UStaticMeshComponent> AirframeVisual;

	/** Model to display. When set, the placeholder box is hidden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Visuals")
	TObjectPtr<UStaticMesh> AirframeMesh;

	/** Longest dimension of the airframe in centimetres, used to normalise the imported scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Visuals")
	float AirframeLength = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Visuals")
	bool bAutoScaleAirframe = true;

	/** Correction if the model does not face along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Visuals")
	FRotator AirframeRotation = FRotator::ZeroRotator;

	// ---------------------------------------------------------------------------------------
	// Rates -- Betaflight "actual rates". Defaults are a typical 5" freestyle setup.
	// ---------------------------------------------------------------------------------------

	/** Rate at full stick deflection, deg/s. Roll / Pitch / Yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Rates")
	FVector MaxRates = FVector(800.f, 800.f, 600.f);

	/** Rate sensitivity around stick centre, deg/s. Lower = finer control near centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Rates")
	FVector CenterSensitivity = FVector(200.f, 200.f, 200.f);

	/** Stick expo, 0..1. Softens the middle of the throw without lowering the maximum rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Rates", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector RateExpo = FVector(0.54f, 0.54f, 0.54f);

	// ---------------------------------------------------------------------------------------
	// Rate controller (the "flight controller")
	// ---------------------------------------------------------------------------------------

	/** Proportional gain: rad/s^2 of angular acceleration per deg/s of rate error. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Controller")
	FVector PID_P = FVector(0.25f, 0.25f, 0.30f);

	/** Integral gain. Cleans up steady-state error from drag and asymmetry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Controller")
	FVector PID_I = FVector(0.05f, 0.05f, 0.05f);

	/** Derivative gain. Damps the response; too much makes it buzz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Controller")
	FVector PID_D = FVector(0.004f, 0.004f, 0.0f);

	/** Anti-windup clamp on the integral term. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Controller")
	float IntegralLimit = 300.f;

	/** Ceiling on commanded angular acceleration, rad/s^2. Stands in for finite motor authority. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Controller")
	float MaxAngularAccel = 400.f;

	// ---------------------------------------------------------------------------------------
	// Thrust and drag
	// ---------------------------------------------------------------------------------------

	/**
	 * Thrust-to-weight at full throttle.
	 * 2.0 is cinematic, 3.0 freestyle, 3.6 a light race build, 5.0+ is a rocket.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Thrust", meta = (ClampMin = "1.0"))
	float ThrustToWeightRatio = 3.6f;

	/** Motor spool-up time constant, seconds. Stops throttle from being instantaneous. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Thrust", meta = (ClampMin = "0.0"))
	float MotorResponseTime = 0.06f;

	/**
	 * Quadratic drag per body axis (X forward, Y right, Z up), in 1/m.
	 *
	 * Terminal velocity on an axis is roughly sqrt(9.8 / coefficient), so this is what sets top
	 * speed. Current values give about 130 km/h nose-on, 92 sideways and 76 climbing -- the
	 * higher lateral and vertical numbers model the quad's much larger broadside area.
	 *
	 * Lower the X value for more speed: 0.0075 is ~130 km/h, 0.006 is ~145, 0.004 is ~178.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Drag")
	FVector DragCoefficients = FVector(0.0075f, 0.015f, 0.022f);

	// ---------------------------------------------------------------------------------------
	// Airframe character
	//
	// This is not a tuned freestyle quad. It is a heavy airframe with a warhead strapped
	// underneath, flown on whatever tune it was given before someone sent it out. It wanders,
	// it overshoots, and it never sits still.
	// ---------------------------------------------------------------------------------------

	/**
	 * Master dial: 0 is a well-tuned racer, 1 is a heavy improvised bomber.
	 *
	 * Scales everything below together, because instability is not one effect. A badly tuned
	 * heavy quad is simultaneously noisier, slower to respond, worse at holding an attitude and
	 * unbalanced by its payload -- adjusting only one of those reads as a bug rather than as
	 * character.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Airframe", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirframeInstability = 0.35f;

	/**
	 * Peak random disturbance in rad/s^2 at full instability.
	 *
	 * Has to stay well under the control authority it is competing with. A small correction --
	 * PID_P of 0.25 against a 50 deg/s error -- produces roughly 12 rad/s^2, so a disturbance
	 * anywhere near that swamps fine input entirely and the aircraft stops answering the sticks.
	 * The first attempt at this used 60, which was five times the authority it was fighting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Airframe")
	float DisturbanceStrength = 13.f;

	/**
	 * How quickly the disturbance wanders, Hz.
	 *
	 * Deliberately low. Fast noise averages out inside the rate loop and is felt as vibration;
	 * slow noise pushes the aircraft off heading and has to be actively flown against, which is
	 * what makes it hard rather than merely rough.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Airframe")
	float DisturbanceFrequency = 1.8f;

	/**
	 * Constant bias from an off-centre warhead, rad/s^2 in pilot axes.
	 * It never trims out, so the aircraft cannot be flown hands-off for even a moment.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Airframe")
	FVector PayloadImbalance = FVector(0.f, -2.2f, 0.8f);

	/** Multiplier on disturbance at full throttle versus idle. Thrust is what shakes it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Airframe")
	float ThrottleShakeScale = 1.3f;

	/** Rate-loop gain retained at full instability. Lower means sloppier, later corrections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Airframe")
	float InstabilityGainScale = 0.85f;

	/** Motor lag multiplier at full instability. Heavy props spool slowly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Airframe")
	float InstabilityMotorLagScale = 1.4f;

	/** Continuous vibration in the video feed, degrees at full instability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Airframe")
	float CameraVibrationDegrees = 0.2f;

	// ---------------------------------------------------------------------------------------
	// Camera
	// ---------------------------------------------------------------------------------------

	/** Camera uptilt in degrees. More tilt = faster level flight, because you fly nose-down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Camera", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float CameraTiltDegrees = 25.f;

	/** FPV cameras are wide. 120 is about right for the genre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Camera", meta = (ClampMin = "60.0", ClampMax = "150.0"))
	float CameraFOV = 120.f;

	// ---------------------------------------------------------------------------------------
	// Sticks
	// ---------------------------------------------------------------------------------------

	/**
	 * Real FPV convention: pushing the pitch stick forward drops the nose. Uncheck if you
	 * would rather fly stick-forward-is-nose-up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Sticks")
	bool bInvertPitchStick = true;

	/**
	 * Leave the mouse cursor free instead of letting the viewport capture it.
	 *
	 * A drone sim never uses the mouse, and capturing it makes the standalone game awkward to
	 * alt-tab out of. It also makes it hard to tell whether the window actually has focus --
	 * which matters, because RawInput only delivers to the focused window.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Sticks")
	bool bReleaseMouseCursor = true;

	/**
	 * Take stick input from a calibrated RC transmitter when one is connected, falling back to
	 * keyboard and gamepad otherwise.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Sticks")
	bool bUseRCTransmitter = true;

	/** True while the flight model is being driven by the transmitter rather than the keyboard. */
	UFUNCTION(BlueprintPure, Category = "Drone|Telemetry")
	bool IsUsingRCTransmitter() const { return bRCInputActive; }

	// ---------------------------------------------------------------------------------------
	// Axis sign correction
	// ---------------------------------------------------------------------------------------

	/**
	 * UE's FRotator convention and its physics angular-velocity convention do not agree on
	 * every axis (pitch is the notorious one). These map pilot convention -- roll right,
	 * pitch up, yaw right all positive -- onto the physics frame.
	 *
	 * If an axis responds backwards on your first flight, flip the matching box. That is the
	 * intended fix; it is one checkbox, not a code change.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Axis Correction")
	bool bInvertRollAxis = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Axis Correction")
	bool bInvertPitchAxis = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Axis Correction")
	bool bInvertYawAxis = false;

	// ---------------------------------------------------------------------------------------
	// Telemetry, read by the HUD
	// ---------------------------------------------------------------------------------------

	/** Current throttle after motor lag, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Drone|Telemetry")
	float GetThrottle() const { return SmoothedThrottle; }

	/** Ground speed in km/h. */
	UFUNCTION(BlueprintPure, Category = "Drone|Telemetry")
	float GetSpeedKPH() const;

	/** Height above the spawn point, in metres. */
	UFUNCTION(BlueprintPure, Category = "Drone|Telemetry")
	float GetAltitudeMeters() const;

	/** Teleport back to the spawn transform with all motion and controller state zeroed. */
	UFUNCTION(BlueprintCallable, Category = "Drone")
	void ResetToStart();

	// ---------------------------------------------------------------------------------------
	// Warhead
	//
	// The drone is the munition. It detonates on a hard enough impact, or on command for an
	// airburst -- which is the tactic that makes a moving target catchable: you do not have to
	// physically connect, you only have to get close enough.
	// ---------------------------------------------------------------------------------------

	/** Blast radius in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Warhead")
	float BlastRadius = 1400.f;

	/** Damage at the centre of the blast, falling off linearly to zero at BlastRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Warhead")
	float BlastDamage = 300.f;

	/**
	 * Impact speed needed to set the warhead off, cm/s.
	 * Below this you have merely bumped into something and can keep flying.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Warhead")
	float ArmingImpactSpeed = 450.f;

	/** Warheads do not arm on the launch rail. Cleared once the drone is clear of the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Warhead")
	float ArmingAltitude = 150.f;

	UFUNCTION(BlueprintPure, Category = "Drone|Warhead")
	bool IsWarheadArmed() const { return bWarheadArmed && !bWarheadSpent; }

	UFUNCTION(BlueprintPure, Category = "Drone|Warhead")
	bool IsWarheadSpent() const { return bWarheadSpent; }

	/** Blow the warhead where the drone currently is. */
	UFUNCTION(BlueprintCallable, Category = "Drone|Warhead")
	void Detonate();

	/** Restore a live warhead. Called when a fresh drone is issued. */
	UFUNCTION(BlueprintCallable, Category = "Drone|Warhead")
	void RearmWarhead();

	/** Shake the FPV camera. Trauma is 0..1; blasts you survive should still be felt. */
	UFUNCTION(BlueprintCallable, Category = "Drone|Camera")
	void AddImpactShake(float Trauma);

	/** Peak camera rotation from a full-trauma shake, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Camera")
	float MaxShakeAngle = 4.5f;

	/** Peak camera displacement from a full-trauma shake, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Camera")
	float MaxShakeOffset = 6.f;

protected:
	// Input actions and mappings are built in C++ at runtime, so the project needs no
	// Input Action assets and no Blueprint wiring to be playable.
	UPROPERTY(Transient) TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RollAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> PitchAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> YawAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ResetAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> DetonateAction;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> InputMapping;

	void BuildInputAssets();
	void AddInputMapping();

	void OnThrottle(const FInputActionValue& Value);
	void OnRoll(const FInputActionValue& Value);
	void OnPitch(const FInputActionValue& Value);
	void OnYaw(const FInputActionValue& Value);
	void OnReset(const FInputActionValue& Value);
	void OnDetonate(const FInputActionValue& Value);

	UFUNCTION()
	void OnDroneHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit);

private:
	/** Raw stick positions, -1..1. Throttle is stored separately as 0..1. */
	float RawThrottle = -1.f;
	float StickRoll = 0.f;
	float StickPitch = 0.f;
	float StickYaw = 0.f;

	float SmoothedThrottle = 0.f;

	FVector RateIntegral = FVector::ZeroVector;
	FVector PreviousRateError = FVector::ZeroVector;

	FTransform SpawnTransform;

	bool bRCInputActive = false;

	bool bWarheadArmed = false;
	bool bWarheadSpent = false;

	FImpactShake CameraShake;
	FVector CameraBaseLocation = FVector::ZeroVector;
	FRotator CameraBaseRotation = FRotator::ZeroRotator;

	/** Advances the disturbance noise. Kept separate from world time so it survives dilation. */
	float DisturbanceTime = 0.f;

	/** Random disturbance plus payload bias, in pilot axes. */
	FVector ComputeAirframeDisturbance(float DeltaSeconds);

	void UpdateCameraShake(float DeltaSeconds);

	/** Overwrite the stick values from the calibrated transmitter, when one is available. */
	void ApplyRCTransmitterInput();

	/** Betaflight's applyActualRates: stick position -> commanded rate in deg/s. */
	static float ApplyActualRates(float Stick, float Center, float Max, float Expo);

	void UpdateFlight(float DeltaSeconds);
};
