#include "RCChannelMapping.h"
#include "FPVDrone.h"
#include "RCDeviceRegistry.h"

#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* RCConfigSection = TEXT("/Script/FPVDrone.RCChannelMapping");

	/** Enough travel to be confident the pilot moved this axis deliberately. */
	constexpr float MinAssignTravel = 0.25f;
}

FRCChannelMapping& FRCChannelMapping::Get()
{
	static FRCChannelMapping Instance;
	return Instance;
}

const TCHAR* FRCChannelMapping::ChannelName(ERCChannel Channel)
{
	switch (Channel)
	{
	case ERCChannel::Throttle: return TEXT("THROTTLE");
	case ERCChannel::Roll:     return TEXT("ROLL");
	case ERCChannel::Pitch:    return TEXT("PITCH");
	case ERCChannel::Yaw:      return TEXT("YAW");
	default:                   return TEXT("?");
	}
}

const FRCChannelCal& FRCChannelMapping::GetCal(ERCChannel Channel) const
{
	return Channels[static_cast<int32>(Channel)];
}

void FRCChannelMapping::SetAxis(ERCChannel Channel, int32 AxisIndex)
{
	FRCChannelCal& Cal = Channels[static_cast<int32>(Channel)];
	Cal.AxisIndex = AxisIndex;
	UE_LOG(LogFPV, Log, TEXT("Channel %s -> axis %d"), ChannelName(Channel), AxisIndex + 1);
}

void FRCChannelMapping::SetInverted(ERCChannel Channel, bool bInvert)
{
	Channels[static_cast<int32>(Channel)].bInvert = bInvert;
}

bool FRCChannelMapping::IsConfigured() const
{
	for (int32 Index = 0; Index < static_cast<int32>(ERCChannel::Count); ++Index)
	{
		if (!Channels[Index].IsValid())
		{
			return false;
		}
	}
	return true;
}

void FRCChannelMapping::ApplyTango2Defaults()
{
	// Axis numbers here are the 1-based ones shown in the channel monitor, minus one.
	struct FDefault { ERCChannel Channel; int32 DisplayAxis; };
	static const FDefault Defaults[] = {
		{ ERCChannel::Throttle, 8 },
		{ ERCChannel::Yaw,      5 },
		{ ERCChannel::Roll,     7 },
		{ ERCChannel::Pitch,    6 },
	};

	for (const FDefault& Default : Defaults)
	{
		FRCChannelCal& Cal = Channels[static_cast<int32>(Default.Channel)];
		Cal.AxisIndex = Default.DisplayAxis - 1;
		Cal.RawMin = 0.f;
		Cal.RawMax = 1.f;
		Cal.RawCenter = 0.5f;
		Cal.bInvert = false;
	}

	UE_LOG(LogFPV, Log, TEXT("Applied Tango 2 defaults: throttle=8 yaw=5 roll=7 pitch=6"));
}

float FRCChannelMapping::GetChannelValue(ERCChannel Channel) const
{
	const FRCChannelCal& Cal = GetCal(Channel);
	if (!Cal.IsValid())
	{
		return 0.f;
	}

	const float Raw = FRCDeviceRegistry::Get().GetParsedAxis(Cal.AxisIndex);

	if (Channel == ERCChannel::Throttle)
	{
		// Throttle is a one-sided channel: it rests at the bottom of its travel rather than
		// springing back to a centre, so no centre handling applies.
		const float Span = Cal.RawMax - Cal.RawMin;
		float Value = (Span > KINDA_SMALL_NUMBER) ? (Raw - Cal.RawMin) / Span : 0.f;
		Value = FMath::Clamp(Value, 0.f, 1.f);
		return Cal.bInvert ? (1.f - Value) : Value;
	}

	// Mapped either side of the measured centre rather than the midpoint of the endpoints:
	// gimbal travel is rarely symmetric, and assuming it is shows up as drift at rest.
	float Value;
	if (Raw >= Cal.RawCenter)
	{
		const float Span = Cal.RawMax - Cal.RawCenter;
		Value = (Span > KINDA_SMALL_NUMBER) ? (Raw - Cal.RawCenter) / Span : 0.f;
	}
	else
	{
		const float Span = Cal.RawCenter - Cal.RawMin;
		Value = (Span > KINDA_SMALL_NUMBER) ? -((Cal.RawCenter - Raw) / Span) : 0.f;
	}

	Value = FMath::Clamp(Value, -1.f, 1.f);
	return Cal.bInvert ? -Value : Value;
}

