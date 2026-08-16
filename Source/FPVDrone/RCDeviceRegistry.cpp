#include "RCDeviceRegistry.h"
#include "FPVDrone.h"

#if PLATFORM_WINDOWS
#include "RawInput.h"
#include "Windows/AllowWindowsPlatformTypes.h"
	#include <hidsdi.h>
	#include <hidpi.h>
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

void FRCDeviceRegistry::InstallRawDataProbe()
{
#if PLATFORM_WINDOWS
	if (bRawProbeInstalled || !FRawInputPlugin::IsAvailable())
	{
		return;
	}

	TSharedPtr<IRawInput> RawInput = FRawInputPlugin::Get().GetRawInputDevice();
	if (!RawInput.IsValid())
	{
		return;
	}

	// Returning false leaves the packet for the plugin's normal parsing -- this only counts.
	RawInput->GetDataReceivedHandler().BindLambda([](int32 /*DataSize*/, const tagRAWINPUT* Data)
	{
		FRCDeviceRegistry& Registry = FRCDeviceRegistry::Get();
		++Registry.RawPacketCount;

		if (Data && Data->header.dwType == RIM_TYPEHID)
		{
			++Registry.RawHidPacketCount;

			// Resolve the originating device so we can tell the radio apart from any other
			// HID hardware that happens to be chattering.
			if (const FRCInputDeviceInfo* Selected = Registry.GetSelectedDevice())
			{
				RID_DEVICE_INFO RidInfo;
				FMemory::Memzero(RidInfo);
				RidInfo.cbSize = sizeof(RID_DEVICE_INFO);
				UINT InfoSize = sizeof(RID_DEVICE_INFO);

				if (GetRawInputDeviceInfo(Data->header.hDevice, RIDI_DEVICEINFO, &RidInfo, &InfoSize) != static_cast<UINT>(-1)
					&& RidInfo.hid.dwVendorId == Selected->VendorId
					&& RidInfo.hid.dwProductId == Selected->ProductId)
				{
					++Registry.RawSelectedPacketCount;
					Registry.ParseHidReport(Data);
				}
			}
		}
		return false;
	});

	bRawProbeInstalled = true;
	UE_LOG(LogFPV, Log, TEXT("RawInput packet probe installed."));
#endif
}

float FRCDeviceRegistry::GetParsedAxis(int32 AxisIndex) const
{
	return (AxisIndex >= 0 && AxisIndex < MaxParsedAxes) ? ParsedAxes[AxisIndex] : 0.f;
}

