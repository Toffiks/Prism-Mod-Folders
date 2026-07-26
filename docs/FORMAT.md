# `modfolders.json` format

Prism Mod Folders stores one readable JSON file in the Minecraft root of each
instance:

```text
<instance>/.minecraft/modfolders.json
```

The file is written only after a folder is created, renamed, removed, or its
membership changes. Opening the Mods page alone does not create it.

## Version 1

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
    },
    {
      "name": "Libraries",
      "mods": []
    }
  ]
}
```

### Root fields

- `formatVersion` must be the integer `1`.
- `folders` is an array of folder objects.

### Folder fields

- `name` is a non-empty folder name, unique without regard to letter case.
- `mods` is an array of bare mod file names. Paths and directory separators are
  rejected.

A mod may appear in at most one folder. Duplicate entries are ignored while
loading. Mods missing from the instance stay recorded so folder membership can
survive a mod update or temporary removal.

The file controls only presentation in Prism Launcher. It never changes the
physical layout of the `mods` directory.

See [the JSON Schema](../schema/modfolders.schema.json) for machine-readable
validation rules.
