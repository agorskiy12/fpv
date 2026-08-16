#include "FPVHUD.h"
#include "DroneTarget.h"
#include "FPVDrone.h"
#include "FPVDronePawn.h"
#include "FPVWarGameMode.h"
#include "RCChannelMapping.h"
#include "RCDeviceRegistry.h"

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

	static TAutoConsoleVariable<int32> CVarShowDevices(
		TEXT("fpv.ShowDevices"),
		0,
		TEXT("Show the connected input device picker.\n")
		TEXT("  0 = off\n")
		TEXT("  1 = on -- press 1-9 to select a device\n")
		TEXT("Use fpv.RefreshDevices after plugging or unplugging hardware."),
		ECVF_Default);

	FAutoConsoleCommand CmdRefreshDevices(
		TEXT("fpv.RefreshDevices"),
		TEXT("Re-enumerate connected HID devices and re-apply the selection."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			FRCDeviceRegistry::Get().Refresh();
			UE_LOG(LogFPV, Log, TEXT("Device list refreshed: %d device(s), %d flyable."),
				FRCDeviceRegistry::Get().GetDevices().Num(),
				FRCDeviceRegistry::Get().GetGameDeviceIndices().Num());
		}));

	FAutoConsoleCommand CmdCalibrate(
		TEXT("fpv.Calibrate"),
		TEXT("Start the RC channel calibration wizard. Move each stick when prompted, press SPACE to confirm."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			FRCChannelMapping::Get().BeginCalibration();
		}));

	FAutoConsoleCommand CmdCalibrateCancel(
		TEXT("fpv.CalibrateCancel"),
		TEXT("Abort the calibration wizard, leaving the existing mapping alone."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			FRCChannelMapping::Get().CancelCalibration();
		}));

	FAutoConsoleCommand CmdTangoDefaults(
		TEXT("fpv.Tango2Defaults"),
		TEXT("Apply the stock TBS Tango 2 channel order: throttle 8, yaw 5, roll 7, pitch 6."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			FRCChannelMapping::Get().ApplyTango2Defaults();
			FRCChannelMapping::Get().SaveToConfig();
		}));

	FAutoConsoleCommand CmdSetChannel(
		TEXT("fpv.SetChannel"),
		TEXT("Assign a channel to an axis, e.g. 'fpv.SetChannel throttle 8'. Channels: throttle roll pitch yaw."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogFPV, Warning, TEXT("Usage: fpv.SetChannel <throttle|roll|pitch|yaw> <axis>"));
				return;
			}

			const FString Name = Args[0].ToLower();
			ERCChannel Channel;
			if (Name == TEXT("throttle"))   { Channel = ERCChannel::Throttle; }
			else if (Name == TEXT("roll"))  { Channel = ERCChannel::Roll; }
			else if (Name == TEXT("pitch")) { Channel = ERCChannel::Pitch; }
			else if (Name == TEXT("yaw"))   { Channel = ERCChannel::Yaw; }
			else
			{
				UE_LOG(LogFPV, Warning, TEXT("Unknown channel '%s'."), *Args[0]);
				return;
			}

			FRCChannelMapping::Get().SetAxis(Channel, FCString::Atoi(*Args[1]) - 1);
			FRCChannelMapping::Get().SaveToConfig();
		}));

	FAutoConsoleCommand CmdInvertChannel(
		TEXT("fpv.InvertChannel"),
		TEXT("Invert a channel, e.g. 'fpv.InvertChannel pitch 1'."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogFPV, Warning, TEXT("Usage: fpv.InvertChannel <throttle|roll|pitch|yaw> <0|1>"));
				return;
			}

			const FString Name = Args[0].ToLower();
			ERCChannel Channel;
			if (Name == TEXT("throttle"))   { Channel = ERCChannel::Throttle; }
			else if (Name == TEXT("roll"))  { Channel = ERCChannel::Roll; }
			else if (Name == TEXT("pitch")) { Channel = ERCChannel::Pitch; }
			else if (Name == TEXT("yaw"))   { Channel = ERCChannel::Yaw; }
			else { return; }

			FRCChannelMapping::Get().SetInverted(Channel, FCString::Atoi(*Args[1]) != 0);
			FRCChannelMapping::Get().SaveToConfig();
		}));

	FAutoConsoleCommand CmdReRegisterDevice(
		TEXT("fpv.ReRegisterDevice"),
		TEXT("Re-register the selected device with RawInput, binding it to the window that is "
			 "active right now. Use this if input stops arriving after alt-tabbing."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			const bool bOK = FRCDeviceRegistry::Get().ForceReRegister();
			UE_LOG(LogFPV, Log, TEXT("Re-registration %s: %s"),
				bOK ? TEXT("succeeded") : TEXT("failed"),
				*FRCDeviceRegistry::Get().GetStatusMessage());
		}));

	FAutoConsoleCommand CmdSelectDevice(
		TEXT("fpv.SelectDevice"),
		TEXT("Select a flyable device by menu slot number, e.g. 'fpv.SelectDevice 1'."),
		FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogFPV, Warning, TEXT("Usage: fpv.SelectDevice <slot>"));
				return;
			}
			const int32 Slot = FCString::Atoi(*Args[0]) - 1;   // menu slots are 1-based
			if (!FRCDeviceRegistry::Get().SelectGameDevice(Slot))
			{
				UE_LOG(LogFPV, Warning, TEXT("No device in slot %s."), *Args[0]);
			}
		}));
}

void AFPVHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	// Deferred to the first draw: RawInput's device is created during Slate startup, so
	// registering a usage any earlier than this would silently fail.
	EnsureDeviceRegistryInitialised();

	// Retried every frame: RawInput's device is created after the first HUD draw, so a single
	// attempt at startup always loses that race.
	FRCDeviceRegistry::Get().TickRegistration(ChannelMonitor.HasSeenAnyInput());

	// Sampled unconditionally so the observed ranges keep accumulating while the overlay is
	// hidden, and drawn before the pawn check so it still works if possession failed.
	ChannelMonitor.Sample(GetOwningPlayerController());

	if (CVarShowDevices.GetValueOnGameThread() != 0)
	{
		HandleDeviceMenuInput();
		DrawDeviceMenu();
	}

	FRCChannelMapping& Mapping = FRCChannelMapping::Get();
	if (Mapping.IsCalibrating())
	{
		Mapping.TickCalibration(GetOwningPlayerController());
		DrawCalibrationWizard();
	}

	if (CVarShowChannels.GetValueOnGameThread() != 0)
	{
		DrawChannelMonitor();
	}

	// During the strike sequence the flight HUD is meaningless -- the drone is gone and the view
	// is a third-person camera -- so it is replaced entirely rather than drawn over.
	if (DrawStrikeReport())
	{
		return;
	}

	const AFPVDronePawn* Drone = Cast<AFPVDronePawn>(GetOwningPawn());
	if (!Drone)
	{
		return;
	}

	DrawCrosshair();
	DrawThrottleBar(Drone->GetThrottle());
	DrawTelemetry(Drone->GetSpeedKPH(), Drone->GetAltitudeMeters());
	DrawTargetMarkers();
	DrawWarheadStatus(Drone);
	DrawMissionStatus();
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

void AFPVHUD::DrawMissionStatus()
{
	const AFPVWarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPVWarGameMode>() : nullptr;
	if (!GameMode)
	{
		return;
	}

	UFont* Font = GEngine->GetMediumFont();
	const float X = Canvas->SizeX - 260.f;
	float Y = 40.f;

	auto Line = [this, X, &Y, Font](const FString& Text, const FLinearColor& Color)
	{
		DrawText(Text, Color, X, Y, Font, 1.f);
		Y += 26.f;
	};

	Line(FString::Printf(TEXT("SCORE    %d"), GameMode->GetScore()), AccentColor);
	Line(FString::Printf(TEXT("KILLS    %d / %d"),
		GameMode->GetTargetsDestroyed(), GameMode->GetTotalTargets()), TextColor);

	const int32 PrimaryLeft = GameMode->GetPrimaryTargetsRemaining();
	Line(FString::Printf(TEXT("PRIMARY  %d left"), PrimaryLeft),
		PrimaryLeft > 0 ? TextColor : AccentColor);

	const int32 Drones = GameMode->GetDronesRemaining();
	Line(Drones < 0
			? FString(TEXT("DRONES   unlimited"))
			: FString::Printf(TEXT("DRONES   %d"), Drones),
		(Drones >= 0 && Drones <= 2) ? FLinearColor(1.f, 0.5f, 0.3f, 1.f) : TextColor);

	// Centre banners for the states that stop play.
	const float CentreX = Canvas->SizeX * 0.5f - 150.f;
	const float CentreY = Canvas->SizeY * 0.28f;

	if (GameMode->IsMissionComplete())
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.65f), CentreX - 20.f, CentreY - 12.f, 340.f, 52.f);
		DrawText(TEXT("ALL PRIMARY TARGETS DESTROYED"), AccentColor, CentreX, CentreY, Font, 1.f);
	}
	else if (GameMode->IsOutOfDrones())
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.65f), CentreX - 20.f, CentreY - 12.f, 340.f, 52.f);
		DrawText(TEXT("OUT OF DRONES  -  press R"), FLinearColor(1.f, 0.45f, 0.25f, 1.f), CentreX, CentreY, Font, 1.f);
	}

	const float Countdown = GameMode->GetRespawnCountdown();
	if (Countdown > 0.f)
	{
		DrawText(FString::Printf(TEXT("NEW DRONE IN %.1f"), Countdown),
			AccentColor, Canvas->SizeX * 0.5f - 80.f, Canvas->SizeY * 0.62f, Font, 1.f);
	}
}