void FRCDeviceRegistry::ParseHidReport(const void* RawInputPacket)
{
#if PLATFORM_WINDOWS
	const RAWINPUT* Data = static_cast<const RAWINPUT*>(RawInputPacket);
	if (!Data || Data->data.hid.dwSizeHid == 0)
	{
		return;
	}

	// Preparsed data describes the report layout. Fetched per packet because the buffer is
	// reused; Windows caches it internally so this is cheap.
	UINT PreparsedSize = 0;
	if (GetRawInputDeviceInfo(Data->header.hDevice, RIDI_PREPARSEDDATA, nullptr, &PreparsedSize) != 0 || PreparsedSize == 0)
	{
		ParseError = TEXT("could not size preparsed data");
		return;
	}

	PreparsedBuffer.SetNumUninitialized(PreparsedSize, EAllowShrinking::No);
	if (GetRawInputDeviceInfo(Data->header.hDevice, RIDI_PREPARSEDDATA, PreparsedBuffer.GetData(), &PreparsedSize) == static_cast<UINT>(-1))
	{
		ParseError = TEXT("could not read preparsed data");
		return;
	}

	PHIDP_PREPARSED_DATA Preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(PreparsedBuffer.GetData());

	HIDP_CAPS Caps;
	FMemory::Memzero(Caps);
	if (HidP_GetCaps(Preparsed, &Caps) != HIDP_STATUS_SUCCESS)
	{
		ParseError = TEXT("HidP_GetCaps failed");
		return;
	}

	PCHAR ReportData = const_cast<PCHAR>(reinterpret_cast<const char*>(Data->data.hid.bRawData));
	const ULONG ReportLength = Data->data.hid.dwSizeHid;

	// --- Analog axes ---------------------------------------------------------------------
	if (Caps.NumberInputValueCaps > 0)
	{
		USHORT NumValueCaps = Caps.NumberInputValueCaps;
		ValueCapsBuffer.SetNumUninitialized(NumValueCaps * sizeof(HIDP_VALUE_CAPS), EAllowShrinking::No);
		HIDP_VALUE_CAPS* ValueCaps = reinterpret_cast<HIDP_VALUE_CAPS*>(ValueCapsBuffer.GetData());

		if (HidP_GetValueCaps(HidP_Input, ValueCaps, &NumValueCaps, Preparsed) == HIDP_STATUS_SUCCESS)
		{
			int32 AxisIndex = 0;
			for (USHORT CapIndex = 0; CapIndex < NumValueCaps && AxisIndex < MaxParsedAxes; ++CapIndex)
			{
				const HIDP_VALUE_CAPS& Cap = ValueCaps[CapIndex];

				// A range cap covers several consecutive usages; a non-range cap covers one.
				const USAGE FirstUsage = Cap.IsRange ? Cap.Range.UsageMin : Cap.NotRange.Usage;
				const USAGE LastUsage = Cap.IsRange ? Cap.Range.UsageMax : Cap.NotRange.Usage;

				for (USAGE Usage = FirstUsage; Usage <= LastUsage && AxisIndex < MaxParsedAxes; ++Usage)
				{
					ULONG RawValue = 0;
					if (HidP_GetUsageValue(HidP_Input, Cap.UsagePage, Cap.LinkCollection, Usage,
							&RawValue, Preparsed, ReportData, ReportLength) != HIDP_STATUS_SUCCESS)
					{
						continue;
					}

					// LogicalMax is signed in the header but devices frequently report an
					// unsigned range, which shows up as a negative maximum. Mask to BitSize to
					// recover the real range -- the same trick the engine plugin uses.
					LONG LogicalMin = Cap.LogicalMin;
					LONG LogicalMax = Cap.LogicalMax;
					if (LogicalMax <= LogicalMin)
					{
						const LONG BitMask = (Cap.BitSize >= 32) ? MAX_int32 : ((1 << Cap.BitSize) - 1);
						LogicalMax = LogicalMax & BitMask;
						if (LogicalMax <= LogicalMin)
						{
							LogicalMin = 0;
							LogicalMax = BitMask;
						}
					}

					const float Range = static_cast<float>(LogicalMax - LogicalMin);
					ParsedAxes[AxisIndex] = (Range > 0.f)
						? FMath::Clamp((static_cast<float>(static_cast<LONG>(RawValue)) - LogicalMin) / Range, 0.f, 1.f)
						: 0.f;

					++AxisIndex;
				}
			}

			ParsedAxisCount = AxisIndex;
			bHasParsedReport = true;
			ParseError.Reset();
		}
		else
		{
			ParseError = TEXT("HidP_GetValueCaps failed");
		}
	}

	// --- Buttons -------------------------------------------------------------------------
	ParsedButtons = 0;
	if (Caps.NumberInputButtonCaps > 0)
	{
		USHORT NumButtonCaps = Caps.NumberInputButtonCaps;
		ButtonCapsBuffer.SetNumUninitialized(NumButtonCaps * sizeof(HIDP_BUTTON_CAPS), EAllowShrinking::No);
		HIDP_BUTTON_CAPS* ButtonCaps = reinterpret_cast<HIDP_BUTTON_CAPS*>(ButtonCapsBuffer.GetData());

		if (HidP_GetButtonCaps(HidP_Input, ButtonCaps, &NumButtonCaps, Preparsed) == HIDP_STATUS_SUCCESS
			&& NumButtonCaps > 0)
		{
			const HIDP_BUTTON_CAPS& Cap = ButtonCaps[0];
			const USAGE UsageMin = Cap.IsRange ? Cap.Range.UsageMin : Cap.NotRange.Usage;

			ULONG UsageCount = HidP_MaxUsageListLength(HidP_Input, Cap.UsagePage, Preparsed);
			if (UsageCount > 0)
			{
				UsageListBuffer.SetNumUninitialized(UsageCount, EAllowShrinking::No);
				if (HidP_GetUsages(HidP_Input, Cap.UsagePage, 0, UsageListBuffer.GetData(),
						&UsageCount, Preparsed, ReportData, ReportLength) == HIDP_STATUS_SUCCESS)
				{
					for (ULONG i = 0; i < UsageCount; ++i)
					{
						const int32 BitIndex = UsageListBuffer[i] - UsageMin;
						if (BitIndex >= 0 && BitIndex < 32)
						{
							ParsedButtons |= (1u << BitIndex);
						}
					}
				}
			}
		}
	}
#endif // PLATFORM_WINDOWS
}

