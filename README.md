# Ivey Bot

Frame-accurate macro bot for Geometry Dash.

- **Game:** 2.2081
- **Loader:** Geode
- **Platforms:** Windows, macOS, Android, iOS
- **ID:** `itzmalikhere.malik`

## Use

1. Open the menu with the Ivey button (main menu or pause menu).
2. Go to **Bot**, turn on **Record**, restart the level and play.
3. Finish the level (auto saves) or press **Save Macro** in the **Macro** tab.
4. Load a macro, turn on **Replay**, restart the level.

## The `.ivey` format

Binary, little-endian. Accuracy comes first.

| Bytes | Field | Notes |
| ----- | ----- | ----- |
| 2 | Accuracy | Steps per second (TPS) |
| 1 | Flags | bit 0 = frame accurate |
| 1 | Version | `3` |
| 4 | Magic | `IVEY` |
| 4 | Level ID | |
| 4 | Count | Number of entries |

Each entry is 7 bytes:

| Bytes | Field | Notes |
| ----- | ----- | ----- |
| 4 | Frame | Physics frame |
| 1 | Input | 1 jump, 2 left, 3 right |
| 1 | State | 1 press, 0 release |
| 1 | Player | 0 = P1, 1 = P2 |

If flags bit 1 is set, position checks follow, one track per player (P1, then P2). Each track is a varint count, then packed records: the first stores frame and position, the rest store only how far the position missed a straight-line guess (1/32 units). A perfect guess takes 1 byte.

Older v1 and v2 files still load.

## Other formats

Put `.gdr`, `.gdr.json` and `.gdr2` files in the macros folder and search for them in the Macro tab.

## Trajectory

Shows where the player will go if you hold or release. It runs a copy of the player through the real level, so nothing in the game changes. Other features can reuse it: `ivey::Trajectory::get().simulate(...)` takes any input sequence and returns the path, whether it died and where.

## Rendering

Needs the [FFmpeg API](https://geode-sdk.org/mods/eclipse.ffmpeg-api) mod. Load a macro, open the level, then use **Render > Start Rendering**. Videos are saved in the mod's `renders` folder. This version has no audio.

## Credits

Swift Clicks idea by Ayumi.

TPS stepping follows the TPS bypass in [xdBot](https://github.com/zilko/xdBot) by Zilko & Camellia.

## Build

```
git clone https://github.com/itzmalikhere/IveyBot
cd IveyBot
geode build
```

Needs the [Geode SDK](https://docs.geode-sdk.org). GitHub Actions builds every platform on push.

## License

MIT, see [LICENSE](LICENSE).
