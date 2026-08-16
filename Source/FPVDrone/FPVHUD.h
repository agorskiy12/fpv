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
	void DrawRaceInfo();
	void DrawGateMarker();
	void DrawCrosshair();

	/** Debug overlay for bringing up an RC transmitter. Toggle with the fpv.ShowChannels console variable. */
	void DrawChannelMonitor();

	FRCChannelMonitor ChannelMonitor;
};
