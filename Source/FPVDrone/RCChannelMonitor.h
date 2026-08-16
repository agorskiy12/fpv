#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;

/**
 * Samples the RawInput generic-USB axes every frame and remembers what each one has done.
 *
 * This exists because channel order on an RC transmitter is not knowable in advance. It varies
 * by radio, by firmware, and by whatever the pilot configured in the radio's own mixer. Rather
 * than guess AETR vs TAER, plug the radio in, wiggle a stick, and read which axis moved.
 *
 * Two input backends are watched at once, because it is not yet known which one will pick the
 * radio up:
 *   - RawInput's `GenericUSBController_Axis1..24`. Works today, but the plugin is deprecated
 *     as of 5.8 and is scheduled for removal.
 *   - GameInput's `FlightStick_Roll/Pitch/Yaw/Throttle`. The supported successor, currently
 *     beta, and only populated if GameInput classifies the device as a flight stick.
 *
 * Watching both means the question is answered by looking at the screen rather than by
 * guessing. Whichever one lights up when the transmitter is connected is the one to build on.
 *
 * Axes are addressed by FKey built from name rather than by linking either plugin's module, so
 * this compiles and runs whether or not they are enabled. Absent a backend, its axes read zero.
 */
struct FPVDRONE_API FRCChannelMonitor
{
	/** RawInput's numbered generic axes. */
	static constexpr int32 NumRawAxes = 24;

	/** GameInput's named flight-stick axes, appended after the numbered ones. */
	static constexpr int32 NumNamedAxes = 4;

	static constexpr int32 MaxAxes = NumRawAxes + NumNamedAxes;

	/** How many buttons to watch. */
	static constexpr int32 MaxButtons = 16;

	/** A change larger than this counts as "the pilot moved this one". */
	static constexpr float MovementThreshold = 0.02f;

	/** Total travel below this means the axis has never really moved. */
	static constexpr float ActivityThreshold = 0.05f;

	FRCChannelMonitor();

	/** Poll every axis and button. Cheap enough to call unconditionally. */
	void Sample(const APlayerController* PC);

	/** Forget the observed ranges and start measuring again. */
	void ResetRanges();

	float GetValue(int32 AxisIndex) const;
	float GetMin(int32 AxisIndex) const;
	float GetMax(int32 AxisIndex) const;

	/** True once this axis has covered a meaningful range. */
	bool HasMoved(int32 AxisIndex) const;

	bool IsButtonDown(int32 ButtonIndex) const;

	/** Display label, e.g. "AX 3" or "FS Roll". */
	FString GetAxisLabel(int32 AxisIndex) const;

	/** True for the GameInput named axes, which are always worth showing. */
	static bool IsNamedAxis(int32 AxisIndex) { return AxisIndex >= NumRawAxes && AxisIndex < MaxAxes; }

	/** Most recently moved axis, or INDEX_NONE. This is what identifies a channel. */
	int32 GetLastMovedAxis() const { return LastMovedAxis; }

	/** False until something moves -- i.e. no device, or the radio is not in joystick mode. */
	bool HasSeenAnyInput() const { return bHasSeenInput; }

	/** Lets the fpv.ResetChannelRanges console command reach whichever monitor is live. */
	static void RequestReset();

private:
	FKey AxisKeys[MaxAxes];
	FKey ButtonKeys[MaxButtons];

	float Values[MaxAxes];
	float MinSeen[MaxAxes];
	float MaxSeen[MaxAxes];
	bool bButtonDown[MaxButtons];

	/** Ranges are seeded from the first sample rather than from a sentinel, so the readout is honest before any input arrives. */
	bool bSeeded = false;
	bool bHasSeenInput = false;
	int32 LastMovedAxis = INDEX_NONE;
};