void AFPVHUD::DrawWarheadStatus(const AFPVDronePawn* Drone)
{
	if (!Drone)
	{
		return;
	}

	UFont* Font = GEngine->GetMediumFont();
	const float X = BarLeftMargin - 4.f;
	const float Y = Canvas->SizeY - 140.f;

	if (Drone->IsWarheadSpent())
	{
		DrawText(TEXT("WARHEAD  SPENT"), FLinearColor(0.6f, 0.6f, 0.6f, 0.9f), X, Y, Font, 1.f);
	}
	else if (Drone->IsWarheadArmed())
	{
		DrawText(TEXT("WARHEAD  ARMED"), FLinearColor(1.f, 0.35f, 0.25f, 1.f), X, Y, Font, 1.f);
		DrawText(TEXT("F / LMB  airburst"),
			FLinearColor(1.f, 1.f, 1.f, 0.45f), X, Y + 24.f, GEngine->GetSmallFont(), 1.f);
	}
	else
	{
		// Safe below the arming altitude, which is why a fresh drone will not go off on the pad.
		DrawText(TEXT("WARHEAD  SAFE"), FLinearColor(1.f, 0.8f, 0.3f, 0.9f), X, Y, Font, 1.f);
		DrawText(TEXT("climb to arm"),
			FLinearColor(1.f, 1.f, 1.f, 0.45f), X, Y + 24.f, GEngine->GetSmallFont(), 1.f);
	}
}

