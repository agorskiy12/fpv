# FPV Drone

A first-person-view quadcopter simulator for Unreal Engine 5. Acro-mode flight physics, a
gate-based race course, and lap timing — all in C++, with no Blueprint or imported content
required to fly.

## What "acro mode" means

There is no self-levelling. The sticks command angular **rates**, not angles. Let go and the
drone holds whatever attitude it happens to be in — upside down, knife-edge, whatever. That is
how real FPV quads fly, and it is the entire reason the genre feels the way it does.

The flight model, per tick:

| Stage | What happens |
|---|---|
| Rate curve | Stick position runs through Betaflight's `applyActualRates` — centre sensitivity, max rate, expo |
| Rate controller | PID against measured body angular velocity, producing angular acceleration |
| Thrust | Throttle → acceleration along the airframe's local up axis, with motor spool-up lag |
| Drag | Quadratic, per body axis, so the quad is draggier sideways than nose-on |

Torque and thrust are applied as accelerations (`bAccelChange = true`), so handling does not
change when you swap the placeholder cube for a real quad model.

## Controls

Mode 2, the standard FPV layout.

| Input | Gamepad | Keyboard |
|---|---|---|
| Throttle | Left stick ↕ | `W` / `S` |
| Yaw | Left stick ↔ | `A` / `D` |
| Pitch | Right stick ↕ | `↑` / `↓` |
| Roll | Right stick ↔ | `←` / `→` |
| Reset to start | `Y` / `Triangle` | `R` |

A gamepad works. A real transmitter in USB joystick mode works much better — its throttle stick
does not self-centre, which is what the `(raw + 1) / 2` throttle mapping expects.

Pitch follows the real convention: **stick forward drops the nose**. Uncheck
`bInvertPitchStick` on the pawn if you want it the other way.

## Getting it running

### 1. Build environment

Verified on this machine, against `UE_5.8/Engine/Config/Windows/Windows_SDK.json`:

| Component | Version | Notes |
|---|---|---|
| Unreal Engine | **5.8** | `C:\Program Files\Epic Games\UE_5.8` |
| Visual Studio Build Tools | 18.9 (2026) | UE 5.8 needs 18.0+ for its VS2026 path |
| MSVC | 14.51.36256 | Above minimum 14.38.33130, not in `BannedVisualCppVersions` |
| MSVC (alternate) | 14.50.35717 | Epic's `VC.14.50.18.0` component |
| Windows SDK | 10.0.26100 | Inside the accepted 19041 → 10.9.x range |
| NETFXSDK | 4.8 | Required — UBT's `SwarmInterface` fails without it |
| .NET | bundled 10.0 | UE ships its own; no system `dotnet` needed |

`EngineAssociation` in `FPVDrone.uproject` is set to `5.8`. On a different engine version,
change it or right-click the `.uproject` and pick **Switch Unreal Engine version**.

Two things worth recording, because neither is obvious from Epic's docs:

- **MSVC 14.51 works.** UBT warns *"newer than latest preferred version 14.50.35717, please use
  caution"* — a warning, not a rejection. Adding an older toolset is not required.
- **The .NET Framework SDK is a hard requirement**, and the *targeting pack* alone is not
  enough. `Microsoft.Net.Component.4.6.2.TargetingPack` gives you reference assemblies but no
  `Windows Kits\NETFXSDK` directory, which is what UBT actually probes for. You need
  `Microsoft.Net.Component.4.8.SDK`:

```bash
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe" modify --installPath "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools" --add Microsoft.Net.Component.4.8.SDK --quiet --norestart
```

Note the VS 18 installer no longer accepts `--wait`; passing it fails with exit code 87.

### 2. Build

