#include "RCDeviceRegistry.h"
#include "FPVDrone.h"

#if PLATFORM_WINDOWS
#include "RawInput.h"
#include "Windows/AllowWindowsPlatformTypes.h"
	#include <hidsdi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// ---------------------------------------------------------------------------------------------
// FRCInputDeviceInfo
// ---------------------------------------------------------------------------------------------

bool FRCInputDeviceInfo::IsGameDevice() const
{
	return UsagePage == RCHidUsage::GenericDesktopPage
		&& (Usage == RCHidUsage::Joystick || Usage == RCHidUsage::Gamepad);
}

bool FRCInputDeviceInfo::IsKnownRadio() const
{
	return VendorId == RCKnownDevice::TBS_VendorId && ProductId == RCKnownDevice::Tango2_ProductId;
}

FString FRCInputDeviceInfo::GetUsageLabel() const
{
	switch (Usage)
	{
	case RCHidUsage::Joystick: return TEXT("Joystick");
	case RCHidUsage::Gamepad:  return TEXT("Gamepad");
	default:                   return FString::Printf(TEXT("Usage 0x%02X"), Usage);
	}
}

FString FRCInputDeviceInfo::GetIdString() const
{
	return FString::Printf(TEXT("%04X:%04X"), VendorId, ProductId);
}

FString FRCInputDeviceInfo::GetDisplayName() const
{
	if (IsKnownRadio())
	{
		// The driver-supplied name for these is usually generic, so prefer the real one.
		return TEXT("TBS Tango 2");
	}
	if (!ProductName.IsEmpty())
	{
		return ProductName;
	}
	return FString::Printf(TEXT("HID device %s"), *GetIdString());
}

// ---------------------------------------------------------------------------------------------
// FRCDeviceRegistry
// ---------------------------------------------------------------------------------------------

FRCDeviceRegistry& FRCDeviceRegistry::Get()
{
	static FRCDeviceRegistry Instance;
	return Instance;
}

void FRCDeviceRegistry::Refresh()
{
	// Remember what was selected so re-enumeration does not silently change the device.
	const bool bHadSelection = Devices.IsValidIndex(SelectedIndex);
	const uint32 PreviousVendor = bHadSelection ? Devices[SelectedIndex].VendorId : 0;
	const uint32 PreviousProduct = bHadSelection ? Devices[SelectedIndex].ProductId : 0;

	EnumerateDevices();
	RebuildGameDeviceList();

	SelectedIndex = INDEX_NONE;
	if (bHadSelection)
	{
		for (int32 Index = 0; Index < Devices.Num(); ++Index)
		{
			if (Devices[Index].VendorId == PreviousVendor && Devices[Index].ProductId == PreviousProduct)
			{
				SelectedIndex = Index;
				break;
			}
		}
	}

	if (SelectedIndex == INDEX_NONE)
	{
		AutoSelect();
	}
}

void FRCDeviceRegistry::EnumerateDevices()
{
	Devices.Reset();

#if PLATFORM_WINDOWS
	UINT NumDevices = 0;
	if (GetRawInputDeviceList(nullptr, &NumDevices, sizeof(RAWINPUTDEVICELIST)) != 0 || NumDevices == 0)
	{
		return;
	}

	TArray<RAWINPUTDEVICELIST> DeviceList;
	DeviceList.SetNumUninitialized(NumDevices);

	const UINT Written = GetRawInputDeviceList(DeviceList.GetData(), &NumDevices, sizeof(RAWINPUTDEVICELIST));
	if (Written == static_cast<UINT>(-1))
	{
		return;
	}

	for (UINT Index = 0; Index < Written; ++Index)
	{
		const RAWINPUTDEVICELIST& Entry = DeviceList[Index];
		if (Entry.dwType != RIM_TYPEHID)
		{
			continue;   // keyboards and mice are not interesting here
		}

		RID_DEVICE_INFO RidInfo;
		FMemory::Memzero(RidInfo);
		RidInfo.cbSize = sizeof(RID_DEVICE_INFO);
		UINT InfoSize = sizeof(RID_DEVICE_INFO);

		if (GetRawInputDeviceInfo(Entry.hDevice, RIDI_DEVICEINFO, &RidInfo, &InfoSize) == static_cast<UINT>(-1))
		{
			continue;
		}

		FRCInputDeviceInfo Info;
		Info.VendorId = RidInfo.hid.dwVendorId;
		Info.ProductId = RidInfo.hid.dwProductId;
		Info.UsagePage = RidInfo.hid.usUsagePage;
		Info.Usage = RidInfo.hid.usUsage;

		// Device interface path, which is also what we open to ask for the product string.
		UINT NameLength = 0;
		if (GetRawInputDeviceInfoW(Entry.hDevice, RIDI_DEVICENAME, nullptr, &NameLength) == 0 && NameLength > 0)
		{
			TArray<WCHAR> NameBuffer;
			NameBuffer.SetNumZeroed(NameLength + 1);
			if (GetRawInputDeviceInfoW(Entry.hDevice, RIDI_DEVICENAME, NameBuffer.GetData(), &NameLength) != static_cast<UINT>(-1))
			{
				Info.DevicePath = FString(NameBuffer.GetData());
			}
		}

		// The product string needs the device opened. Ask for no access rights and full sharing:
		// devices already claimed by another process still answer this.
		if (!Info.DevicePath.IsEmpty())
		{
			HANDLE DeviceHandle = CreateFileW(
				*Info.DevicePath,
				0,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr,
				OPEN_EXISTING,
				0,
				nullptr);

			if (DeviceHandle != INVALID_HANDLE_VALUE)
			{
				WCHAR ProductBuffer[256] = {};
				if (HidD_GetProductString(DeviceHandle, ProductBuffer, sizeof(ProductBuffer)))
				{
					Info.ProductName = FString(ProductBuffer).TrimStartAndEnd();
				}
				CloseHandle(DeviceHandle);
			}
		}

		Devices.Add(MoveTemp(Info));
	}
#endif // PLATFORM_WINDOWS
}