// ---------------------------------------------------------------------------------------------
// Calibration wizard
// ---------------------------------------------------------------------------------------------

void FRCChannelMapping::BeginCalibration()
{
	UE_LOG(LogFPV, Log, TEXT("Channel calibration started."));
	BeginStep(ERCCalStep::Throttle);
}

void FRCChannelMapping::CancelCalibration()
{
	Step = ERCCalStep::Inactive;
	CandidateAxis = INDEX_NONE;
	CandidateTravel = 0.f;
	UE_LOG(LogFPV, Log, TEXT("Channel calibration cancelled."));
}

void FRCChannelMapping::BeginStep(ERCCalStep NewStep)
{
	Step = NewStep;
	CandidateAxis = INDEX_NONE;
	CandidateTravel = 0.f;
	bStepSeeded = false;

	StepMin.Reset();
	StepMax.Reset();
	StepMin.SetNumZeroed(FRCDeviceRegistry::MaxParsedAxes);
	StepMax.SetNumZeroed(FRCDeviceRegistry::MaxParsedAxes);
}

void FRCChannelMapping::CommitStepAssignment()
{
	if (CandidateAxis == INDEX_NONE)
	{
		return;
	}

	ERCChannel Channel = ERCChannel::Throttle;
	switch (Step)
	{
	case ERCCalStep::Throttle: Channel = ERCChannel::Throttle; break;
	case ERCCalStep::Roll:     Channel = ERCChannel::Roll;     break;
	case ERCCalStep::Pitch:    Channel = ERCChannel::Pitch;    break;
	case ERCCalStep::Yaw:      Channel = ERCChannel::Yaw;      break;
	default: return;
	}

	FRCChannelCal& Cal = Channels[static_cast<int32>(Channel)];
	Cal.AxisIndex = CandidateAxis;
	Cal.RawMin = StepMin[CandidateAxis];
	Cal.RawMax = StepMax[CandidateAxis];
	Cal.RawCenter = (Cal.RawMin + Cal.RawMax) * 0.5f;   // refined by the centre step

	UE_LOG(LogFPV, Log, TEXT("%s -> axis %d  [%.3f .. %.3f]"),
		ChannelName(Channel), CandidateAxis + 1, Cal.RawMin, Cal.RawMax);
}

void FRCChannelMapping::TickCalibration(APlayerController* PC)
{
	if (!IsCalibrating() || !PC)
	{
		return;
	}

	const FRCDeviceRegistry& Registry = FRCDeviceRegistry::Get();

	// Track how far each axis has travelled since this step began.
	for (int32 Index = 0; Index < FRCDeviceRegistry::MaxParsedAxes; ++Index)
	{
		const float Value = Registry.GetParsedAxis(Index);
		if (!bStepSeeded)
		{
			StepMin[Index] = Value;
			StepMax[Index] = Value;
		}
		else
		{
			StepMin[Index] = FMath::Min(StepMin[Index], Value);
			StepMax[Index] = FMath::Max(StepMax[Index], Value);
		}
	}
	bStepSeeded = true;

	// The axis that moved most is the one the pilot is holding.
	CandidateAxis = INDEX_NONE;
	CandidateTravel = 0.f;
	for (int32 Index = 0; Index < FRCDeviceRegistry::MaxParsedAxes; ++Index)
	{
		const float Travel = StepMax[Index] - StepMin[Index];
		if (Travel > CandidateTravel)
		{
			CandidateTravel = Travel;
			CandidateAxis = Index;
		}
	}

	if (!PC->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		return;
	}

	if (Step == ERCCalStep::Centre)
	{
		// Sample the resting position of each stick.
		for (int32 Index = 1; Index < static_cast<int32>(ERCChannel::Count); ++Index)
		{
			FRCChannelCal& Cal = Channels[Index];
			if (Cal.AxisIndex != INDEX_NONE)
			{
				Cal.RawCenter = Registry.GetParsedAxis(Cal.AxisIndex);
			}
		}

		Step = ERCCalStep::Done;
		SaveToConfig();
		UE_LOG(LogFPV, Log, TEXT("Calibration complete and saved."));
		return;
	}

	if (CandidateTravel < MinAssignTravel)
	{
		UE_LOG(LogFPV, Warning, TEXT("Not enough movement to assign %s (best was %.2f). Move the stick fully."),
			GetCalibrationPrompt().IsEmpty() ? TEXT("channel") : *GetCalibrationPrompt(), CandidateTravel);
		return;
	}

	CommitStepAssignment();

	switch (Step)
	{
	case ERCCalStep::Throttle: BeginStep(ERCCalStep::Roll);   break;
	case ERCCalStep::Roll:     BeginStep(ERCCalStep::Pitch);  break;
	case ERCCalStep::Pitch:    BeginStep(ERCCalStep::Yaw);    break;
	case ERCCalStep::Yaw:      BeginStep(ERCCalStep::Centre); break;
	default: break;
	}
}

