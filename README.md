# Battery Warn

A tiny tray app that warns you when your laptop battery drops below a
threshold you choose.

Windows already nags you at 10% and 5%. This lets you decide — 15%, 25%,
whatever fits how you work — and shows the warning as a proper
notification with a sound you can't miss.

## Install

Grab `BatteryWarn.exe` from the [latest release](../../releases/latest) and
run it. That's it — no installer, no dependencies.

To start it with Windows, drop a shortcut in
`%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup`.

## Using it

A tray icon appears. Right-click it for the menu:

- **Battery: 78% (on battery)** — current state, read-only.
- **Warn at** — pick a preset (6%, 8%, 10%, 12%, 14%, 16%) or **Custom…**.
- **Test notification** — see what a warning looks like.
- **Exit**.

The tray tooltip shows the current percentage and whether you're plugged
in. Double-click the icon to check the battery immediately instead of
waiting for the next poll.

## When it warns

The warning fires once when the battery crosses below your threshold on
battery power. It won't keep nagging while you stay below that line.
Plug in the charger and the state resets, so the next time you unplug
and drop past the threshold it warns again.

Raising or lowering the threshold while the battery is already below it
resets the warning too, so the new value takes effect right away.

## Notes

- Works on Windows 7 and later, 32-bit and 64-bit.
- The warning sound respects Focus Assist / Quiet Hours — you get the
  notification silently, then the sound when the quiet window ends.
- Desktops with no battery get a quiet tooltip that says so, and no
  warnings.
- Everything lives under `HKCU\Software\BatteryWarn`. Delete that key
  and the app starts fresh with the default of 8%.

## Building

Open a Developer Command Prompt and run `build.bat`. Requires Visual
Studio with the C++ workload. A `build.sh` for cross-compiling on macOS
or Linux with Clang and LLD is included too.

## License

MIT.
