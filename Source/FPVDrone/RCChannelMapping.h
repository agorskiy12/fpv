#pragma once

#include "CoreMinimal.h"

class APlayerController;

/** The four flight channels, in TAER order. */
enum class ERCChannel : uint8
{
	Throttle = 0,
	Roll,
	Pitch,
	Yaw,
	Count
};

/** Which raw axis drives a channel, and the endpoints observed for it. */
struct FPVDRONE_API FRCChannelCal
{
	int32 AxisIndex = INDEX_NONE;
	float RawMin = 0.f;
	float RawCenter = 0.5f;
	float RawMax = 1.f;
	bool bInvert = false;

	bool IsValid() const { return AxisIndex != INDEX_NONE && (RawMax - RawMin) > 0.05f; }
};

/** Where the calibration wizard currently is. */
enum class ERCCalStep : uint8
{
	Inactive = 0,
	Throttle,
	Roll,
	Pitch,
	Yaw,
	Centre,
	Done
};

/**
 * Maps raw HID axes onto flight channels, and calibrates their endpoints.
 *
 * Channel order is not guessable. It varies by radio, by firmware, and by whatever the pilot
 * set up in their own mixer -- AETR and TAER are both common, and a Tango 2 can be either. An
 * attempt here to infer it from a scripted stick sweep produced ambiguous results, with two
 * axes moving within one sample of each other.
 *
 * So it is not inferred. The pilot moves each stick in turn and the axis that actually moved
 * is the one assigned. This is what every sim does, and it is the only approach that cannot be
 * wrong.
 *
 * Endpoints are captured during the same pass, and centre is sampled separately at rest --
 * gimbal travel is rarely symmetric, so assuming centre is the midpoint of min and max
 * introduces a bias you then feel as drift.
 */
class FPVDRONE_API FRCChannelMapping
{
public:
	static FRCChannelMapping& Get();

	void LoadFromConfig();
	void SaveToConfig();

	const FRCChannelCal& GetCal(ERCChannel Channel) const;
	void SetAxis(ERCChannel Channel, int32 AxisIndex);
	void SetInverted(ERCChannel Channel, bool bInvert);

	/** True once every channel has a usable axis assignment. */
	bool IsConfigured() const;

	/**
	 * Stock TBS Tango 2 channel order, confirmed against the hardware:
	 * throttle on axis 8, yaw on 5, roll on 7, pitch on 6.
	 *
	 * Note this is neither plain AETR nor TAER, which is precisely why it was not guessable.
	 * Endpoints default to the full 0..1 range with centre at 0.5; run the wizard to measure
	 * the real ones.
	 */
	void ApplyTango2Defaults();

	/**
	 * Throttle returns 0..1; roll, pitch and yaw return -1..1.
	 * Returns 0 for channels that are not configured.
	 */
	float GetChannelValue(ERCChannel Channel) const;

	// --- Calibration wizard ---------------------------------------------------------------

	void BeginCalibration();
	void CancelCalibration();
	bool IsCalibrating() const { return Step != ERCCalStep::Inactive && Step != ERCCalStep::Done; }
	ERCCalStep GetStep() const { return Step; }

	/** Watches axis travel and advances on the confirm key. Call once per frame. */
	void TickCalibration(APlayerController* PC);

	/** Instruction text for the current step. */
	FString GetCalibrationPrompt() const;

	/** Live feedback: the axis currently moving most in this step, or INDEX_NONE. */
	int32 GetCandidateAxis() const { return CandidateAxis; }
	float GetCandidateTravel() const { return CandidateTravel; }

	static const TCHAR* ChannelName(ERCChannel Channel);

private:
	FRCChannelMapping() = default;

	void BeginStep(ERCCalStep NewStep);
	void CommitStepAssignment();

	FRCChannelCal Channels[static_cast<int32>(ERCChannel::Count)];

	ERCCalStep Step = ERCCalStep::Inactive;

	/** Per-axis extremes observed since the current step began. */
	TArray<float> StepMin;
	TArray<float> StepMax;

	int32 CandidateAxis = INDEX_NONE;
	float CandidateTravel = 0.f;
	bool bStepSeeded = false;
};