void FRCDeviceRegistry::TickRegistration(bool bInputSeen)
{
	InstallRawDataProbe();

	const double Now = FPlatformTime::Seconds();

	// RawInput latches hwndTarget at registration time. Re-register when the window comes
	// forward so the handle tracks whatever is actually being looked at.
	const bool bFocused = FApp::HasFocus();
	const bool bJustGainedFocus = bFocused && !bWasFocused;
	bWasFocused = bFocused;

	if (bJustGainedFocus && RegisteredHandle != INDEX_NONE)
	{
		UE_LOG(LogFPV, Log, TEXT("Window gained focus -- re-registering RawInput device."));
		LastRegisterAttemptTime = Now;
		ForceReRegister();
		return;
	}

	if (RegisteredHandle == INDEX_NONE)
	{
		if (const FRCInputDeviceInfo* Device = GetSelectedDevice())
		{
			LastRegisterAttemptTime = Now;
			ApplyRawInputRegistration(Device->UsagePage, Device->Usage);
		}
		return;
	}

	// Registered, but nothing has ever moved. The most likely explanation is that hwndTarget
	// points at a window that no longer matters (a startup/loading window, say), which produces
	// no error of any kind. Re-register periodically until input actually shows up.
	// Only worth retrying while the selected device's packets are genuinely not reaching us.
	// Once they are, re-registering solves nothing and the fault lies further downstream.
	if (!bInputSeen && bFocused && RawSelectedPacketCount == 0 && (Now - LastRegisterAttemptTime) > 2.0)
	{
		LastRegisterAttemptTime = Now;
		UE_LOG(LogFPV, Log, TEXT("No packets from the selected device yet (total=%lld hid=%lld) -- re-registering."),
			RawPacketCount, RawHidPacketCount);
		ForceReRegister();
	}
}

bool FRCDeviceRegistry::ForceReRegister()
{
	const FRCInputDeviceInfo* Device = GetSelectedDevice();
	if (!Device)
	{
		StatusMessage = TEXT("no device selected");
		return false;
	}

	const uint16 UsagePage = Device->UsagePage;
	const uint16 Usage = Device->Usage;

#if PLATFORM_WINDOWS
	if (RegisteredHandle != INDEX_NONE && FRawInputPlugin::IsAvailable())
	{
		if (TSharedPtr<IRawInput> RawInput = FRawInputPlugin::Get().GetRawInputDevice())
		{
			RawInput->RemoveRegisteredInputDevice(RegisteredHandle);
		}
	}
#endif

	RegisteredHandle = INDEX_NONE;
	RegisteredUsagePage = 0;
	RegisteredUsage = 0;
	bLoggedRegistrationFailure = false;

	return ApplyRawInputRegistration(UsagePage, Usage);
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
