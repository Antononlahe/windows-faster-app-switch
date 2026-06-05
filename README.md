# WinSwitch

A fast, keyboard-driven window switcher for Windows — a Spotlight/Alfred-style
command palette for jumping between your open windows. Press a hotkey, start
typing, and hit <kbd>Enter</kbd> to switch.

It's a single ~260-line C++ Win32 program with **no dependencies** beyond the
Windows SDK, and it compiles to a tiny standalone `.exe`.

![hotkey: Ctrl+Alt+Space](https://img.shields.io/badge/hotkey-Ctrl%2BAlt%2BSpace-blue)

## Features

- **Global hotkey** — <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Space</kbd> pops up
  the palette from anywhere.
- **Fuzzy search** — type part of a window title; results are ranked by
  substring match first, then subsequence (fuzzy) match, so `ch` finds
  *Chrome* and `vsc` finds *Visual Studio Code*.
- **Pure keyboard** — <kbd>↑</kbd>/<kbd>↓</kbd> to move,
  <kbd>Enter</kbd> to switch, <kbd>Esc</kbd> to dismiss. Mouse double-click
  works too.
- **Smart window list** — only shows real "Alt-Tab" windows (skips tool
  windows, cloaked/virtual-desktop windows, and untitled windows).
- **Reliable foreground switching** — uses the `AttachThreadInput` trick to
  reliably bring the chosen window to the front, even from a background app.
- **System tray icon** — runs quietly in the notification area; left-click to
  open the palette, right-click for a menu to open it or **Exit**.
- **Lightweight** — no installer, no background services, no telemetry. Just
  one small executable.

## Usage

1. Run `winswitch.exe`. It runs in the background with an icon in the system
   tray (notification area).
2. Press <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Space</kbd> (or left-click the
   tray icon).
3. Type to filter your open windows.
4. Use <kbd>↑</kbd>/<kbd>↓</kbd> to select and <kbd>Enter</kbd> to switch.
   Press <kbd>Esc</kbd> (or click away) to dismiss without switching.

**To quit**, right-click the tray icon and choose **Exit**.

### Start automatically with Windows

Put a shortcut to `winswitch.exe` in your Startup folder:

1. Press <kbd>Win</kbd>+<kbd>R</kbd>, type `shell:startup`, press Enter.
2. Drop a shortcut to `winswitch.exe` into the folder that opens.

## Download

Prebuilt 64-bit Windows binaries are produced automatically by CI:

- **Latest release:** grab `winswitch.exe` from the
  [Releases](../../releases) page (published whenever a version tag is pushed).
- **Latest development build:** download the `winswitch-exe` artifact from the
  most recent successful run on the
  [Actions](../../actions) tab.

## Building from source

WinSwitch is a single translation unit and links only against standard
Windows libraries (`dwmapi`, `user32`, `gdi32`), which are pulled in via
`#pragma comment(lib, ...)`.

### MSVC (recommended)

From a *Developer Command Prompt for VS* (or *x64 Native Tools Command
Prompt*):

```bat
cl /EHsc /O2 /W3 /DUNICODE /D_UNICODE winswitch.cpp /Fe:winswitch.exe /link /SUBSYSTEM:WINDOWS
```

### MinGW-w64

```bat
g++ -O2 -municode -mwindows -DUNICODE -D_UNICODE winswitch.cpp -o winswitch.exe -ldwmapi -luser32 -lgdi32
```

## How it works

- `EnumWindows` + `IsAltTabWindow` collect the windows a user would expect to
  see in Alt-Tab (visible, titled, not tool windows, not DWM-cloaked).
- The palette is a borderless top-most popup containing an edit box and a
  list box. Keystrokes in the edit box are intercepted via a subclassed
  window procedure (`EditProc`) so arrow keys and Enter/Esc drive the list
  instead of the text field.
- `Refilter` scores each window title against the query — exact substring
  matches rank highest (earlier and shorter matches win), then subsequence
  matches (tightest span wins) — and re-sorts the list on every keystroke.
- `ForceForeground` restores minimized windows and uses `AttachThreadInput`
  to work around Windows' foreground-locking so the selected window reliably
  comes to the front.

## Configuration

The hotkey and window size are currently compile-time constants in
`winswitch.cpp`:

- Hotkey: `RegisterHotKey(..., MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE)`
  in `wWinMain`.
- Palette size: `width`/`height` in `ShowPalette`.

Change them and rebuild to customize.

## License

[MIT](LICENSE) © Anton Žatkin
