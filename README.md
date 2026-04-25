# StreamDock Remote Control for OBS

This repository contains the source code for the MiraBox StreamDock OBS plugin.

## Build Requirements

| Platform | Requirements                                                     |
| -------- | ---------------------------------------------------------------- |
| Windows  | Visual Studio 2022 with Desktop development for C++, CMake 3.28+ |
| macOS    | Xcode 16.0+, CMake 3.28+                                         |

Notes:

- The first CMake configure step downloads the required OBS Studio, obs-deps, and Qt dependencies defined in `buildspec.json`.
- In-source builds are not supported.
- Git is required because the bootstrap logic uses the repository layout directly.

## Build on Windows

Use the provided Visual Studio preset:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

This generates the build tree in `build_x64` and builds the `RelWithDebInfo` configuration by default.

## Build on macOS

Use the provided Xcode preset:

```bash
cmake --preset macos
cmake --build --preset macos
```

This generates the build tree in `build_macos` and builds the `RelWithDebInfo` configuration by default.

## CI Presets

The repository also includes CI-oriented presets:

- `windows-ci-x64`
- `macos-ci`
- `ubuntu-ci-x86_64`

They are intended for automation, but can also be used locally when you want the stricter CI configuration.
