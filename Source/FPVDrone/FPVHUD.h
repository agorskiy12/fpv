#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RCChannelMonitor.h"
#include "FPVHUD.generated.h"

/**
 * Canvas-drawn HUD -- throttle, speed, altitude, lap times, and a marker on the next gate.
 *
 * Deliberately drawn in C++ rather than UMG so the project has no asset dependencies and
 * runs the moment it compiles. Swap it for a UMG widget once you want real styling.
 */
UCLASS()
class FPVDRONE_API AFPVHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Accent colour used for the throttle bar and the gate marker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	FLinearColor AccentColor = FLinearColor(0.1f, 0.9f, 0.6f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	FLinearColor TextColor = FLinearColor(1.f, 1.f, 1.f, 0.9f);

private:
	void DrawThrottleBar(float Throttle);
	void DrawTelemetry(float SpeedKPH, float AltitudeM);
	void DrawCrosshair();

	/** Score, drones remaining, objectives left, mission banners. */
	void DrawMissionStatus();

	/** Brackets over live targets, with range and a blast-range cue. */
	void DrawTargetMarkers();

	/** Warhead state and the detonate prompt. */
	void DrawWarheadStatus(const class AFPVDronePawn* Drone);

	/** After-action report shown over the kill cam. Returns true when the sequence is running. */
	bool DrawStrikeReport();

	/** Debug overlay for bringing up an RC transmitter. Toggle with the fpv.ShowChannels console variable. */
	void DrawChannelMonitor();

	/** Connected-device picker. Toggle with fpv.ShowDevices; pick a slot with the number keys. */
	void DrawDeviceMenu();

	/** Step-by-step channel assignment prompts. Shown only while fpv.Calibrate is running. */
	void DrawCalibrationWizard();

	/** Number-key selection while the device menu is open. */
	void HandleDeviceMenuInput();

	/** Enumerate devices and auto-select once, on the first frame the HUD draws. */
	void EnsureDeviceRegistryInitialised();

	FRCChannelMonitor ChannelMonitor;

	bool bDeviceRegistryInitialised = false;
};
