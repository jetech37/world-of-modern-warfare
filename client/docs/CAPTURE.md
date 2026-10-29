# Headless captures of held guns, poses and the mounted view

The client inherits benilla's capture mode (`benilla/crates/benilla-app/src/capture/`): with
`WOW_CAPTURE=<scenario>` it boots with no server, pins the clock and camera, waits for the image
to stop changing, writes one PNG and exits. The window opens for a few seconds.

Set these first (Git Bash):

```sh
export WOW_DATA='C:\Projects\azeroth-warfare\dist\AzerothWarfare\game\WoW\Data'
export MW2_PATH='C:\Projects\azeroth-warfare\dist\AzerothWarfare\game\MW2'   # needed for the guns
cd client && cargo build --release
```

The subject is the lighting matrix's creature (scenario `creature-indoor-front` and its kin,
`WOW_CAPTURE=list` prints them). Knobs:

| Knob | Does |
|---|---|
| `WOW_SUBJECT_RACE=race,sex` | a naked player body: race 1 human, 2 orc, 3 dwarf, 4 night elf, 5 undead, 6 tauren, 7 gnome, 8 troll; sex 0 male, 1 female |
| `WOW_SUBJECT_DISPLAY=id` | any creature display instead |
| `WOW_SUBJECT_MOUNT=id` | seat it on a mount display |
| `WOW_SUBJECT_YAW=deg`, `WOW_SUBJECT_UP=yd` | turn / lift it |
| `MW2_PROBE_HOLD_GUN=<id or name>` | it holds that MW2 gun (crate::npc_guns): `m4`, `usp`, `ak47_mp` or a catalog id |
| `MW2_PROBE_POSE=gun`, `crouch`, `crouch,gun` | the gun raised, crouched, or both (crate::body_state) |
| `MW2_PROBE_READY_EVERY=secs` | how often the ready clip is replayed (default the game's 0.9). The shutter catches the clip at a phase set by this: 0.9 suits humans, 0.25 taurens |
| `WOW_CAPTURE_UI=0` | no UI, even over a `ui:` scenario (the window then opens 3200x1800) |
| `MW2_READY_FRAC=f` | where in each model's ReadyRifle clip a standing body is held (default 0.5) |
| `WOW_SUBJECT_ORBIT=az,el,dist` | third-person camera orbiting its middle |
| `WOW_SUBJECT_EYE=el[,turn[,back]]` | first-person camera at its face attachment, looking along its facing |

The camera the knobs set is re-applied after every Update system (`repin_camera`), so no
client camera (first-person seat, shake, recoil) can move it.

Examples:

```sh
# a tauren female holding catalog gun 1, raised and crouched, from the front-right
WOW_CAPTURE=creature-indoor-front WOW_CAPTURE_OUT=/tmp/tau.png WOW_SUBJECT_RACE=6,1 \
  MW2_PROBE_HOLD_GUN=m4 MW2_PROBE_POSE=crouch,gun WOW_SUBJECT_YAW=0 WOW_SUBJECT_ORBIT=90,10,4.5 \
  ./target/release/benilla-mw2.exe
# first person on a gryphon (KNOWN BROKEN: the mount body does not draw in capture yet)
WOW_CAPTURE=creature-indoor-front WOW_CAPTURE_OUT=/tmp/fp.png WOW_SUBJECT_RACE=1,0 \
  WOW_SUBJECT_MOUNT=1149 WOW_SUBJECT_EYE=-15 ./target/release/benilla-mw2.exe
```

Framing: with `WOW_SUBJECT_YAW=0` the body faces -Z; orbit az 0 is behind it, 90 its right (gun)
side, 180 its front. The orbit aims at 0.72 of the face height and scales `dist` by height/1.8, so a gnome and a tauren fill the frame alike (crouched it uses the crouched height, so frames tighter).
The UI is drawn over the lower left; frame the subject above it.

## Our windows, with no player

One switch keeps any of our layer's windows open in a capture (crate::capture_keep):

```sh
WOW_CAPTURE=creature-indoor-front WOW_CAPTURE_UI=1 WOW_CAPTURE_OUT=/tmp/bounty.png   MW2_CAPTURE_KEEP_WINDOWS=1 MW2_CAPTURE_WINDOW=bounty ./target/release/benilla-mw2.exe
```

`MW2_CAPTURE_KEEP_WINDOWS` also takes the names straight (`bounty,kits`) or `all`. The names:
`duel` (a sample duel, as `MW2_PROBE_DUEL_UI`), `kits`, `companions`, `bounty`, `minstrelsy`,
`augments`, `professions`, `trainer`, `build`, `achievements`, `guide`, `map`. Add `WOW_RESIZE=2560x1600` for the large size.
With no server the windows show their empty states (no bounties, no companions).
