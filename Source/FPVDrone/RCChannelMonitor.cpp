#include "RCChannelMonitor.h"
#include "FPVDrone.h"

#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
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

	for (int32 Index = 0; Index < MaxAxes; ++Index)
	{
		const float NewValue = PC->GetInputAnalogKeyState(AxisKeys[Index]);

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

	for (int32 Index = 0; Index < MaxButtons; ++Index)
	{
		const bool bDown = PC->IsInputKeyDown(ButtonKeys[Index]);
		if (bDown && !bButtonDown[Index])
		{
			bHasSeenInput = true;
		}
		bButtonDown[Index] = bDown;
	}

	bSeeded = true;
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
