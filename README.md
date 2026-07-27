# Prism Mod Folders

Virtual, shareable mod folders for the Prism Launcher Mods page.

![Prism Mod Folders preview](docs/images/preview.png)

Prism Mod Folders is an unofficial source modification for Prism Launcher. It
groups mods visually without moving, renaming, or changing any JAR files. A
small Windows patcher applies the modification to a supported official
`prismlauncher.exe` and keeps a verified backup for restoration.

[Русская версия](README.ru.md)

## Features

- Create, rename, expand, collapse, and remove virtual folders.
- Drag mods with the right mouse button while left click remains available for
  normal selection.
- Scroll the list with the mouse wheel while dragging.
- Drop anywhere on a folder row; the standard insertion line remains visible.
- Sort mods independently inside every folder with the existing Prism columns.
- Show a folder icon composed from up to four alphabetically selected mod
  icons.
- Keep expanded/collapsed state temporary, just like other view-only settings.
- Store a readable, portable `.minecraft/modfolders.json`.
- Removing a folder only ungroups its mods. It never deletes mod files.

Folders are purely visual. Every mod remains directly inside the instance
`mods` directory.

## Compatibility

The current patcher supports only the official Windows x64 build of Prism
Launcher 11.0.3:

```text
Official SHA-256:
C24C7C84FCE7FF1D12C709E0BCC8993AAA2A8CB662381C960CCB7D93C88BC2E3

Patched SHA-256:
E91DDEB27A1679F91F2FB10DC391CFC13EFF76DA7D0FF115C77C48D3274128A0
```

Unknown or previously modified executables are rejected without being changed.

## Installation

1. Download `PrismModFoldersPatcher-11.0.3.exe` from the latest GitHub
   release.
2. Close Prism Launcher completely.
3. Run the patcher and verify the detected `prismlauncher.exe` path.
4. Select **Install Mod Folders**.

The patcher automatically uses the Windows display language and includes a
manual `Русский / English` switch. Select **Restore Original** to restore the
verified official executable.

The patcher changes only `prismlauncher.exe`. It does not access Prism
instances, accounts, launcher settings, or Minecraft files.

## Portable folder format

The file is created only after a folder operation:

```text
<instance>/.minecraft/modfolders.json
```

Example:

```json
{
  "formatVersion": 1,
  "folders": [
    {
      "name": "Optimization",
      "mods": [
        "sodium-fabric.jar",
        "lithium-fabric.jar"
      ]
    }
  ]
}
```

Only JAR file names are stored. A mod can belong to at most one folder. See
[the format specification](docs/FORMAT.md), the
[JSON Schema](schema/modfolders.schema.json), and the
[complete example](examples/modfolders.example.json).

## Repository layout

- `launcher/` and `tests/` — modified Prism Launcher source and tests.
- `patcher/` — patcher source, delta builder, build script, and embedded delta.
- `docs/` — format documentation and upstream attribution.
- `schema/` — JSON Schema for `modfolders.json`.
- `examples/` — example portable folder layouts.

The included Prism Launcher source is based on the official `11.0.3` tag.

## Building

Build Prism Launcher using its normal CMake workflow and dependencies. The two
feature tests are:

```text
ModFolderStorage
ModFolderProxyModel
```

Build the Windows patcher from a PowerShell prompt:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\patcher\build-patcher.ps1 `
  -Delta .\patcher\PrismModFolders-11.0.3.pmdelta
```

The patcher is compiled with the .NET Framework C# compiler available on
Windows. The release executable contains the binary delta, not a full copy of
Prism Launcher.

## License and attribution

This project is licensed under the GNU General Public License v3.0. It is based
on [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher); see
[NOTICE.md](NOTICE.md) and the preserved
[upstream README](docs/UPSTREAM_README.md).
