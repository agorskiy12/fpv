using UnrealBuildTool;

public class FPVDrone : ModuleRules
{
	public FPVDrone(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"PhysicsCore",
			"Niagara"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// RawInput is Windows-only, and deprecated as of 5.8 -- we depend on it directly so we
		// can register HID usages ourselves rather than relying on the plugin's automatic
		// registration, which never reaches gamepad-class devices when a joystick is attached.
		// hid.lib supplies HidD_GetProductString for readable device names.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// RawInput.h pulls in IInputDeviceModule.h and GenericApplicationMessageHandler.h,
			// which live in these two modules.
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"RawInput",
				"InputDevice",
				"ApplicationCore"
			});
			PublicSystemLibraries.Add("hid.lib");
		}
	}
}