bool AFPVHUD::DrawStrikeReport()
{
	const AFPVWarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPVWarGameMode>() : nullptr;
	if (!GameMode || GameMode->GetStrikeState() == EStrikeState::Flying)
	{
		return false;
	}

	UFont* TitleFont = GEngine->GetMediumFont();
	UFont* Font = GEngine->GetMediumFont();
	UFont* SmallFont = GEngine->GetSmallFont();

	// Letterbox. Cheap, and it reads immediately as "this is a cutaway, not gameplay".
	const float BarHeight = Canvas->SizeY * 0.11f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.85f), 0.f, 0.f, Canvas->SizeX, BarHeight);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.85f), 0.f, Canvas->SizeY - BarHeight, Canvas->SizeX, BarHeight);

	// The kill cam runs with no text at all, so the explosion is watched rather than read over.
	if (GameMode->GetStrikeState() == EStrikeState::KillCam)
	{
		DrawText(TEXT("IMPACT"), FLinearColor(1.f, 0.4f, 0.25f, 1.f),
			Canvas->SizeX * 0.5f - 46.f, BarHeight * 0.42f, TitleFont, 1.f);
		return true;
	}

	const TArray<FStrikeKill>& Kills = GameMode->GetStrikeKills();

	const float PanelW = 460.f;
	const float RowHeight = 28.f;
	const float HeaderHeight = 64.f;
	const float PanelH = HeaderHeight + FMath::Max(Kills.Num(), 1) * RowHeight + 62.f;
	const float PanelX = (Canvas->SizeX - PanelW) * 0.5f;
	const float PanelY = (Canvas->SizeY - PanelH) * 0.5f;

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.82f), PanelX, PanelY, PanelW, PanelH);
	DrawLine(PanelX, PanelY, PanelX + PanelW, PanelY, AccentColor, 2.f);

	DrawText(TEXT("STRIKE REPORT"), AccentColor, PanelX + 20.f, PanelY + 16.f, TitleFont, 1.f);

	float RowY = PanelY + HeaderHeight;

	if (Kills.Num() == 0)
	{
		// Named plainly. A miss should be as clear as a hit.
		DrawText(TEXT("NO TARGETS DESTROYED"), FLinearColor(1.f, 0.55f, 0.3f, 1.f),
			PanelX + 20.f, RowY, Font, 1.f);
		RowY += RowHeight;
	}
	else
	{
		for (const FStrikeKill& Kill : Kills)
		{
			const FLinearColor RowColor = Kill.bSecondary
				? FLinearColor(1.f, 0.78f, 0.35f, 1.f)   // chain reactions earn their own colour
				: TextColor;

			DrawText(Kill.TargetName, RowColor, PanelX + 20.f, RowY, Font, 1.f);

			if (Kill.bSecondary)
			{
				DrawText(TEXT("secondary"), FLinearColor(1.f, 1.f, 1.f, 0.45f),
					PanelX + 190.f, RowY + 3.f, SmallFont, 1.f);
			}

			DrawText(FString::Printf(TEXT("+%d"), Kill.Score), RowColor,
				PanelX + PanelW - 90.f, RowY, Font, 1.f);

			RowY += RowHeight;
		}
	}

	RowY += 10.f;
	DrawLine(PanelX + 20.f, RowY, PanelX + PanelW - 20.f, RowY, FLinearColor(1.f, 1.f, 1.f, 0.25f), 1.f);
	RowY += 10.f;

	DrawText(TEXT("STRIKE TOTAL"), TextColor, PanelX + 20.f, RowY, Font, 1.f);
	DrawText(FString::Printf(TEXT("%d"), GameMode->GetStrikeScore()), AccentColor,
		PanelX + PanelW - 90.f, RowY, Font, 1.f);

	const int32 Drones = GameMode->GetDronesRemaining();
	const FString DroneLine = (Drones < 0)
		? FString(TEXT("NEW DRONE INBOUND"))
		: FString::Printf(TEXT("DRONES REMAINING  %d"), Drones);

	DrawText(DroneLine, FLinearColor(1.f, 1.f, 1.f, 0.55f),
		PanelX + 20.f, PanelY + PanelH - 26.f, SmallFont, 1.f);

	DrawText(TEXT("R / F to continue"), FLinearColor(1.f, 1.f, 1.f, 0.4f),
		PanelX + PanelW - 150.f, PanelY + PanelH - 26.f, SmallFont, 1.f);

	return true;
}

void AFPVHUD::DrawTargetMarkers()
{
	const AFPVWarGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPVWarGameMode>() : nullptr;
	const AFPVDronePawn* Drone = Cast<AFPVDronePawn>(GetOwningPawn());
	if (!GameMode || !Drone)
	{
		return;
	}

	TArray<ADroneTarget*> Targets;
	GameMode->GetLiveTargets(Targets);
	if (Targets.Num() == 0)
	{
		return;
	}

	const FVector DroneLocation = Drone->GetActorLocation();
	const ADroneTarget* Nearest = GameMode->FindNearestLiveTarget(DroneLocation);
	const float BlastRadiusM = Drone->BlastRadius / 100.f;

	UFont* Font = GEngine->GetSmallFont();

	for (const ADroneTarget* Target : Targets)
	{
		if (!Target)
		{
			continue;
		}

		const FVector AimPoint = Target->GetAimPoint();
		const FVector Projected = Project(AimPoint);
		if (Projected.Z <= 0.f)
		{
			continue;   // behind the camera
		}

		const float DistanceM = FVector::Dist(DroneLocation, AimPoint) / 100.f;
		const bool bIsNearest = (Target == Nearest);

		// Inside blast range is the moment the airburst becomes worth taking, so it gets its
		// own colour rather than being something you have to judge from the range readout.
		const bool bInBlastRange = DistanceM <= BlastRadiusM;

		FLinearColor MarkerColor = bInBlastRange
			? FLinearColor(1.f, 0.3f, 0.2f, 1.f)
			: (bIsNearest ? AccentColor : FLinearColor(1.f, 1.f, 1.f, 0.4f));

		// Distant targets get a small fixed bracket; close ones scale up.
		const float Size = FMath::Clamp(6000.f / FMath::Max(DistanceM, 1.f), 10.f, 110.f);
		const float X = Projected.X;
		const float Y = Projected.Y;
		const float Arm = Size * 0.32f;
		const float Thickness = bIsNearest ? 2.f : 1.2f;

		auto Corner = [this, MarkerColor, Thickness](float CX, float CY, float DX, float DY, float Length)
		{
			DrawLine(CX, CY, CX + DX * Length, CY, MarkerColor, Thickness);
			DrawLine(CX, CY, CX, CY + DY * Length, MarkerColor, Thickness);
		};

		Corner(X - Size, Y - Size,  1.f,  1.f, Arm);
		Corner(X + Size, Y - Size, -1.f,  1.f, Arm);
		Corner(X - Size, Y + Size,  1.f, -1.f, Arm);
		Corner(X + Size, Y + Size, -1.f, -1.f, Arm);

		// Label only what is worth reading, so a target-rich level does not become soup.
		if (bIsNearest || bInBlastRange || DistanceM < 60.f)
		{
			DrawText(FString::Printf(TEXT("%s  %.0fm"), *Target->GetDisplayName(), DistanceM),
				MarkerColor, X - Size, Y + Size + 6.f, Font, 1.f);

			if (Target->GetHealthFraction() < 1.f)
			{
				const float BarW = Size * 2.f;
				DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.2f), X - Size, Y - Size - 10.f, BarW, 4.f);
				DrawRect(MarkerColor, X - Size, Y - Size - 10.f, BarW * Target->GetHealthFraction(), 4.f);
			}
		}

		if (bInBlastRange)
		{
			DrawText(TEXT("IN RANGE"), MarkerColor, X - Size, Y + Size + 20.f, Font, 1.f);
		}
	}
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

