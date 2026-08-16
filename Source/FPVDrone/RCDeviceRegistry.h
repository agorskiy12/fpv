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
	void TickRegistration();

	/** Human-readable note about the last registration attempt, for the menu. */
	const FString& GetStatusMessage() const { return StatusMessage; }

private:
	FRCDeviceRegistry() = default;

	void EnumerateDevices();
	void RebuildGameDeviceList();
	bool ApplyRawInputRegistration(uint16 UsagePage, uint16 Usage);

	TArray<FRCInputDeviceInfo> Devices;
	TArray<int32> GameDeviceIndices;

	int32 SelectedIndex = INDEX_NONE;
	int32 RegisteredHandle = INDEX_NONE;
	uint16 RegisteredUsagePage = 0;
	uint16 RegisteredUsage = 0;

	/** Keeps a genuinely failing registration from spamming the log every frame. */
	bool bLoggedRegistrationFailure = false;

	FString StatusMessage;
};
