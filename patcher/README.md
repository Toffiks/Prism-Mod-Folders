# Prism Mod Folders Patcher

The single-file Windows patcher adds Prism Mod Folders to an existing supported
Prism Launcher installation.

The current executable is approximately 1.56 MB and contains a compressed
binary delta, not a complete `prismlauncher.exe`.

## Safety model

1. Detect or manually select `prismlauncher.exe`.
2. Require Prism Launcher to be completely closed.
3. Verify the exact SHA-256 of the official executable.
4. Back up the original executable under
   `%LOCALAPPDATA%\PrismModFolders\backups`.
5. Reconstruct the patched executable in a temporary file.
6. Verify the patched SHA-256 before atomically replacing the original.
7. Provide a **Restore Original** action.

Unknown or previously modified executables are never patched.

The patcher has Russian and English interfaces. It changes only
`prismlauncher.exe` and does not access Prism Launcher user data.

## Supported build

```text
Prism Launcher: 11.0.3 Windows x64

Official SHA-256:
C24C7C84FCE7FF1D12C709E0BCC8993AAA2A8CB662381C960CCB7D93C88BC2E3

Patched SHA-256:
083834047BCB5139C032E163EE38AC4C10529ED8BC6CC180400176E979CE5ECE
```

## Build

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\build-patcher.ps1 `
  -Delta .\PrismModFolders-11.0.3.pmdelta
```

Self-test without modifying the source executable:

```powershell
PrismModFoldersPatcher-11.0.3.exe `
  --self-test official-prismlauncher.exe reconstructed.exe
```