void AFPVHUD::DrawCalibrationWizard()
{
	UFont* Font = GEngine->GetMediumFont();
	const FRCChannelMapping& Mapping = FRCChannelMapping::Get();

	constexpr float PanelW = 640.f;
	constexpr float PanelH = 150.f;
	const float PanelX = (Canvas->SizeX - PanelW) * 0.5f;
	const float PanelY = Canvas->SizeY * 0.34f;

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.88f), PanelX, PanelY, PanelW, PanelH);

	DrawText(TEXT("RC CALIBRATION"), AccentColor, PanelX + 16.f, PanelY + 12.f, Font, 1.f);
	DrawText(Mapping.GetCalibrationPrompt(), TextColor, PanelX + 16.f, PanelY + 44.f, Font, 1.f);

	// Live feedback on which axis is currently winning, so a wrong stick is obvious immediately.
	const int32 Candidate = Mapping.GetCandidateAxis();
	const float Travel = Mapping.GetCandidateTravel();

	if (Candidate != INDEX_NONE && Travel > 0.02f)
	{
		const bool bEnough = Travel >= 0.25f;
		DrawText(FString::Printf(TEXT("detecting AXIS %d   travel %.2f   %s"),
				Candidate + 1, Travel, bEnough ? TEXT("- ready") : TEXT("- move it further")),
			bEnough ? AccentColor : FLinearColor(1.f, 0.75f, 0.2f, 1.f),
			PanelX + 16.f, PanelY + 76.f, Font, 1.f);
	}
	else
	{
		DrawText(TEXT("waiting for stick movement..."),
			FLinearColor(1.f, 1.f, 1.f, 0.5f), PanelX + 16.f, PanelY + 76.f, Font, 1.f);
	}

	DrawText(TEXT("fpv.CalibrateCancel to abort"),
		FLinearColor(1.f, 1.f, 1.f, 0.4f), PanelX + 16.f, PanelY + 112.f, GEngine->GetSmallFont(), 1.f);
}

// ---------------------------------------------------------------------------------------------
// Device picker
// ---------------------------------------------------------------------------------------------

void AFPVHUD::EnsureDeviceRegistryInitialised()
{
	if (bDeviceRegistryInitialised)
	{
		return;
	}
	bDeviceRegistryInitialised = true;

	FRCDeviceRegistry& Registry = FRCDeviceRegistry::Get();
	Registry.Refresh();

	// Saved mapping wins; otherwise seed a recognised radio with its known channel order so it
	// is flyable immediately, with the wizard available to refine the endpoints.
	FRCChannelMapping& Mapping = FRCChannelMapping::Get();
	Mapping.LoadFromConfig();

	const FRCInputDeviceInfo* Selected = Registry.GetSelectedDevice();
	if (!Mapping.IsConfigured() && Selected && Selected->IsKnownRadio())
	{
		Mapping.ApplyTango2Defaults();
	}
	UE_LOG(LogFPV, Log, TEXT("Input devices: %d total, %d flyable. Selected: %s (%s)"),
		Registry.GetDevices().Num(),
		Registry.GetGameDeviceIndices().Num(),
		Selected ? *Selected->GetDisplayName() : TEXT("none"),
		*Registry.GetStatusMessage());
}

