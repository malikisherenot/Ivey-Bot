# Ivey Bot Changelog

## v1.19.9

- Fix build: Geode 5.11.1 has no Mod::isEnabled
- Light Steps (test): skips the visual update on every step of a frame except the last, for heavy levels at high TPS
- Log TPS: new "cost" line (frames/s, ms per step, postUpdate time, budget cuts)

## v1.19.8

- Overlay and log name other mods that hook the step function (they can change TPS)
- README: note about other mods that change TPS

## v1.19.7

- Developer mode: Log TPS To Geode Log (Bot tab), writes [TPS] lines every second

## v1.19.6

- Time warp method puts the scheduler time scale back too, and cannot shrink step by step
- Developer mode shows time warp, time scale and steps per level second

## v1.19.5

- Built with Geode 5.11.1, the newest, to test the step size hook

## v1.19.4

- TPS loop can no longer lock up and freeze the player
- Overlay numbers measured fresh after the menu closes

## v1.19.3

- Built with Geode 5.4.1 like the XDBot fork (test for the step size hook)

## v1.19.2

- mod.json asks for Geode 5.11.1 (needed for the nightly SDK build)

## v1.19.1

- Built with the nightly Geode SDK (for the step size hook)
- libx264 first again in the codec list
- Audio tries the song file if the WAV fails
- Developer mode shows how many hooks the mod has

## v1.19.0

- TPS above 240 counts steps correctly with the time warp method
- Menu pages when a tab has too many rows
- In-level labels hidden while the menu is open
- Hardware video encoders first, libx264 last
- Audio errors name the codec
- Developer mode (tap Version in Theme 7 times)
- Dev effects Plasma and Rain, effect strength over buttons, more label info

## v1.18.1

- TPS above 240 works without the step size hook (time warp method)
- TPS Method in the Bot tab

## v1.18.0

- Editor playtest can start from a start position
- Window effect also covers the buttons (faint), can be turned off
- New window effects: Frost, Pastel, Snow
- Rainbow accent color that moves

## v1.17.3

- Overlay shows if the step size hook is on, and turns it on if it was off

## v1.17.2

- Overlay shows Moves/s, Hook/s and Hook step when TPS is not 240

## v1.17.1

- TPS loop no longer slows the game at high TPS
- Prevent Death looks ahead once per frame
- Overlay shows Steps/s and Progress/s

## v1.17.0

- Replay macros in the level editor playtest
- Menu button in the editor pause menu

## v1.16.1

- Classic TPS Loop (xdBot method) to compare with
- Speed readout in the overlay

## v1.16.0

- Window effects fixed on phones
- Frame stepper fixed with Prevent Death on
- Show Hitboxes drawn by the mod, no restart needed
- Render without a macro
- about.md updated

## v1.15.0

- Window Effect (Aurora, Rainbow, Scanlines) in the Theme tab

## v1.14.0

- Prevent Death (pauses before a death, then click and step)
- Show Hitboxes
- Hitbox Trail
- Trajectory tab is now called Assist

## v1.13.1

- Build fixes (Windows, macOS, iOS, Android)

## v1.13.0

- Audio in the renderer (the song is added to the video)
- Recording one player turns on noclip for the other player
- gdr.json macros keep the player rotation (orbs and portals look right)
- Presets removed

## v1.12.0

- Renderer zoom fixed
- Stop button while rendering fixed
- Macros now save vertical speed for better corrections
- Respawn no longer drops clicks while recording
- Single player recording fixed
- Merge takes each player from one macro

## v1.11.0

- Renderer (video, no audio yet)

## v1.10.0

- Macro names
- Continue macro
- Bot P1 only and 2P only
- Merge macros

## v1.9.2

- TPS no longer speeds up the game
- Macro TPS applies only while botting
- Resize buttons
- Smaller menu button
- Open Folder no longer crashes phones
- Removed extra options and arrows

## v1.9.1

- Trajectory outside the step budget
- Slow warning
- Stepper buttons bottom left

## v1.9.0

- Faster menu
- New menu look
- Movable and resizable menu
- Real TPS readout
- Bigger step budget

## v1.8.3

- .gdr.json playback

## v1.8.2

- TPS locked while botting
- Real TPS readout

## v1.8.1

- xdBot TPS
- Longer trajectory
- Removed macro shortcuts

## v1.8.0

- Show trajectory
- Swift clicks
- Search tab

## v1.7.0

- Gameplay tab
- Noclip
- Frame stepper
- Instant respawn
- Safe mode

## v1.6.0

- Speedhack audio
- Presets
- Smaller macros
- .gdr and .gdr2 playback

## v1.5.0

- Macro settings apply on load
- Names and ids in file names
- New TPS and frame timing
- Fixed seed

## v1.4.1

- TPS fix

## v1.4.0

- Macro search

## v1.3.0

- Faster stepping
- Wave checks
- Removed main menu button

## v1.2.0

- Wider font
- Type your own values

## v1.1.0

- Abel font
- Position correction
- Format v2
- Bigger menu button
- Build fixes

## v1.0.0

- Initial release
