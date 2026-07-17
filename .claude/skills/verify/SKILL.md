---
name: verify
description: Build voxel-factory, launch it deterministically, and drive it with synthesized input + screenshots to verify a change end-to-end on Windows.
---

# Verifying voxel-factory changes (Windows)

## Build + selftest

Run from the **PowerShell tool** (Git Bash mangles `cmd /c` into `C:\`), env
chained in one cmd process (vcvars does not survive between invocations):

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --preset x64-release && cmake --build --preset x64-release'
.\out\build\x64-Release\bin\voxel-factory.exe --selftest   # exit 0 = pass
```

## Deterministic launch

Stash **all four** pref files so a fresh island generates windowed at
1280x720 with default keybinds (the player's settings.cfg may set
FULLSCREEN=1 / rebound keys):

- `%APPDATA%\BennyThompson\voxel-factory\{save.vxf, save.vxf.bak, settings.cfg, settings.cfg.bak}` → rename aside
- `%APPDATA%\benny\voxel-factory\` (legacy dir) → rename aside, or
  `migrateLegacySave()` re-copies the old save into the empty pref dir

Fresh island = camera at `vg::spawnFeet()` looking −Z at the demo line
(source + miner + generator + belts), starting kit in inventory. Kill the
process when done (skips auto-save), then restore everything in a `finally`.

## Driving input

SendInput with **scancodes** (SDL ignores plain VK). Three hard rules:

1. **Build the INPUT struct inside C#** — a static `Key(ushort scan, bool up)`
   in the Add-Type class. Mutating nested struct fields from PowerShell
   (`$i.ki.wScan = ...`) writes to a copy: SendInput sends scancode-0 events,
   returns success, and the game sees nothing.
2. INPUT must be 40 bytes on x64 (`LayoutKind.Explicit, Size = 40`, union at
   offset 8).
3. Key down and up are **separate SendInput calls ≥ 100 ms apart** (the input
   pump folds same-frame edges); call `SetProcessDPIAware()` before any
   `GetWindowRect` screenshot; verify `GetForegroundWindow()` == the game hwnd
   before every key (tap Alt + `SetForegroundWindow` in a retry loop to take
   focus).

Known-good harness: `Add-Type` a class with `SetProcessDPIAware`,
`SetForegroundWindow`/`GetForegroundWindow`, `GetWindowRect`, a private
40-byte INPUT + `KEYBDINPUT`, and:

```csharp
public static uint Key(ushort scan, bool up) {
    INPUT i = new INPUT();
    i.type = 1;
    i.ki.wScan = scan;
    i.ki.dwFlags = 0x0008u | (up ? 0x0002u : 0u); // SCANCODE | KEYUP
    return SendInput(1, new INPUT[] { i }, 40);
}
```

Screenshots: `System.Drawing` `CopyFromScreen` of the window rect.

Useful scancodes: Esc 0x01, Enter 0x1C, Tab 0x0F, E 0x12, W 0x11, S 0x1F,
F1 0x3B, F3 0x3D, F5 0x3F, digits 1-9 0x02-0x0A, 0 0x0B.

## Flows worth driving

- Spawn view screenshot: exercises terrain/machine/source rendering, atlas
  tiles, hotbar icons, hearts.
- `E` crafting menu: every hand recipe + names + INVENTORY grid icons.
- `Tab` inventory overlay: owned grid + hotbar assignments.
- `Esc` chain: closes topmost overlay; with nothing open, pause menu
  (RESUME/SETTINGS/SAVE GAME/SAVE AND QUIT). Quit = Esc, S, S, Enter.
- Mouse look = relative `MOUSEEVENTF_MOVE` in ~15-count steps. Counts-per-degree
  drifts between sessions (seen 3.75 and 7.5) — calibrate from an aim screenshot
  in-run, don't trust a constant. Overlay clicks = `SetCursorPos` in screen
  coords (client is unscaled at 100%).
- Structure GUI drives as single-purpose passes (launch → act → shot → kill):
  after a missed RMB, Esc opens the pause menu instead of closing a panel and a
  blind sequence can't recover. Kill between passes; it also skips the auto-save.

## Gotchas

- The game may already be running (the user plays on this machine) — check
  `Get-Process voxel-factory` first and never steal focus from a live session;
  "mystery" exits mid-drive are usually the user closing the window.
- Don't toggle fullscreen mid-drive: the mode change drops foreground and
  later keys land elsewhere. Test fullscreen in its own pass.
