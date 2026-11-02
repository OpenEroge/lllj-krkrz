# win32

TJS-only stand-ins for Windows facilities (`prelude/win32.tjs`). Each one
reports what a game would see on Windows, or that the facility is absent, in
the way the original DLL/API reports it.

| Replaces | Behaviour |
|---|---|
| `System.getOSVersion` | Windows 10 19045 (Media Foundation movie paths pick .mp4) |
| `System.readRegValue/writeRegValue` | stubs (void) |
| `wuvorbis/wuopus/krmovie/kagexopt/lzfs/yuzuex.dll` | nothing to do: built into the engine |
| `getlangname.dll` | Japanese UI language (0x411) |
| `win32dialog.dll` | `WIN32Dialog` whose dialogs are always cancelled, plus the Win32 constants |
| `windowEx.dll` | `Window` members/constants, one monitor the size of the screen, `System` stubs |
| `menu.dll` | data-only `MenuItem` (no native menus) |
| `drawdeviced2d/k2compat/httprequest/win32ole/shellexecute/process/messenger/addfont.dll` | link succeeds, nothing installed |

Stubs log `(compat) stub: Name` on first use; tests list the accepted ones.
