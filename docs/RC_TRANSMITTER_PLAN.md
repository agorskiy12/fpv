# Supporting a TBS Tango 2 over USB

Plan for flying the sim with a TBS Tango 2 in USB joystick mode. Verified against the UE 5.8
install on this machine.

## The problem

The current gamepad bindings use `EKeys::Gamepad_LeftY` and friends. Those come from
**XInput**, which only understands Xbox-class controllers — two sticks, two triggers, a fixed
button set.

A radio in USB joystick mode is a **generic HID / DirectInput** device presenting 4–16+ axes.
XInput cannot see it, so the existing gamepad path will not work with the Tango 2, and no
amount of remapping `Gamepad_*` keys will fix it. A different input backend is required.

## The fix: Epic's RawInput plugin

Ships with the engine, already built here:

```
C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\Experimental\RawInput\
  → UnrealEditor-RawInput.dll
```

What it gives us, confirmed by reading the plugin source:

- **24 analog axes** — `GenericUSBController_Axis1` … `Axis24` (`RawInputWindows.cpp:280-303`).
  More than enough for a 16-channel radio.
- **Buttons** — `GenericUSBController_Button1` upward.
- These are real `FKey`s, so they drop straight into the Enhanced Input mapping context the
  pawn already builds in C++. No architectural change to the input layer.
- Devices are matched by Vendor/Product ID as hex strings, and **`0` is a wildcard** —
  `VendorID == 0 || VendorID == DeviceEntry.DeviceData.VendorID` (`RawInputWindows.cpp:244`).
  So we can match any HID device while bringing a new radio up, then pin it down later.

Per-axis settings (`RawInputSettings.h`): `bEnabled`, `Key`, `bInverted`, `bGamepadStick`
(scale −1..1 instead of 0..1), and `Offset`.

**Design decision: leave `bGamepadStick` off and do our own normalisation.** The plugin's
invert/offset is a fixed, compile-time-ish transform. A transmitter needs runtime calibration —
endpoints vary per radio and per model, centre is rarely exactly mid-travel, and channel order
differs between firmware. Our own layer can be calibrated in-game and saved.

### RawInput is deprecated in 5.8

Surfaced by UnrealBuildTool the first time the plugin was enabled:

```
Project 'FPVDrone' depends on plugin 'RawInput' which was deprecated in 5.8
and will soon be removed. Please update your dependencies.
```

`RawInput.uplugin` carries `"DeprecatedEngineVersion": "5.8"`. It still builds and works today,
but it is on the way out and should not be the long-term foundation.

### The successor: GameInput

`Engine/Plugins/GameInputWindows` — *"a next-generation input API that exposes input devices of
all kinds through a single consistent interface."* Microsoft's GameInput, replacing both XInput
and DirectInput. The `GameInputRedist` component is already installed on this machine alongside
the engine.

It differs from RawInput in an important way. RawInput hands you 24 anonymous numbered axes.
GameInput instead exposes *semantic* keys for recognised device classes —
`FlightStick_Roll`, `FlightStick_Pitch`, `FlightStick_Yaw`, `FlightStick_Throttle` — and, for
devices it treats as generic controllers, a configurable
`ControllerAxisMappingData` (`TMap<uint32, FGameInputControllerAxisData>`) that maps axis
indices onto key names you choose. There is no fixed generic-axis key set to read.

Which path applies depends on how GameInput classifies a Tango 2, and that is not knowable
without the hardware. It is also beta and not enabled by default.

### Decision: watch both, decide with evidence

Both plugins are enabled and **the channel monitor samples both key sets at once** — RawInput's
`GenericUSBController_Axis1..24` and GameInput's four `FlightStick_*` axes, listed together.

Connect the radio, move a stick, and whichever set responds is the one to build Phases 2–5 on.
The named GameInput rows are always displayed even when idle, because a row sitting at zero is
itself the answer to "does GameInput see this device?".

This costs almost nothing now and removes the main open risk in the plan.

---

## Phase 1 — Plumbing and a channel monitor

Goal: see live numbers from the device on screen. Nothing else can be debugged until this works.

**Status: done.**

1. Add `RawInput` to the `Plugins` array in `FPVDrone.uproject`.
2. **Add no device configuration at all.**

   This is the opposite of what an earlier draft of this plan said, and the reason is in
   `RawInputWindows.cpp:238-278`. `SetupBindings` walks `DeviceConfigurations` looking for a
   VID/PID match; on a hit it binds *only* the axes listed in that entry and `break`s. The
   automatic 24-axis binding at line 280 runs **only when nothing matched**.

   Since VID/PID `0` matches everything, adding a wildcard entry would suppress the defaults and
   bind fewer axes than doing nothing. Leaving `DeviceConfigurations` empty gives all 24 axes
   bound automatically. A specific entry becomes worthwhile later, once the Tango 2's real
   VID/PID is known and per-axis inversion is wanted.

3. Build a **channel monitor** overlay in `AFPVHUD`: one labelled bar plus raw numeric value per
   axis, toggled by a console variable (`fpv.ShowChannels 1`).

   This is the highest-value item in the whole plan. Axis ordering is radio- and
   firmware-dependent, so mapping channels without a live readout is guesswork. Build it first
   and every later phase gets easier.

**Exit criterion:** wiggle a stick, watch the right bar move.

## Phase 2 — Calibration layer

Goal: turn raw driver values into clean −1..1 channels.

4. `FRCChannel` struct: `SourceAxisIndex`, `RawMin`, `RawCenter`, `RawMax`, `bInvert`,
   `Deadband`.
