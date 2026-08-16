#pragma once

#include "CoreMinimal.h"

/** HID usage page / usage codes for the device classes we care about. */
namespace RCHidUsage
{
	constexpr uint16 GenericDesktopPage = 0x01;
	constexpr uint16 Joystick = 0x04;
	constexpr uint16 Gamepad = 0x05;

	/** RIM_TYPEHID, redeclared so this header stays free of Windows includes. */
	constexpr int32 RawInputTypeHID = 2;
}

/** Known radios, so the menu can show something better than a hex pair. */
namespace RCKnownDevice
{
	constexpr uint32 TBS_VendorId = 0x04D8;   // Microchip, used by TBS FreedomTX
	constexpr uint32 Tango2_ProductId = 0x5710;
}

/** One connected HID device. */
struct FPVDRONE_API FRCInputDeviceInfo
{
	FString ProductName;
	FString DevicePath;
	uint32 VendorId = 0;
	uint32 ProductId = 0;
	uint16 UsagePage = 0;
	uint16 Usage = 0;

	/** Joystick or gamepad on the generic desktop page -- i.e. something you could fly with. */
	bool IsGameDevice() const;

	/** True for hardware we recognise by VID/PID. */
	bool IsKnownRadio() const;

	/** "Joystick" / "Gamepad" / "Usage 0x.." */
	FString GetUsageLabel() const;

	/** "04D8:5710" */
	FString GetIdString() const;

	/** Product name if the driver gave us one, otherwise something synthesised. */
	FString GetDisplayName() const;
};

/**
 * Enumerates connected HID devices and decides which one drives the sim.
 *
 * This exists because of a real defect in the RawInput plugin. At startup it registers HID
 * usage 0x04 (Joystick) and only falls back to 0x05 (Gamepad) if that *fails*
 * (RawInputWindows.cpp:97-111). With any joystick attached, the joystick registration
 * succeeds, the fallback never runs, and gamepad-class devices are never subscribed to.
 *
 * A TBS Tango 2 enumerates as usage 0x05. So a plugged-in flight stick silently prevents the
 * radio from being seen at all -- which is exactly what happened here.
 *
 * The fix: turn the plugin's automatic registration off (bRegisterDefaultDevice=False in
 * DefaultInput.ini) and register the selected device's usage explicitly instead.
 *
 * Selecting a device therefore does something concrete: it registers that device's usage and
 * drops the previous one. Two devices with *different* usages -- a joystick and a radio, say --
 * are cleanly separated this way. Two devices sharing the same usage cannot be separated,
 * because RawInput emits every device's axes to the same GenericUSBController_AxisN keys.
 */
class FPVDRONE_API FRCDeviceRegistry
{
public:
	static FRCDeviceRegistry& Get();

	/** Re-enumerate connected devices. Keeps the current selection if that device is still present. */
	void Refresh();

	const TArray<FRCInputDeviceInfo>& GetDevices() const { return Devices; }

	/** Devices worth flying with, as indices into GetDevices(). */
	const TArray<int32>& GetGameDeviceIndices() const { return GameDeviceIndices; }

	int32 GetSelectedIndex() const { return SelectedIndex; }
	const FRCInputDeviceInfo* GetSelectedDevice() const;

	/** Select by index into GetDevices(). Registers that device's HID usage with RawInput. */
	bool SelectDevice(int32 DeviceIndex);

	/** Select by position within GetGameDeviceIndices() -- what the menu's number keys use. */
	bool SelectGameDevice(int32 MenuSlot);

	/** Prefer a known radio, then any gamepad, then any joystick. */
	void AutoSelect();

	/** True once a usage has actually been registered with RawInput. */
	bool IsRegistered() const { return RegisteredHandle != INDEX_NONE; }

	/**
	 * Retry registration until it takes. Cheap no-op once registered.
	 *
	 * RawInput creates its input device during Slate startup, which happens *after* the first
	 * HUD draw -- so a single attempt at startup reliably loses the race and fails with
	 * "RawInput device not created yet". Call this every frame instead.
	 */
	/**
	 * @param bInputSeen whether any axis has actually moved yet.
	 *
	 * While no input has been seen, registration is retried every couple of seconds. That is
	 * deliberate self-healing: RawInput latches hwndTarget to Slate's active top-level window
	 * at registration time, and early in startup that can be a loading window rather than the
	 * real game window -- in which case WM_INPUT is delivered somewhere useless forever, with
	 * no error anywhere to indicate it.
	 */
	void TickRegistration(bool bInputSeen);

