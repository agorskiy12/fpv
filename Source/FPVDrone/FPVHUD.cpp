#include "FPVHUD.h"
#include "FPVDronePawn.h"
#include "FPVGameMode.h"
#include "RaceGate.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
	constexpr float BarWidth = 18.f;
	constexpr float BarLeftMargin = 44.f;
	const FLinearColor PanelColor(0.f, 0.f, 0.f, 0.35f);

	static TAutoConsoleVariable<int32> CVarShowChannels(
		TEXT("fpv.ShowChannels"),
		0,
		TEXT("Show the RC channel monitor overlay.\n")
		TEXT("  0 = off\n")
		TEXT("  1 = axes that have moved, plus axes 1-8\n")
		TEXT("  2 = all 24 axes\n")
		TEXT("Use fpv.ResetChannelRanges to clear the observed min/max."),
		ECVF_Default);
}

void AFPVHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	// Sampled unconditionally so the observed ranges keep accumulating while the overlay is
	// hidden, and drawn before the pawn check so it still works if possession failed.
	ChannelMonitor.Sample(GetOwningPlayerController());

	if (CVarShowChannels.GetValueOnGameThread() != 0)
	{
		DrawChannelMonitor();
	}

	const AFPVDronePawn* Drone = Cast<AFPVDronePawn>(GetOwningPawn());
	if (!Drone)
	{
		return;
	}

	DrawCrosshair();
	DrawThrottleBar(Drone->GetThrottle());
	DrawTelemetry(Drone->GetSpeedKPH(), Drone->GetAltitudeMeters());
	DrawRaceInfo();
	DrawGateMarker();
}

void AFPVHUD::DrawCrosshair()
{
	const float CX = Canvas->SizeX * 0.5f;
	const float CY = Canvas->SizeY * 0.5f;
	const float Arm = 9.f;
	const FLinearColor Faint(1.f, 1.f, 1.f, 0.45f);

	DrawLine(CX - Arm, CY, CX - 3.f, CY, Faint, 1.5f);
	DrawLine(CX + 3.f, CY, CX + Arm, CY, Faint, 1.5f);
	DrawLine(CX, CY - Arm, CX, CY - 3.f, Faint, 1.5f);
	DrawLine(CX, CY + 3.f, CX, CY + Arm, Faint, 1.5f);
}

void AFPVHUD::DrawThrottleBar(float Throttle)
{
	const float BarHeight = Canvas->SizeY * 0.34f;
	const float BarTop = Canvas->SizeY * 0.5f - BarHeight * 0.5f;

	// Track
	DrawRect(PanelColor, BarLeftMargin, BarTop, BarWidth, BarHeight);

	// Fill, growing upward from the bottom
	const float FillHeight = BarHeight * FMath::Clamp(Throttle, 0.f, 1.f);
	DrawRect(AccentColor, BarLeftMargin, BarTop + BarHeight - FillHeight, BarWidth, FillHeight);

	// Hover reference: with a thrust-to-weight of 3, you hover at about a third throttle.
	const float HoverY = BarTop + BarHeight * (1.f - 1.f / 3.f);
	DrawLine(BarLeftMargin - 5.f, HoverY, BarLeftMargin + BarWidth + 5.f, HoverY,
		FLinearColor(1.f, 1.f, 1.f, 0.5f), 1.f);

	DrawText(FString::Printf(TEXT("THR %3d%%"), FMath::RoundToInt(Throttle * 100.f)),
		TextColor, BarLeftMargin - 8.f, BarTop + BarHeight + 10.f, GEngine->GetSmallFont(), 1.f);
}

void AFPVHUD::DrawTelemetry(float SpeedKPH, float AltitudeM)
{
	const float X = BarLeftMargin - 4.f;
	const float Y = Canvas->SizeY - 96.f;

	DrawText(FString::Printf(TEXT("SPD  %5.1f km/h"), SpeedKPH),
		TextColor, X, Y, GEngine->GetMediumFont(), 1.f);
	DrawText(FString::Printf(TEXT("ALT  %5.1f m"), AltitudeM),
		TextColor, X, Y + 26.f, GEngine->GetMediumFont(), 1.f);
}