Close the Unreal Editor first — Live Coding holds the module binaries and UBT will refuse with
*"Unable to build while Live Coding is active."*

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" FPVDroneEditor Win64 Development -Project="C:\Users\jsdev\Desktop\work\fpv\FPVDrone.uproject" -WaitMutex
```

Or double-click the `.uproject` and let the editor offer to rebuild the module.

### 3. Make a level

The project ships no maps — `.umap` files are binary and have to be made in the editor.

1. **File → New Level → Basic** (gives you a floor, sky, and light)
2. Save it as `Content/Maps/RaceTrack`
3. Drop in a **Player Start** somewhere above the floor — that is where the drone spawns
4. **Edit → Project Settings → Maps & Modes**, set both editor and game default map to `RaceTrack`

The GameMode is already wired up in `Config/DefaultEngine.ini`, so the drone pawn and HUD come
up automatically. Press Play and you can fly.

### 4. Build a course

Drag `RaceGate` actors from the Place Actors panel into the level. For each one set **GateIndex**
— 0, 1, 2, … in the order you want them flown. Gate 0 is the start/finish line.

- Rotate a gate so its **local +X** points the way you fly through it
- `GateSize` and `FrameThickness` reshape the frame live in the viewport
- The frame bars are solid, so clipping one costs you the run
- Gates hit out of order are ignored — you have to go back for the one you missed

Timing starts the first time you cross gate 0, so you get a flying start. Each later crossing of
gate 0 closes a lap.

## Tuning

Select the drone and open the **Drone** categories in the Details panel. Everything is live:

| Property | Default | Effect |
|---|---|---|
| `MaxRates` | 800/800/600 °/s | Rotation rate at full stick |
| `CenterSensitivity` | 200 °/s | Finer control near stick centre |
| `RateExpo` | 0.54 | Softens the middle of the throw |
| `PID_P` / `PID_I` / `PID_D` | 0.25 / 0.05 / 0.004 | Rate loop response. Raise P for sharper, add D if it overshoots |
| `ThrustToWeightRatio` | 3.6 | Acceleration. 2.0 cinematic, 3.0 freestyle, 3.6 light race |
| `MotorResponseTime` | 0.06 s | Throttle lag |
| `DragCoefficients` | 0.0075/0.015/0.022 | **Top speed.** Terminal ≈ √(9.8 / coefficient) m/s — currently ~130 km/h nose-on |
| `CameraTiltDegrees` | 25° | More tilt = faster level flight |

## Known first-flight check

UE's `FRotator` convention and its physics angular-velocity convention do not agree on every
axis. The `bInvertRollAxis` / `bInvertPitchAxis` / `bInvertYawAxis` flags on the pawn map pilot
convention onto the physics frame, and the defaults are a best-effort derivation that has not
been verified against a running engine — there isn't one on this machine yet.

**On your first flight, check each axis.** If one responds backwards, tick the matching box.
That is the intended fix, not a code change.

## Where to take it next

- Swap the cube for a real quad mesh, and the Canvas HUD for a UMG widget
- Move force application into a physics substep callback, so the rate loop runs at the full
  240 Hz instead of once per frame
- Colour the next gate differently — needs a material with a colour parameter
- Prop wash, ground effect, battery sag
- Ghost replay of your best lap

## Layout

```
FPVDrone.uproject          plugins: EnhancedInput, RawInput, GameInputWindows
Config/
  DefaultEngine.ini        GameMode, Enhanced Input classes, physics substepping
  DefaultGame.ini
  DefaultInput.ini         documentation only — bindings are built in C++
docs/
  RC_TRANSMITTER_PLAN.md   phased plan for TBS Tango 2 support
Source/
  FPVDrone.Target.cs
  FPVDroneEditor.Target.cs
  FPVDrone/
    FPVDrone.Build.cs
    FPVDrone.h/.cpp          module + log category
    FPVDronePawn.h/.cpp      flight model, input, camera
    RaceGate.h/.cpp          gate frame + pass trigger
    FPVGameMode.h/.cpp       gate ordering and lap timing
    FPVHUD.h/.cpp            Canvas HUD, channel monitor, device picker, wizard
    RCChannelMonitor.h/.cpp  RC axis sampling and range tracking
    RCDeviceRegistry.h/.cpp  HID enumeration, usage registration, report decoding
    RCChannelMapping.h/.cpp  channel assignment, calibration, persistence