FString FRCChannelMapping::GetCalibrationPrompt() const
{
	switch (Step)
	{
	case ERCCalStep::Throttle: return TEXT("Move THROTTLE fully up and down, then press SPACE");
	case ERCCalStep::Roll:     return TEXT("Move ROLL fully left and right, then press SPACE");
	case ERCCalStep::Pitch:    return TEXT("Move PITCH fully forward and back, then press SPACE");
	case ERCCalStep::Yaw:      return TEXT("Move YAW fully left and right, then press SPACE");
	case ERCCalStep::Centre:   return TEXT("Let every stick rest at centre, then press SPACE");
	case ERCCalStep::Done:     return TEXT("Calibration complete");
	default:                   return FString();
	}
}

// ---------------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------------

void FRCChannelMapping::SaveToConfig()
{
	if (!GConfig)
	{
		return;
	}

	for (int32 Index = 0; Index < static_cast<int32>(ERCChannel::Count); ++Index)
	{
		const FRCChannelCal& Cal = Channels[Index];
		const FString Key = FString::Printf(TEXT("Channel%d"), Index);
		const FString Value = FString::Printf(TEXT("(Axis=%d,Min=%f,Centre=%f,Max=%f,Invert=%d)"),
			Cal.AxisIndex, Cal.RawMin, Cal.RawCenter, Cal.RawMax, Cal.bInvert ? 1 : 0);
		GConfig->SetString(RCConfigSection, *Key, *Value, GGameUserSettingsIni);
	}
	GConfig->Flush(false, GGameUserSettingsIni);
}

void FRCChannelMapping::LoadFromConfig()
{
	if (!GConfig)
	{
		return;
	}

	for (int32 Index = 0; Index < static_cast<int32>(ERCChannel::Count); ++Index)
	{
		FString Value;
		const FString Key = FString::Printf(TEXT("Channel%d"), Index);
		if (!GConfig->GetString(RCConfigSection, *Key, Value, GGameUserSettingsIni))
		{
			continue;
		}

		FRCChannelCal& Cal = Channels[Index];
		int32 Invert = 0;
		FParse::Value(*Value, TEXT("Axis="), Cal.AxisIndex);
		FParse::Value(*Value, TEXT("Min="), Cal.RawMin);
		FParse::Value(*Value, TEXT("Centre="), Cal.RawCenter);
		FParse::Value(*Value, TEXT("Max="), Cal.RawMax);
		FParse::Value(*Value, TEXT("Invert="), Invert);
		Cal.bInvert = (Invert != 0);
	}

	if (IsConfigured())
	{
		UE_LOG(LogFPV, Log, TEXT("Loaded RC channel mapping: T=%d R=%d P=%d Y=%d"),
			Channels[0].AxisIndex + 1, Channels[1].AxisIndex + 1,
			Channels[2].AxisIndex + 1, Channels[3].AxisIndex + 1);
	}
}