void AFPVHUD::HandleDeviceMenuInput()
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	static const FKey NumberKeys[9] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine
	};

	for (int32 Slot = 0; Slot < 9; ++Slot)
	{
		if (PC->WasInputKeyJustPressed(NumberKeys[Slot]))
		{
			FRCDeviceRegistry::Get().SelectGameDevice(Slot);
			break;
		}
	}
}

void AFPVHUD::DrawDeviceMenu()
{
	UFont* Font = GEngine->GetSmallFont();
	FRCDeviceRegistry& Registry = FRCDeviceRegistry::Get();

	const TArray<int32>& Slots = Registry.GetGameDeviceIndices();
	const TArray<FRCInputDeviceInfo>& Devices = Registry.GetDevices();

	constexpr float PanelW = 580.f;
	constexpr float RowHeight = 20.f;
	constexpr float HeaderHeight = 48.f;

	const float PanelX = (Canvas->SizeX - PanelW) * 0.5f;
	constexpr float PanelY = 58.f;
	const float PanelH = HeaderHeight + FMath::Max(Slots.Num(), 1) * RowHeight + 44.f;

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.80f), PanelX, PanelY, PanelW, PanelH);

	DrawText(TEXT("INPUT DEVICE"), AccentColor, PanelX + 12.f, PanelY + 8.f, Font, 1.f);
	DrawText(TEXT("press 1-9 to select   |   fpv.RefreshDevices after plugging in"),
		FLinearColor(1.f, 1.f, 1.f, 0.5f), PanelX + 12.f, PanelY + 26.f, Font, 1.f);

	float RowY = PanelY + HeaderHeight;

	if (Slots.Num() == 0)
	{
		DrawText(TEXT("No joystick or gamepad devices found."),
			FLinearColor(1.f, 0.75f, 0.2f, 1.f), PanelX + 12.f, RowY, Font, 1.f);
		RowY += RowHeight;
	}

	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		const int32 DeviceIndex = Slots[Slot];
		if (!Devices.IsValidIndex(DeviceIndex))
		{
			continue;
		}

		const FRCInputDeviceInfo& Device = Devices[DeviceIndex];
		const bool bSelected = (DeviceIndex == Registry.GetSelectedIndex());

		const FLinearColor RowColor = bSelected
			? AccentColor
			: (Device.IsKnownRadio() ? FLinearColor(0.7f, 0.9f, 1.f, 0.9f) : TextColor);

		DrawText(FString::Printf(TEXT("[%d]"), Slot + 1), RowColor, PanelX + 12.f, RowY, Font, 1.f);
		DrawText(Device.GetDisplayName(), RowColor, PanelX + 46.f, RowY, Font, 1.f);
		DrawText(Device.GetIdString(), RowColor, PanelX + 300.f, RowY, Font, 1.f);
		DrawText(Device.GetUsageLabel(), RowColor, PanelX + 382.f, RowY, Font, 1.f);

		if (bSelected)
		{
			DrawText(TEXT("ACTIVE"), AccentColor, PanelX + 480.f, RowY, Font, 1.f);
		}

		RowY += RowHeight;
	}

	// Registration status. This is the line that says whether input can actually arrive.
	const FLinearColor StatusColor = Registry.IsRegistered()
		? FLinearColor(0.5f, 1.f, 0.6f, 0.9f)
		: FLinearColor(1.f, 0.6f, 0.3f, 1.f);
	DrawText(FString::Printf(TEXT("RawInput: %s"), *Registry.GetStatusMessage()),
		StatusColor, PanelX + 12.f, RowY + 6.f, Font, 1.f);

	const int32 OtherDevices = Devices.Num() - Slots.Num();
	if (OtherDevices > 0)
	{
		DrawText(FString::Printf(TEXT("(%d other HID device(s) present but not flyable)"), OtherDevices),
			FLinearColor(1.f, 1.f, 1.f, 0.4f), PanelX + 12.f, RowY + 24.f, Font, 1.f);
	}
}
