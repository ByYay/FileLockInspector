<p align="center"><img src="resources/file-lock-inspector-icon.png" width="112" alt="File Lock Inspector icon"></p>

# File Lock Inspector

A lightweight, on-demand file usage inspector for **Windows 11 x64**, built with C++20 and native Win32 controls.

**No service. No startup task. No tray process.** Scanning runs only when requested, in an isolated worker that is closed with the application.

## Features

- Identify processes reported by Windows Restart Manager.
- Run an optional Advanced Scan using local file identity rather than filename matching.
- View executable paths, PIDs, detection status and process details.
- Request a controlled close, or explicitly confirm Force Terminate.
- Use the app as a portable utility or register an Explorer context-menu command.
- Light/dark theme support, high-contrast fallback, keyboard navigation and DPI-aware layout.

## Project status

Release builds with MSVC have succeeded. CLI smoke tests verified two simultaneous file holders, normal and advanced detection, refresh after a holder exits, and an unused file. The full CTest executable encountered a local execution/access restriction and has **not** passed verification. The latest UI revision is not visually verified. Modern signed-package deployment and the complete Windows 11 acceptance matrix remain unverified; this is not a fully validated production release.

## Build

Install Visual Studio 2022 Build Tools with **Desktop development with C++**, Windows 11 SDK and CMake 3.24 or newer. From an x64 developer terminal:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix build/dist
```

The portable output is in `build/dist`. Keep `FileLockInspector.exe`, `FileLockWorker.exe` and `FileLockShell.dll` together. The CRT is statically linked; the application needs no Python, Node.js, Qt or .NET runtime. PowerShell is used only by development and installation scripts.

Alternatively, `tools/build-test.ps1` locates the installed Visual Studio tools and builds, tests and stages the application. Use `-SkipTests` to build without claiming test validation.

## Use

Drag a file onto `FileLockInspector.exe`, or run:

```powershell
.\build\dist\FileLockInspector.exe 'C:\Test\example.txt'
```

Select a process in the list:

- **Details** shows identity, owner SID, protection status and last-scan metrics.
- **End Process** requests controlled closure after confirmation.
- **More** contains Copy PID, Copy executable path, Open file location and **Force Terminate**.
- **Refresh** (F5) replaces the current snapshot; **Advanced Scan** performs the optional deeper scan.

Force Terminate requires its own confirmation and may discard unsaved work. There is no automatic fallback from controlled closure to forced termination. Protected/critical processes, Windows components, other users/sessions and processes whose safety cannot be established are not eligible. The app never forcibly closes another process's file handles.

## Explorer integration

Run in 64-bit PowerShell as the current user:

```powershell
.\build\dist\installer\install.ps1 -Mode Classic
```

Right-click a file, choose **Show more options**, then **Who is using this file?** Sign out and back in if Explorer retains a cached menu.

Uninstall:

```powershell
& "$env:LOCALAPPDATA\Programs\FileLockInspector\uninstall.ps1"
```

Modern Windows 11 menu integration uses `IExplorerCommand` and a sparse MSIX package. It requires a package signed by a trusted publisher; no signing certificate is included:

```powershell
.\installer\package.ps1 -Publisher 'CN=Your Publisher'
# Sign build/package/FileLockInspector.msix with the Windows SDK signing tools.
.\build\dist\installer\install.ps1 -Mode Modern -PackagePath .\build\package\FileLockInspector.msix
```

Classic and modern registrations are mutually exclusive. Installation creates no service, startup entry or scheduled task. A loaded Shell DLL can require sign-out before update/removal; the installer does not forcibly restart Explorer.

## Scope and limitations

| Item | Supported scope |
|---|---|
| Selection | One file or one folder; multi-selection is explicitly unsupported |
| Folders | Advanced Scan checks the folder object itself, never recursive contents |
| Normal scan | Restart Manager reports possible usage, not direct proof of a particular handle |
| Advanced scan | Local volume GUID, volume serial and file ID matching; inaccessible handles are reported as incomplete |
| Hard links / symlinks | Identity-based matching of the resolved local object |
| Network paths | Passed to Restart Manager subject to Windows/provider limits; Advanced Scan requires a local volume GUID |
| Long paths | Supported where the OS/API allows; the Windows command-line length limit still applies |
| Results | A transient snapshot; no detected processes does not prove a file is unused |

Advanced detection is not exhaustive and uses a runtime-checked Native API whose layout may change. Its worker has a 20-second deadline and a 64 MiB snapshot limit. No driver or debug privilege is used. Owner information is shown as a SID to avoid domain lookup delays.

## Testing

```powershell
ctest --test-dir build -C Release --output-on-failure
.\tools\smoke-test.ps1
.\tools\start-demo.ps1
```

`Test Et.cmd` opens an interactive sample with two disposable file-holder consoles. Press Enter in a holder console to release the sample file. `Tarama Testi.cmd` runs CLI smoke checks. `Derle ve Test Et.cmd` builds and runs the full validation script.

Tests operate on their own fixture processes and files. Generated reports in `build/` can contain local paths, process IDs and hardware information: **do not commit or publish them**. Build outputs, reports, local tool state and private signing material are excluded from the source distribution and Git ignore rules.

The GitHub Actions workflow builds and runs tests on a Windows runner; that does not replace manual Windows 11 Explorer, theme, accessibility and packaging checks.

## Source layout

- `src/`: UI, scan engine, worker supervision and process safety.
- `shell/`: Explorer COM command and sparse package manifest.
- `installer/`: per-user installation, removal and packaging scripts.
- `tests/`, `tools/`: integration tests and local development helpers.
- `resources/`: native manifest, version resource and application artwork.

[Mimari / Architecture](docs/ARCHITECTURE.md) · [Güvenlik / Security](docs/SECURITY.md) · [Validation status](docs/VALIDATION.md)

## License

[MIT](LICENSE). The application artwork was generated with an AI image tool.