```

## Console reference

| Command | Purpose |
|---|---|
| `fpv.ShowChannels 1` | Channel monitor overlay (`2` for all 28 rows) |
| `fpv.ShowDevices 1` | Device picker; number keys select |
| `fpv.Calibrate` | Channel assignment wizard |
| `fpv.SetChannel yaw 5` | Assign a channel to an axis directly |
| `fpv.InvertChannel pitch 1` | Invert a channel |
| `fpv.Tango2Defaults` | Restore throttle 8, yaw 5, roll 7, pitch 6 |
| `fpv.LogChannels 1` | Log axis values, focus and registration state |
| `fpv.RefreshDevices` | Re-enumerate after plugging hardware in |
| `fpv.ReRegisterDevice` | Rebind RawInput to the current window |
| `fpv.ResetChannelRanges` | Clear observed min/max |

---

# Changelog

## Unreleased — TBS Tango 2 flying

The radio now drives the sim. Getting there took three separate faults, only one of which was
the one originally suspected.

### Added

- **`FRCDeviceRegistry`** — enumerates HID devices with `GetRawInputDeviceList`, reads product
  names via `HidD_GetProductString`, auto-selects a known radio (then any gamepad, then any
  joystick), and registers the chosen device's HID usage with RawInput explicitly.
- **Device picker overlay** — `fpv.ShowDevices`, number keys `1`–`9` to select, plus
  `fpv.SelectDevice` and `fpv.RefreshDevices`.
- **Direct HID report decoding** — `HidP_GetValueCaps` / `HidP_GetUsageValue`, normalising each
  axis against its own logical range and handling both range and non-range usage caps.
- **`FRCChannelMapping`** — assigns raw axes to throttle/roll/pitch/yaw, normalises them, and
  persists to config.
- **Calibration wizard** — `fpv.Calibrate` prompts for each stick in turn and assigns whichever
  axis actually moves. Manual overrides: `fpv.SetChannel`, `fpv.InvertChannel`,
  `fpv.Tango2Defaults`.
- **Flight model wiring** — the pawn takes stick input from a calibrated transmitter when one is
  present, falling back to keyboard and gamepad otherwise.
- **Diagnostics** — per-type raw packet counters, `fpv.LogChannels` (logs axis values alongside
  focus and registration state), `fpv.ReRegisterDevice`.
- **Mouse cursor released by default** (`bReleaseMouseCursor`). A drone sim never uses the mouse,
  and capturing it hides whether the window has focus — which RawInput depends on.

### The three faults

1. **RawInput never subscribed to the radio.** Its startup path registers HID usage `0x04`
   (Joystick) and only falls back to `0x05` (Gamepad) if that call *fails*
   (`RawInputWindows.cpp:97-111`). A Tango 2 is usage `0x05`, so any attached joystick made the
   first call succeed and silently hid the radio. Fixed by setting `bRegisterDefaultDevice=False`
   and registering the selected device's usage directly.

2. **Registration lost a startup race.** RawInput creates its input device during Slate startup,
   *after* the first HUD draw, so a single attempt always failed with "device not created yet".
   Now retried every frame until it takes.

3. **RawInput's parser never ran.** This was the real one. Instrumenting the raw `WM_INPUT`
   delegate showed `pkt == hid == mine` — the Tango 2 was streaming into the process at its full
   125 Hz the entire time. But `ParseInputData` logs a warning on every parse failure and there
   were none: it was never being *entered*. Packets die at the device match in `ProcessMessage`
   (`RawInputWindows.cpp:556-573`) before parsing is attempted.

   The engine plugin cannot be patched and is deprecated anyway, so reports are now decoded
   directly. RawInput is used for exactly one thing: the `WM_INPUT` subscription.

### Notes

- **Tango 2 channel order is neither AETR nor TAER** — throttle 8, yaw 5, roll 7, pitch 6.
  Confirmed against the hardware. An attempt to infer it from a timed stick sweep was ambiguous
  (two axes moved within one sample of each other), which is why assignment is now done by
  moving one stick at a time rather than by inference.
- **Centre is sampled separately from the endpoints.** Gimbal travel is rarely symmetric, and
  treating the midpoint of min and max as centre produces drift you feel constantly at rest.
- **Diagnosing this needed the log, not the screen.** `fpv.LogChannels` exists because reasoning
  about what the overlay *might* be showing produced two wrong theories in a row; printing focus,
  registration state and packet counts settled it in one run.

## 0.2.0 — RC transmitter groundwork

Phase 1 of [docs/RC_TRANSMITTER_PLAN.md](docs/RC_TRANSMITTER_PLAN.md): make it possible to
discover how a real radio presents itself, before writing any code that depends on the answer.

### Added

- **`FRCChannelMonitor`** (`Source/FPVDrone/RCChannelMonitor.h/.cpp`). Samples 28 analog axes
  and 16 buttons per frame via `APlayerController::GetInputAnalogKeyState` / `IsInputKeyDown`,
  and records the min/max range each axis has covered.
  - Watches **two input backends at once**: RawInput's `GenericUSBController_Axis1..24` and
    GameInput's `FlightStick_Roll/Pitch/Yaw/Throttle`.
  - Tracks the most recently moved axis — this is the mechanism for identifying which physical
    stick maps to which channel.
  - Keys are constructed by name rather than by linking either plugin's module, so the module
    compiles and runs with either plugin disabled; absent a backend, its axes simply read zero.
  - Ranges seed from the first sample rather than from a sentinel, so the readout stays honest
    before any input arrives.
- **Channel monitor overlay** (`AFPVHUD::DrawChannelMonitor`). Per axis: a live position marker
  on a fixed −1…+1 track, a band showing the observed range, the numeric value, and the
  recorded endpoints. Includes a button row and a warning line when nothing has ever moved.
  - The track is fixed to −1…+1 rather than scaled to the observed range, so a 0..1 axis
    visibly occupies only the right-hand half — the signal format is readable at a glance.
  - Drawn before the pawn check in `DrawHUD`, so it still works if possession fails.
- **Console commands**
  - `fpv.ShowChannels` — `0` off, `1` axes 1–8 plus anything that moved, `2` all 28 rows.
  - `fpv.ResetChannelRanges` — clear the observed min/max.
- **Plugins enabled**: `RawInput`, `GameInputWindows`.
- **`docs/RC_TRANSMITTER_PLAN.md`** — five-phase plan covering plumbing, calibration, flight
  model integration, arming/AUX switches, and fidelity.

### Notes

- **RawInput is deprecated in UE 5.8.** `RawInput.uplugin` carries
  `"DeprecatedEngineVersion": "5.8"`, and UBT warns it "will soon be removed". It still builds
  and works. GameInput (`Engine/Plugins/GameInputWindows`) is the successor — Microsoft's
  replacement for both XInput and DirectInput — but is beta and off by default.

  Rather than guess which one will see a Tango 2, both are enabled and the monitor samples both.
  Whichever responds when the radio is connected is the one to build on.

- **Do not add a wildcard RawInput device configuration.** Per `RawInputWindows.cpp:238-278`,
  `SetupBindings` binds only the axes listed in the first matching `DeviceConfigurations` entry
  and then `break`s; the automatic 24-axis binding runs *only when nothing matched*. Since
  VID/PID `0` matches everything, a wildcard entry binds **fewer** axes than having no
  configuration at all.

- **XInput cannot see RC transmitters.** The existing `EKeys::Gamepad_*` bindings come from
  XInput, which only handles Xbox-class devices. This is why a separate backend is needed at
  all, and why no amount of remapping `Gamepad_*` would have worked.

---

## 0.1.0 — Initial project

### Added

- **Project scaffold** — `.uproject`, game and editor targets, module rules, and a primary game
  module with a `LogFPV` category. Targets use `BuildSettingsVersion.Latest` and
  `EngineIncludeOrderVersion.Latest` to stay version-agnostic.

- **`AFPVDronePawn`** — acro-mode ("rate mode") quadcopter flight model. No self-levelling;
  sticks command angular rates, not angles.
  - Betaflight's `applyActualRates` ported directly, giving real centre-sensitivity, max-rate
    and expo semantics.
  - PID rate controller against measured body angular velocity.
  - Thrust along the airframe's local up axis with exponential motor spool-up lag.
  - Quadratic aerodynamic drag, per body axis, so the quad is draggier sideways than nose-on.
  - Torque and thrust applied as accelerations (`bAccelChange = true`), making handling
    independent of the mesh's mass and inertia tensor.
  - Rigidly mounted camera with configurable uptilt and a 120° FOV.
  - `bInvertRoll/Pitch/YawAxis` flags to reconcile UE's `FRotator` convention with its physics
    angular-velocity convention.

- **Input built entirely in C++** — `UInputAction` and `UInputMappingContext` objects are
  constructed at runtime, so the project needs no Input Action assets and no Blueprint wiring.
  Mode 2 gamepad layout plus a keyboard fallback.

- **`ARaceGate`** — gate frame assembled from four scaled engine cubes in the constructor and
  rebuilt in `OnConstruction`, so courses can be laid out with no gate mesh to model or import.
  Solid frame bars, thin overlap trigger sized to resist tunnelling at speed.

- **`AFPVGameMode`** — finds gates in the level, sorts them by `GateIndex`, enforces running
  order, and times laps. Flying start: the clock begins on the first crossing of gate 0.

- **`AFPVHUD`** — Canvas-drawn HUD (throttle bar with hover reference, speed, altitude, lap and
  best times, next-gate bracket with distance, crosshair). Deliberately C++ rather than UMG so
  the project has no asset dependencies.

- **Config** — GameMode wired globally, Enhanced Input classes registered, and physics
  substepping at 240 Hz (a quad can exceed 800°/s, which is over 13° per frame at 60 Hz —
  enough to make the rate loop visibly wobble).

### Changed

- `EngineAssociation` `5.6` → `5.8`, after finding UE 5.8 installed rather than 5.6.

### Build environment

Resolved while getting the first compile through:

- **Installed** `Microsoft.Net.Component.4.8.SDK` into VS Build Tools 2026. This was the only
  hard blocker — UBT failed with *"Could not find NetFxSDK install dir"*. The .NET Framework
  **targeting pack alone is not sufficient**: it provides reference assemblies but no
  `Windows Kits\NETFXSDK` directory, which is what UBT actually probes for.
- **Installed** `Microsoft.VisualStudio.Component.VC.14.50.18.0.x86.x64` + `.ATL`, Epic's
  preferred toolset. UBT now selects MSVC **14.50.35737** over the pre-existing 14.51.36256.
- MSVC 14.51 was in fact usable — above UE's minimum (14.38.33130) and outside its
  `BannedVisualCppVersions` — producing only a *"newer than latest preferred version"* warning.
  Adding 14.50 removed that warning.
- The VS 18 installer no longer accepts `--wait`; passing it fails with exit code 87.

## Verification status

| | |
|---|---|
| Game and editor targets | **Build clean** — zero warnings, zero errors |
| TBS Tango 2 input | **Working** — detected, decoded, mapped, driving the flight model |
| Channel monitor and device picker | **Working** — verified on hardware |
| Flight model behaviour | **Flies.** Not yet tuned against real-world feel |
| Physics axis sign mapping | **Unverified** — derived by hand, see *Known first-flight check*. An axis responding backwards is expected here first |
| Channel endpoints | **Defaults only.** Full 0..1 assumed; axis 6 was observed reaching only 0.153–0.863, so run `fpv.Calibrate` for full deflection |
| Race gates and lap timing | **Never exercised** — no gates placed in a level yet |