5. Piecewise normalisation — map `RawMin→RawCenter` onto `−1→0` and `RawCenter→RawMax` onto
   `0→+1` separately. Handles asymmetric travel, which is normal on real gimbals, and avoids the
   drift you get from assuming centre is the midpoint.
6. Store the profile on a `UFPVInputSettings : UDeveloperSettings` with `config` properties, so
   it persists to ini and is editable in Project Settings.
7. Calibration flow, console-command driven at first:
   - `fpv.CalStart` — begin capturing min/max, prompt "move every stick and switch to both extremes"
   - `fpv.CalCenter` — prompt "centre the sticks", capture centre
   - `fpv.CalSave` — write to config

   A guided on-screen wizard is nicer but is polish; the commands are enough to fly.

**Exit criterion:** all four channels read −1.00 / 0.00 / +1.00 at the stops and centre.

## Phase 3 — Wire into the flight model

8. Route `AFPVDronePawn`'s stick values through the calibrated channels instead of reading the
   Enhanced Input axis directly.
9. **Throttle stays direct-mapped.** The existing `(raw + 1) * 0.5` is *correct* for a real
   transmitter — its throttle stick does not self-centre, so bottom-of-throw genuinely means
   zero. The awkward "50% when idle" behaviour is purely an artefact of keyboards and
   self-centring gamepads, and disappears with a real radio.
10. Set the Enhanced Input dead zone to **0** for RC axes. The radio applies its own expo and
    deadband; stacking a second one softens the centre twice.
11. Keep the keyboard and XInput paths working as a fallback, chosen by an input-source enum.

**Exit criterion:** flies on the radio, keyboard still works.

## Phase 4 — Switches and arming

12. Map AUX channels. Note that radio switches usually arrive as **axes**, not buttons —
    a 3-position switch reads as roughly −1 / 0 / +1 on its own channel.
13. **Arm / disarm on AUX1.** Motors produce no thrust until armed.

    This is worth doing for its own sake: it is how a real quad behaves, and it cleanly solves
    the "drone climbs on its own when you are not touching anything" problem rather than
    working around it.
14. AUX2 → reset/rescue, replacing the `R` key.

## Phase 5 — Fidelity

15. **Rates source toggle.** Real setups usually apply rates and expo *in the radio*. If yours
    does, the sim's Betaflight rate curve is being applied twice. Add a switch: either the sim
    owns the curve (radio sends linear), or the radio owns it (sim passes through linearly).
16. Multiple saved profiles, one per radio.
17. Optional: 16-channel support, telemetry-style OSD.

---

## Risks

| Risk | Mitigation |
|---|---|
| RawInput is marked Experimental | Stable in practice; isolate behind our own channel abstraction so a swap is contained |
| PIE focus — RawInput may not receive input unless the viewport has focus | Test in **Standalone Game**, not just PIE |
| Radio enumerates as several HID interfaces | Start with wildcard VID/PID, then pin the exact one via the monitor |
| Axis order varies by firmware | Exactly what the Phase 1 monitor is for |
| EdgeTX lets you remap channel→axis *in the radio* | Set the radio to a known default and document it, so calibration is not chasing a moving target |

---

## TBS Tango 2 specifics

The Tango 2 runs TBS's OpenTX-derived firmware (FreedomTX), so it behaves like an OpenTX /
EdgeTX radio for USB purposes rather than like a game controller.

### Getting it into joystick mode

1. **Power the radio on first.** Plugging in USB while it is off may only charge it — it will
   not enumerate as an input device.
2. Connect USB-C. The radio should offer a mode menu: **USB Storage / USB Serial / USB
   Joystick** — choose **Joystick**.
3. If no menu appears, set it explicitly: **Radio Setup → USB Mode → Joystick** (rather than
   `Ask`).

### Things that will bite

- **The axes follow the mixer, not the gimbals.** In joystick mode the radio outputs its
  *channel* values. If the active model has no outputs configured, the sticks will move and the
  axes will not. Make sure a model is selected and its channels 1–4 respond in the radio's own
  channel monitor screen before blaming the sim.
- **Channel order is not guaranteed.** OpenTX-derived firmware is usually **AETR**
  (roll, pitch, throttle, yaw), but TAER exists in the wild and the order is editable on the
  radio. Do not hard-code it — this is what the Phase 1 monitor is for.
- **Throttle warning / trims.** A radio refusing to leave its startup warning screen will not
  send useful channel data. Clear warnings before testing.
- **Rates and expo are probably already applied in the radio.** See Phase 5 — running the
  sim's Betaflight curve on top of the radio's curve double-applies expo and the sticks will
  feel dead around centre.

### Unknown until it is plugged in

The Tango 2's USB **vendor and product ID**, and how many axes it exposes. OpenTX-based radios
commonly enumerate under the pid.codes vendor ID, but this is worth confirming rather than
assuming — TBS ships its own firmware build.

Discovering it is a one-liner once the radio is connected and in joystick mode:

```bash
powershell -Command "Get-PnpDevice -Class HIDClass -Status OK | Where-Object { $_.FriendlyName -match 'game controller|joystick' } | Select-Object FriendlyName, InstanceId"
```

The `VID_xxxx&PID_xxxx` in the returned `InstanceId` is what goes into the RawInput device
configuration. Until then, the wildcard config from Phase 1 is enough to get moving.

## Suggested order

Phase 1 alone is worth doing immediately — it is small, and it turns every later step from
guesswork into reading a number off the screen. It can be built before the radio is connected;
the wildcard VID/PID means it will pick the Tango 2 up as soon as it appears.
