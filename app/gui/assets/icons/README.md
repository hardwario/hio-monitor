# Icons (ADR-0060)

Material Symbols **Outlined**, weight 400, grade 0, optical size 24, as individual SVG files (no icon font). Each
file is named after its function in the GUI, not after the upstream glyph, so that a later set swap touches only
these files. SVG is text and is not stored in Git LFS. The icons are compiled into the QML module
`HardwarioMonitor` (`assets/icons/<name>.svg`) and recoloured from the `Theme` at runtime (`Icon.qml`,
`icon.color`).

- Source: <https://github.com/google/material-design-icons>, directory
  `symbols/web/<glyph>/materialsymbolsoutlined/<glyph>_24px.svg`.
- Pinned commit: `bd8cb85bd4bad964fe6918f79665bb40c3a8efef` (2026-09-25, "Update Symbols"). The repository has no
  release tag that contains Material Symbols (its newest tag, 4.0.0, is from 2020), so the files are pinned to this
  commit (as ADR-0064 allows for dependencies without a recent tag).
- Licence: Apache License 2.0, `LICENSE-Apache-2.0.txt` (the repository's `LICENSE` at the same commit, SHA-256
  `58d1e17ffe5109a7ae296caafcadfdbe6a7d176f0bc4ab01e12a689b0499d8bd`); listed
  under Settings → About → Licences.

| File | Upstream glyph | Used for | SHA-256 |
|---|---|---|---|
| `add.svg` | `add` | new device tab | `e9532de2b9be01ce951e899e98ee673bb97acd574f0db118feb72fe074a1c376` |
| `check.svg` | `check` | done phase (PhaseStepper) | `01edd53418816632be08e9565b8ca40d9028c230a8a67baaf4b8a1de20db00e6` |
| `close.svg` | `close` | close tab, dismiss | `a82592b0faa0b104a4d4b0530bb05c6a4bfed5728f5aad03ae8e7a19f32b5511` |
| `collapse.svg` | `expand_less` | collapse (Details) | `c226790222367980dcb9366768440a1bdbc778f6f957f6de878d39b523632528` |
| `console.svg` | `terminal` | Console page | `02d50185ce66a04950f8b7eed976005aec13442b818e693247c2d3a791febaa8` |
| `copy.svg` | `content_copy` | copy details | `3575c3fdb035a66097bed9a7597620e313ed1efa051eb586a7335348c3fdff35` |
| `device.svg` | `developer_board` | device | `6248e2ccac1676e75cf648eafc8028c1427c8277b77ad7b5173aeee58ac2294f` |
| `error.svg` | `error` | ErrorPanel, failed check | `f02b30c23e0c004e47a128526ab417832b2fd475c4cfbcdfff58e9df8f6c30ab` |
| `expand.svg` | `expand_more` | expand (Details) | `6760bd2307f7ec3c1a5f2905af907aa7a12ad5ad7448289c8624f5d5ec6057e4` |
| `flash.svg` | `bolt` | Flash page | `efc632df3302b78537d07d8fd674a66cb911b132515cd4f84c6a6a995ecff8e7` |
| `help.svg` | `help` | Help (shortcut overlay) | `9f2bdd969cefc5843f95ccbd560c4281b1eec87c78b68bd4daf694d19fc9c5f9` |
| `info.svg` | `info` | information | `33c77c7a4f193445d0136b970ad4b5a80ba8cc340603465d4cb46a1f8abec4ec` |
| `keyboard.svg` | `keyboard` | keyboard shortcuts | `2c3e09a5bbc5122b64cb3c0d351708b94d6c68c68378ab2cd70d37c0426b2679` |
| `more.svg` | `more_horiz` | ⋯ Advanced, overflow menus | `498ade89fbbdd4f4ab9b90bb49bae4cc6b2dc0ff0f019b6b051ac641a7bd5872` |
| `pending.svg` | `radio_button_unchecked` | open check (empty state) | `adf550b8d06942c7a6d77e650b0279ccdb6cc888761dcb4d1e860105c623fcaf` |
| `placeholder.svg` | `construction` | placeholder pages | `0e767ed70b73835986f715e33373b125a65816f66842756ba1bbc4fc36c2c5f6` |
| `probe.svg` | `usb` | J-Link probe, no-probe state | `dded90bd9f5080293d7e6a79ab516eebc58c7567e9175a176a2824fb8e7db4e4` |
| `refresh.svg` | `refresh` | refresh the probe list | `e56d7ee330abd6d410cca7778419c524cebacf300c3b6e1b97ca9d0f3c3c1f7c` |
| `settings.svg` | `settings` | Settings page | `6aebb4a88b40a5bf1ce378ca532e4a885aedc402a0574562c6ed4ded03360fd2` |
| `success.svg` | `check_circle` | passed check | `42bf0950bc12ecf7952a8c3be86e0131f225095a7dc6c824a659a13df442e373` |
| `warning.svg` | `warning` | WarningBanner | `0212c59cfa1e2da887b0b36c7d89886de94b019feaaba5c063ec8dc7d32be436` |

Adding an icon: copy `<glyph>_24px.svg` from the same commit as `<function>.svg`, add a row here (glyph, use,
SHA-256). Plans 0005 and 0006 add their own icons this way.