void FRCDeviceRegistry::RebuildGameDeviceList()
{
	GameDeviceIndices.Reset();
	for (int32 Index = 0; Index < Devices.Num(); ++Index)
	{
		if (Devices[Index].IsGameDevice())
		{
			GameDeviceIndices.Add(Index);
		}
	}

	// Known radios first, then gamepads, then joysticks -- so the menu's slot 1 is the thing
	// you most likely want to fly with.
	GameDeviceIndices.Sort([this](const int32& A, const int32& B)
	{
		const FRCInputDeviceInfo& DeviceA = Devices[A];
		const FRCInputDeviceInfo& DeviceB = Devices[B];

		auto Rank = [](const FRCInputDeviceInfo& Device) -> int32
		{
			if (Device.IsKnownRadio()) { return 0; }
			return (Device.Usage == RCHidUsage::Gamepad) ? 1 : 2;
		};

		const int32 RankA = Rank(DeviceA);
		const int32 RankB = Rank(DeviceB);
		return (RankA != RankB) ? (RankA < RankB) : (A < B);
	});
}

const FRCInputDeviceInfo* FRCDeviceRegistry::GetSelectedDevice() const
{
	return Devices.IsValidIndex(SelectedIndex) ? &Devices[SelectedIndex] : nullptr;
}

void FRCDeviceRegistry::AutoSelect()
{
	if (GameDeviceIndices.Num() == 0)
	{
		SelectedIndex = INDEX_NONE;
		StatusMessage = TEXT("no joystick or gamepad devices found");
		return;
	}

	// GameDeviceIndices is already ordered by preference, so slot 0 is the best candidate.
	SelectDevice(GameDeviceIndices[0]);
}

bool FRCDeviceRegistry::SelectGameDevice(int32 MenuSlot)
{
	if (!GameDeviceIndices.IsValidIndex(MenuSlot))
	{
		return false;
	}
	return SelectDevice(GameDeviceIndices[MenuSlot]);
}

void FRCDeviceRegistry::TickRegistration()
{
	if (RegisteredHandle != INDEX_NONE)
	{
		return;
	}

	if (const FRCInputDeviceInfo* Device = GetSelectedDevice())
	{
		ApplyRawInputRegistration(Device->UsagePage, Device->Usage);
	}
}

bool FRCDeviceRegistry::SelectDevice(int32 DeviceIndex)
{
	if (!Devices.IsValidIndex(DeviceIndex))
	{
		return false;
	}

	SelectedIndex = DeviceIndex;
	bLoggedRegistrationFailure = false;   // a new choice deserves a fresh diagnosis
	const FRCInputDeviceInfo& Device = Devices[DeviceIndex];

	UE_LOG(LogFPV, Log, TEXT("Selected input device: %s [%s] %s"),
		*Device.GetDisplayName(), *Device.GetIdString(), *Device.GetUsageLabel());

	return ApplyRawInputRegistration(Device.UsagePage, Device.Usage);
}

bool FRCDeviceRegistry::ApplyRawInputRegistration(uint16 UsagePage, uint16 Usage)
{
#if PLATFORM_WINDOWS
	if (!FRawInputPlugin::IsAvailable())
	{
		StatusMessage = TEXT("RawInput plugin not loaded");
		return false;
	}

	TSharedPtr<IRawInput> RawInput = FRawInputPlugin::Get().GetRawInputDevice();
	if (!RawInput.IsValid())
	{
		// The device is created during Slate startup, so this can be hit if we ask too early.
		StatusMessage = TEXT("RawInput device not created yet");
		return false;
	}

	if (RegisteredHandle != INDEX_NONE && RegisteredUsagePage == UsagePage && RegisteredUsage == Usage)
	{
		StatusMessage = TEXT("already registered");
		return true;
	}

	if (RegisteredHandle != INDEX_NONE)
	{
		RawInput->RemoveRegisteredInputDevice(RegisteredHandle);
		RegisteredHandle = INDEX_NONE;
	}

	RegisteredHandle = RawInput->RegisterInputDevice(
		RCHidUsage::RawInputTypeHID, /*Flags=*/0, Usage, static_cast<int16>(UsagePage), nullptr);

	if (RegisteredHandle == INDEX_NONE)
	{
		StatusMessage = FString::Printf(TEXT("registration FAILED for usage 0x%02X"), Usage);
		if (!bLoggedRegistrationFailure)
		{
			bLoggedRegistrationFailure = true;
			UE_LOG(LogFPV, Warning, TEXT("RawInput registration failed for usage page 0x%02X usage 0x%02X"),
				UsagePage, Usage);
		}
		return false;
	}

	RegisteredUsagePage = UsagePage;
	RegisteredUsage = Usage;
	bLoggedRegistrationFailure = false;
	StatusMessage = FString::Printf(TEXT("registered usage 0x%02X, handle %d"), Usage, RegisteredHandle);

	UE_LOG(LogFPV, Log, TEXT("RawInput registered usage page 0x%02X usage 0x%02X -> handle %d"),
		UsagePage, Usage, RegisteredHandle);
	return true;
#else
	StatusMessage = TEXT("RawInput is Windows-only");
	return false;
#endif
}