void AFPVHUD::DrawRaceInfo()
{
	const AFPVGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPVGameMode>() : nullptr;
	if (!GameMode || GameMode->GetTotalGates() == 0)
	{
		return;
	}

	const float X = Canvas->SizeX - 240.f;
	float Y = 40.f;

	auto Line = [this, X, &Y](const FString& Text, const FLinearColor& Color)
	{
		DrawText(Text, Color, X, Y, GEngine->GetMediumFont(), 1.f);
		Y += 26.f;
	};

	if (!GameMode->IsRaceStarted())
	{
		Line(TEXT("FLY THROUGH GATE 1 TO START"), AccentColor);
	}
	else
	{
		Line(FString::Printf(TEXT("LAP    %s"), *AFPVGameMode::FormatLapTime(GameMode->GetCurrentLapTime())), TextColor);
		Line(FString::Printf(TEXT("BEST   %s"), *AFPVGameMode::FormatLapTime(GameMode->GetBestLapTime())), AccentColor);
		Line(FString::Printf(TEXT("LAPS   %d"), GameMode->GetCompletedLaps()), TextColor);
	}

	Line(FString::Printf(TEXT("GATE   %d / %d"),
		GameMode->GetNextGateNumber(), GameMode->GetTotalGates()), TextColor);
}

void AFPVHUD::DrawGateMarker()
{
	const AFPVGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPVGameMode>() : nullptr;
	if (!GameMode)
	{
		return;
	}

	const ARaceGate* NextGate = GameMode->GetNextGate();
	const APawn* Drone = GetOwningPawn();
	if (!NextGate || !Drone)
	{
		return;
	}

	const FVector GateCenter = NextGate->GetGateCenter();
	const FVector Projected = Project(GateCenter);

	// Project returns Z <= 0 when the point is behind the camera.
	if (Projected.Z <= 0.f)
	{
		return;
	}

	const float DistanceM = FVector::Dist(Drone->GetActorLocation(), GateCenter) / 100.f;

	// Bracket shrinks as you close on the gate.
	const float Size = FMath::Clamp(4000.f / FMath::Max(DistanceM, 1.f), 14.f, 90.f);
	const float X = Projected.X;
	const float Y = Projected.Y;
	const float Arm = Size * 0.35f;

	auto Corner = [this](float CX, float CY, float DX, float DY, float Length)
	{
		DrawLine(CX, CY, CX + DX * Length, CY, AccentColor, 2.f);
		DrawLine(CX, CY, CX, CY + DY * Length, AccentColor, 2.f);
	};

	Corner(X - Size, Y - Size,  1.f,  1.f, Arm);
	Corner(X + Size, Y - Size, -1.f,  1.f, Arm);
	Corner(X - Size, Y + Size,  1.f, -1.f, Arm);
	Corner(X + Size, Y + Size, -1.f, -1.f, Arm);

	DrawText(FString::Printf(TEXT("%.0f m"), DistanceM),
		AccentColor, X - 18.f, Y + Size + 8.f, GEngine->GetSmallFont(), 1.f);
}

