# Bundled fonts (ADR-0048)

The GUI bundles its fonts so that it renders the same on every OS (design system § 2). The `.ttf` files are stored
with **Git LFS** (`*.ttf` in `.gitattributes`); they are compiled into `hio-monitor` as Qt resources (`:/fonts/…`)
and registered by `app/gui/src/fonts.cpp`. Both families are licensed under the **SIL Open Font License 1.1**; the
licence texts are next to the fonts and are listed under Settings → About → Licences.

| File | Family / style | Source (pinned release) | SHA-256 |
|---|---|---|---|
| `Inter-Regular.ttf` | Inter Regular (400) | Inter 4.1, `Inter-4.1.zip` → `extras/ttf/Inter-Regular.ttf` | `40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82` |
| `Inter-Medium.ttf` | Inter Medium (500) | Inter 4.1, `extras/ttf/Inter-Medium.ttf` | `97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872` |
| `Inter-SemiBold.ttf` | Inter SemiBold (600) | Inter 4.1, `extras/ttf/Inter-SemiBold.ttf` | `78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3` |
| `Inter-LICENSE.txt` | OFL 1.1 licence of Inter | Inter 4.1, `LICENSE.txt` | `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a` |
| `JetBrainsMono-Regular.ttf` | JetBrains Mono Regular (400) | JetBrains Mono 2.304, `JetBrainsMono-2.304.zip` → `fonts/ttf/JetBrainsMono-Regular.ttf` | `a0bf60ef0f83c5ed4d7a75d45838548b1f6873372dfac88f71804491898d138f` |
| `JetBrainsMono-Bold.ttf` | JetBrains Mono Bold (700) | JetBrains Mono 2.304, `fonts/ttf/JetBrainsMono-Bold.ttf` | `5590990c82e097397517f275f430af4546e1c45cff408bde4255dad142479dcb` |
| `JetBrainsMono-OFL.txt` | OFL 1.1 licence of JetBrains Mono | JetBrains Mono 2.304, `OFL.txt` | `30f0c136e3c88e422d0791acd97238870f9054a9729bc34cf2ff0d4ed8cac4ad` |

Release archives:

- Inter 4.1 (2024-11-16): <https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip>,
  SHA-256 `9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e`.
- JetBrains Mono 2.304 (2023-01-14): <https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip>,
  SHA-256 `6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf`.

Updating: download the new release archive, check its hash, copy the same files, update this table, run the GUI
tests (`tst_gui`) and `tools/check-lfs.sh`.
