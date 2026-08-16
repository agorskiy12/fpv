#include "RCChannelMonitor.h"
#include "FPVDrone.h"
#include "RCDeviceRegistry.h"

#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarLogChannels(
		TEXT("fpv.LogChannels"),
		0,
		TEXT("Write RC axis values to the log twice a second.\n")
		TEXT("Useful when the on-screen overlay cannot be observed directly -- the log line\n")
		TEXT("also reports window focus and RawInput registration state, which are the two\n")
		TEXT("things that stop input arriving."),
		ECVF_Default);

	/** Set by the console command, consumed by the next Sample. Avoids plumbing an instance pointer to the console. */
	bool GResetRangesRequested = false;

	FAutoConsoleCommand CmdResetChannelRanges(
		TEXT("fpv.ResetChannelRanges"),
		TEXT("Clear the observed min/max range for every RC axis in the channel monitor."),
		FConsoleCommandDelegate::CreateStatic([]()
		{
			GResetRangesRequested = true;
			UE_LOG(LogFPV, Log, TEXT("RC channel ranges reset."));
		}));
}

FRCChannelMonitor::FRCChannelMonitor()
{
	// RawInput's numbered axes.
	for (int32 Index = 0; Index < NumRawAxes; ++Index)
	{
		AxisKeys[Index] = FKey(*FString::Printf(TEXT("GenericUSBController_Axis%d"), Index + 1));
	}

	// GameInput's named flight-stick axes, appended after them.
	static const TCHAR* NamedAxisKeyNames[NumNamedAxes] = {
		TEXT("FlightStick_Roll"),
		TEXT("FlightStick_Pitch"),
		TEXT("FlightStick_Yaw"),
		TEXT("FlightStick_Throttle")
	};
	for (int32 Index = 0; Index < NumNamedAxes; ++Index)
	{
		AxisKeys[NumRawAxes + Index] = FKey(NamedAxisKeyNames[Index]);
	}

	for (int32 Index = 0; Index < MaxAxes; ++Index)
	{
		Values[Index] = 0.f;
		MinSeen[Index] = 0.f;
		MaxSeen[Index] = 0.f;
	}

	for (int32 Index = 0; Index < MaxButtons; ++Index)
	{
		ButtonKeys[Index] = FKey(*FString::Printf(TEXT("GenericUSBController_Button%d"), Index + 1));
		bButtonDown[Index] = false;
	}
}

void FRCChannelMonitor::Sample(const APlayerController* PC)
{
	if (!PC)
	{
		return;
	}

	if (GResetRangesRequested)
	{
		GResetRangesRequested = false;
		ResetRanges();
	}

	// Direct HID parsing is authoritative when it is working. RawInput's own parser never runs
	// for this device, so its keys stay at zero; fall back to them only when we have no report
	// of our own (a device the direct parser has not seen, or a non-Windows build).
	const FRCDeviceRegistry& DeviceRegistry = FRCDeviceRegistry::Get();
	const bool bUseParsedReport = DeviceRegistry.HasParsedReport();

	for (int32 Index = 0; Index < MaxAxes; ++Index)
	{
		const float NewValue = (bUseParsedReport && Index < FRCDeviceRegistry::MaxParsedAxes)
			? DeviceRegistry.GetParsedAxis(Index)
			: PC->GetInputAnalogKeyState(AxisKeys[Index]);

		if (bSeeded && FMath::Abs(NewValue - Values[Index]) > MovementThreshold)
		{
			LastMovedAxis = Index;
			bHasSeenInput = true;
		}

		Values[Index] = NewValue;

		if (bSeeded)
		{
			MinSeen[Index] = FMath::Min(MinSeen[Index], NewValue);
			MaxSeen[Index] = FMath::Max(MaxSeen[Index], NewValue);
		}
		else
		{
			MinSeen[Index] = NewValue;
			MaxSeen[Index] = NewValue;
		}
	}

	const uint32 ParsedButtonMask = DeviceRegistry.GetParsedButtons();
	for (int32 Index = 0; Index < MaxButtons; ++Index)
	{
		const bool bDown = bUseParsedReport
			? ((ParsedButtonMask & (1u << Index)) != 0)
			: PC->IsInputKeyDown(ButtonKeys[Index]);
		if (bDown && !bButtonDown[Index])
		{
			bHasSeenInput = true;
		}
		bButtonDown[Index] = bDown;
	}

	bSeeded = true;

	if (CVarLogChannels.GetValueOnGameThread() != 0)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - LastLogTime > 0.5)
		{
			LastLogTime = Now;

			FString Line;
			for (int32 Index = 0; Index < MaxAxes; ++Index)
			{
				if (HasMoved(Index) || FMath::Abs(Values[Index]) > 0.005f)
				{
					Line += FString::Printf(TEXT("%s=%+.3f  "), *GetAxisLabel(Index), Values[Index]);
				}
			}
			if (Line.IsEmpty())
			{
				Line = TEXT("(every axis reading exactly zero)");
			}

			const FRCDeviceRegistry& Registry = FRCDeviceRegistry::Get();
			UE_LOG(LogFPV, Log, TEXT("RC [focus=%s reg=%s pkt=%lld hid=%lld mine=%lld] %s"),
				FApp::HasFocus() ? TEXT("yes") : TEXT("NO"),
				Registry.IsRegistered() ? TEXT("yes") : TEXT("NO"),
				Registry.GetRawPacketCount(),
				Registry.GetRawHidPacketCount(),
				Registry.GetRawSelectedPacketCount(),
				*Line);
		}
	}
}

void FRCChannelMonitor::ResetRanges()
{
	for (int32 Index = 0; Index < MaxAxes; ++Index)
	{
		MinSeen[Index] = Values[Index];
		MaxSeen[Index] = Values[Index];
	}
	LastMovedAxis = INDEX_NONE;
}

float FRCChannelMonitor::GetValue(int32 AxisIndex) const
{
	return (AxisIndex >= 0 && AxisIndex < MaxAxes) ? Values[AxisIndex] : 0.f;
}

float FRCChannelMonitor::GetMin(int32 AxisIndex) const
{
	return (AxisIndex >= 0 && AxisIndex < MaxAxes) ? MinSeen[AxisIndex] : 0.f;
}

float FRCChannelMonitor::GetMax(int32 AxisIndex) const
{
	return (AxisIndex >= 0 && AxisIndex < MaxAxes) ? MaxSeen[AxisIndex] : 0.f;
}

bool FRCChannelMonitor::HasMoved(int32 AxisIndex) const
{
	if (AxisIndex < 0 || AxisIndex >= MaxAxes)
	{
		return false;
	}
	return (MaxSeen[AxisIndex] - MinSeen[AxisIndex]) > ActivityThreshold;
}

bool FRCChannelMonitor::IsButtonDown(int32 ButtonIndex) const
{
	return (ButtonIndex >= 0 && ButtonIndex < MaxButtons) ? bButtonDown[ButtonIndex] : false;
}

FString FRCChannelMonitor::GetAxisLabel(int32 AxisIndex) const
{
	static const TCHAR* NamedLabels[NumNamedAxes] = {
		TEXT("FS Roll"), TEXT("FS Pitch"), TEXT("FS Yaw"), TEXT("FS Thr")
	};

	if (IsNamedAxis(AxisIndex))
	{
		return NamedLabels[AxisIndex - NumRawAxes];
	}
	return FString::Printf(TEXT("AX %2d"), AxisIndex + 1);
}

void FRCChannelMonitor::RequestReset()
{
	GResetRangesRequested = true;
}
