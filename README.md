# PowerRefreshSwitcher

Small Win32 background utility for laptops:

- on battery: sets the primary display to 60 Hz and enables Windows Energy Saver;
- on AC power: sets the primary display to 144 Hz and disables Windows Energy Saver;
- waits for Windows power-source notifications, so it does not poll in a loop.

## Build

Install Visual Studio Build Tools with the **Desktop development with C++** workload first.

Then double-click `build-msvc.bat` or run:

```bat
build-msvc.bat
```

The result is `PowerRefreshSwitcher.exe`.

## Autostart

Recommended:

```powershell
powershell -ExecutionPolicy Bypass -File .\install-startup.ps1
```

Manual Regedit variant:

1. Open `install-startup.reg.template`.
2. Replace `C:\\FULL\\PATH\\TO\\PowerRefreshSwitcher.exe` with the real full path to the compiled exe.
3. Import the edited `.reg` file.

The registry key is:

```text
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
```

## Uninstall Autostart

```powershell
powershell -ExecutionPolicy Bypass -File .\remove-startup.ps1
```

Then stop `PowerRefreshSwitcher.exe` from Task Manager or reboot.

## Settings

Edit these constants in `PowerRefreshSwitcher.cpp`, then rebuild:

```cpp
constexpr DWORD kBatteryRefreshHz = 60;
constexpr DWORD kAcRefreshHz = 144;
constexpr bool kPrimaryDisplayOnly = true;
```

`kPrimaryDisplayOnly = true` changes only the primary display. Set it to `false` if every attached desktop display should be switched.

## Notes

On Windows 11 the utility changes the current scheme's Energy Saver threshold/policy:

- battery: threshold `100`, policy `Aggressive`;
- AC power: threshold `0`, policy `User`.

It also uses `PowerSetUserConfiguredDCPowerMode` and `PowerSetUserConfiguredACPowerMode` for the Windows power mode slider. If those functions are unavailable, it falls back to switching the active power scheme to Power Saver or High Performance.