void AFPVHUD::DrawChannelMonitor()
{
	UFont* Font = GEngine->GetSmallFont();

	// Decide which axes to list. Mode 1 shows the first eight (which covers a typical radio's
	// main channels) plus anything above that has actually moved; mode 2 shows everything.
	const bool bShowAll = CVarShowChannels.GetValueOnGameThread() >= 2;

	TArray<int32, TInlineAllocator<FRCChannelMonitor::MaxAxes>> AxesToShow;
	for (int32 Index = 0; Index < FRCChannelMonitor::MaxAxes; ++Index)
	{
		// The GameInput named axes always show, because their being stuck at zero is itself the
		// answer to "does GameInput see this device?".
		if (bShowAll || Index < 8 || FRCChannelMonitor::IsNamedAxis(Index) || ChannelMonitor.HasMoved(Index))
		{
			AxesToShow.Add(Index);
		}
	}

	constexpr float PanelX = 34.f;
	constexpr float PanelY = 58.f;
	constexpr float PanelW = 448.f;
	constexpr float RowHeight = 19.f;
	constexpr float HeaderHeight = 46.f;
	constexpr float TrackX = PanelX + 62.f;
	constexpr float TrackW = 188.f;

	const float PanelH = HeaderHeight + AxesToShow.Num() * RowHeight + 30.f;

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.72f), PanelX, PanelY, PanelW, PanelH);

	DrawText(TEXT("RC CHANNEL MONITOR"), AccentColor, PanelX + 10.f, PanelY + 7.f, Font, 1.f);

	// Status line. This is what tells you whether the problem is the sim or the radio.
	if (!ChannelMonitor.HasSeenAnyInput())
	{
		DrawText(TEXT("no input yet - radio powered on, and USB mode set to Joystick?"),
			FLinearColor(1.f, 0.75f, 0.2f, 1.f), PanelX + 10.f, PanelY + 25.f, Font, 1.f);
	}
	else
	{
		const int32 Last = ChannelMonitor.GetLastMovedAxis();
		const FString Status = (Last != INDEX_NONE)
			? FString::Printf(TEXT("last moved: AXIS %d   (fpv.ResetChannelRanges to reset)"), Last + 1)
			: FString(TEXT("(fpv.ResetChannelRanges to reset)"));
		DrawText(Status, TextColor, PanelX + 10.f, PanelY + 25.f, Font, 1.f);
	}

	// Maps an axis value onto the track. Fixed to [-1, 1] rather than to the observed range, so
	// a 0..1 axis visibly sits in the right-hand half -- that alone tells you the signal format.
	auto ValueToX = [&](float Value) -> float
	{
		return TrackX + (FMath::Clamp(Value, -1.f, 1.f) + 1.f) * 0.5f * TrackW;
	};

	float RowY = PanelY + HeaderHeight;

	for (const int32 AxisIndex : AxesToShow)
	{
		const float Value = ChannelMonitor.GetValue(AxisIndex);
		const float MinValue = ChannelMonitor.GetMin(AxisIndex);
		const float MaxValue = ChannelMonitor.GetMax(AxisIndex);
		const bool bIsLastMoved = (AxisIndex == ChannelMonitor.GetLastMovedAxis());
		const bool bActive = ChannelMonitor.HasMoved(AxisIndex);

		const FLinearColor RowColor = bIsLastMoved
			? AccentColor
			: (bActive ? TextColor : FLinearColor(1.f, 1.f, 1.f, 0.35f));

		DrawText(ChannelMonitor.GetAxisLabel(AxisIndex),
			RowColor, PanelX + 10.f, RowY, Font, 1.f);

		// Track
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.10f), TrackX, RowY + 4.f, TrackW, 10.f);

		// Band covering everything this axis has been seen to do
		const float BandLeft = ValueToX(MinValue);
		const float BandRight = ValueToX(MaxValue);
		if (BandRight - BandLeft > 1.f)
		{
			DrawRect(FLinearColor(AccentColor.R, AccentColor.G, AccentColor.B, 0.22f),
				BandLeft, RowY + 4.f, BandRight - BandLeft, 10.f);
		}

		// Centre reference
		const float CenterX = ValueToX(0.f);
		DrawLine(CenterX, RowY + 2.f, CenterX, RowY + 16.f, FLinearColor(1.f, 1.f, 1.f, 0.30f), 1.f);

		// Current position
		DrawRect(RowColor, ValueToX(Value) - 1.5f, RowY + 1.f, 3.f, 16.f);

		DrawText(FString::Printf(TEXT("%+.3f"), Value),
			RowColor, TrackX + TrackW + 12.f, RowY, Font, 1.f);

		DrawText(FString::Printf(TEXT("[%+.2f %+.2f]"), MinValue, MaxValue),
			FLinearColor(1.f, 1.f, 1.f, 0.45f), TrackX + TrackW + 74.f, RowY, Font, 1.f);

		RowY += RowHeight;
	}

	// Buttons, on one line. Switches on most radios arrive as axes, but the Tango 2's face
	// buttons should land here.
	FString ButtonLine = TEXT("BTN ");
	for (int32 Index = 0; Index < FRCChannelMonitor::MaxButtons; ++Index)
	{
		ButtonLine += ChannelMonitor.IsButtonDown(Index)
			? FString::Printf(TEXT("%d "), Index + 1)
			: FString(TEXT("- "));
	}
	DrawText(ButtonLine, TextColor, PanelX + 10.f, RowY + 6.f, Font, 1.f);
}