	/** Count of all raw WM_INPUT packets seen -- mouse and keyboard included. */
	int64 GetRawPacketCount() const { return RawPacketCount; }

	/**
	 * Count of WM_INPUT packets of HID type, i.e. neither mouse nor keyboard.
	 *
	 * This is the number that matters. A rising total with this stuck at zero means the
	 * packets are just mouse movement and the radio is still not reaching us.
	 */
	int64 GetRawHidPacketCount() const { return RawHidPacketCount; }

	/** HID packets whose device handle matches the selected device specifically. */
	int64 GetRawSelectedPacketCount() const { return RawSelectedPacketCount; }

	// -----------------------------------------------------------------------------------------
	// Direct HID parsing
	//
	// RawInput delivers the Tango 2's reports to this process at its full 125 Hz, but its own
	// ParseInputData never runs: the device-match in ProcessMessage (RawInputWindows.cpp:556-573)
	// never succeeds, so the packets are dropped before parsing is attempted. There are no
	// warnings because the parser is simply never entered.
	//
	// Rather than fight that, the reports are decoded here with HidP_* directly. That removes
	// the dependency on the deprecated plugin for everything except the WM_INPUT subscription,
	// and it puts axis normalisation under our control -- which Phase 2 calibration wants anyway.
	// -----------------------------------------------------------------------------------------

	static constexpr int32 MaxParsedAxes = 24;

	/** Axis value normalised to 0..1 from the report's own logical range. */
	float GetParsedAxis(int32 AxisIndex) const;

	/** Number of analog axes the selected device actually reports. */
	int32 GetParsedAxisCount() const { return ParsedAxisCount; }

	/** Bitmask of pressed buttons. */
	uint32 GetParsedButtons() const { return ParsedButtons; }

	/** True once a report has been decoded successfully. */
	bool HasParsedReport() const { return bHasParsedReport; }

	/** Last HID parse error, empty when fine. */
	const FString& GetParseError() const { return ParseError; }

	/**
	 * Drop the current registration and register again.
	 *
	 * Needed because RawInput binds hwndTarget to whatever Slate reports as the active
	 * top-level window at the moment of registration (RawInputWindows.cpp:129-141). Register
	 * while some other window is in front and WM_INPUT is routed to the wrong HWND forever.
	 * Re-registering once the game window is genuinely focused fixes it.
	 */
	bool ForceReRegister();

	/** Human-readable note about the last registration attempt, for the menu. */
	const FString& GetStatusMessage() const { return StatusMessage; }

private:
	FRCDeviceRegistry() = default;

	void EnumerateDevices();
	void RebuildGameDeviceList();
	bool ApplyRawInputRegistration(uint16 UsagePage, uint16 Usage);

	/** Taps RawInput's raw-data delegate purely to count packets; never consumes them. */
	void InstallRawDataProbe();

	/** Decode one HID input report into ParsedAxes / ParsedButtons. */
	void ParseHidReport(const void* RawInputPacket);

	int64 RawPacketCount = 0;
	int64 RawHidPacketCount = 0;
	int64 RawSelectedPacketCount = 0;
	bool bRawProbeInstalled = false;
	double LastRegisterAttemptTime = 0.0;

	float ParsedAxes[MaxParsedAxes] = {};
	int32 ParsedAxisCount = 0;
	uint32 ParsedButtons = 0;
	bool bHasParsedReport = false;
	FString ParseError;

	/** Scratch buffers, kept as members so the per-packet path does no allocation. */
	TArray<uint8> PreparsedBuffer;
	TArray<uint8> ValueCapsBuffer;
	TArray<uint8> ButtonCapsBuffer;
	TArray<uint16> UsageListBuffer;

	TArray<FRCInputDeviceInfo> Devices;
	TArray<int32> GameDeviceIndices;

	int32 SelectedIndex = INDEX_NONE;
	int32 RegisteredHandle = INDEX_NONE;
	uint16 RegisteredUsagePage = 0;
	uint16 RegisteredUsage = 0;

	/** Keeps a genuinely failing registration from spamming the log every frame. */
	bool bLoggedRegistrationFailure = false;

	/** Focus edge detection, so registration can be redone when the game window comes forward. */
	bool bWasFocused = false;

	FString StatusMessage;
};
