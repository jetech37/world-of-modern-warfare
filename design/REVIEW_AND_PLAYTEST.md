# Review and playtest list (2026-10-28)

What needs a comprehensive code review and a real playtest before release. Each item says what
to check. "Verified" means checked only by tests or captures, never in live play.

## A. Highest risk

1. **Server stability.** Five mangosd crashes today: a stack overflow on Windows, exact function
   unknown. The fix is 8 MB thread stacks plus a crash handler that survives the overflow. Play
   for a long session, with logins and teleports. If it crashes, read the new `.dmp` with
   `server/contrib/crash/symbolize.py`. Code review: look for deep recursion in fusion code
   (pathing, ships, companions, wards).
2. **Logout and login from every state.** Downed, duel, companion ride, at a ship's helm or deck
   gun, aboard a ship, mid-skyride, mid-mount cast, mid-charge, in Dalaran. There should be no
   stuck roots, lost companions or duplicated items.
3. **Security review follow-ups.** The open Low items in `design/SECURITY_REVIEW_2026-10.md`
   (#27-#32), plus everything added since: ships (commission, crew, cargo, `.ship` GM commands),
   card duels (wire v2), per-hand charge power, the mount cast (op 7), grenades while mounted.
4. **Balance.** Per-hand charges and the 2.75x/2.25x full charge: the Druid Starcaller wins its
   identity duel with 1% health left. Guns vs. classes (`BALANCE_GUNS_VS_CLASSES.md`). Duel decks:
   Avian/Humanoid at 39-42%.

## B. Systems to playtest end to end

### Controls and hands
- Ctrl: tap to crouch, hold to go prone, hold again to stand; chords never go prone. Space rises one stance.
- C is the Companion bar; Ctrl+Space is Launch; Alt+scroll does not zoom.
- Action-slot outlines: gold when selected, main vs off hand, across all four systems.
- Utility slot 1: relaxed hands, toggling to fists.
- First person ↔ third person switches instantly.
- Gnome and other small races crouching or prone: no floating.

### Guns
- All 57 MW2 guns in third person, every race and gender: barrel forward, grip in the hand.
  Lighting on held guns.
- First-person viewmodels; mounted first person (horse, gryphon, others): the hands are only
  checked in captures.
- Shot targeting; shooting through companions and party/raid members; hit validation.
- Grenades: main hand only, pin and throw animations, blocked while skyriding above 3 yd.

### Spells (two hands)
- Separate charges per hand, two cast bars, the charge hum and chimes (each hand separately).
- Damage over time still uses the per-spell windows.

### Mounts and skyriding
- The companion mount cast: cast-bar timing, the run-in in front of the player, interrupts
  (moving, combat, pressing the key again), the companion's bark.
- The Timber Wolf name still shows to other players and on target/party frames.
- Mounted combat: the mount attacks when the rider shoots or swings; the rear tips the rider
  back; gryphons have no attack clip.
- Skyriding: tilt springs, levelling in a hover, the climb/dive/flap clips, wind streaks,
  Launch/Surge bursts, wingtip trails. **None of these have been seen in live flight.**
- City wards and no-fly ceilings over every city, including Dalaran's new pad and ceiling.

### Companions and card duels
- Following beside the player, not on top of them.
- Tamers (Goldshire, Razor Hill, then by level) and the trainer in Stormwind: confirm their
  spawn positions in game (they are marked VERIFY in the SQL).
- The card battler: every family's deck, Ctrl+1..n, C doing nothing in a duel, forfeiting on
  logout, rewards, achievements.
- The Infernal's look; all companion looks after the missing-texture fallback.

### Ships
- `.ship test airship|sail`, `.ship delete`.
- The real commission flow and its cost; helm steering; deck guns; hull damage; cargo; upkeep;
  respawn aboard; the bell; crew.
- Persistence across a server restart.
- Ships near cities, wards and Dalaran.
- Deck-gun state against the other systems (never reviewed).

### Professions
- Construction, Minstrelsy, Inscription, Jewelcrafting: the stock-look window, the cast bar,
  recipes, skill-ups.

### Classes
- All 27 archetypes: each plays distinctly and can win without guns. Owner questions Q1-Q6
  are still open.

### World
- **Dalaran (rebuilt today):** level with the land, the plateau slopes, the lake shore, the
  gatekeepers in and out, the Stormwind and Orgrimmar archmages, the NPCs in the market square
  (vendors, trainers, bank, inn), flying in. Some Alterac quests lost Dalaran Summoner and
  Elemental Slave spawns.
- The Stormwind Harbor ↔ city transition; zeppelin paths; the world map and its zone dropdowns.
- Community zones and quests: 1,671 offered, 700 hidden; 42 bosses with EventAI.
- Textures: 1,121 creature displays fall back to an embedded or untextured skin; 19 world files
  use stand-ins; the HD pack's look.
- The Undercity exterior, Northrend and the 57 community Stormwind hole cells: unverified or
  not done.

### UI
- The tooltip pass on every custom window (never done).
- Window overlap and z-order, chat over windows, the Kits tabs.
- Achievements for the new systems.
- `LuaErrors.log` stays empty in normal play.

## C. Code review targets (largest or riskiest diffs tonight)
- `client/src/mount_flight.rs`: the tilt springs and effects.
- benilla `entities/display.rs`: texture settling and the fallback; `creature_anim/driver.rs`:
  the mounted hold.
- `client/src/wotlk/dalaran.rs`: the plateau and the ring cleanup; `37_dalaran_city.sql`.
- `FusionSpellcasting.cpp`: per-release power on bolts and zones.
- `FusionCompanions.cpp` and `Companions.cpp`: the mount cast.
- `FusionShips.cpp` and `ShipCrew.cpp`; `CompanionBattles.cpp`.
- `tools/community-merge`: substitutes, verify, hd.
- The benilla schedule-ordering ceilings were raised, not fixed.

## D. Known gaps (not bugs, but not done)
- No walk-in gate for Dalaran (it needs a model edit); no Dalaran flight master.
- Card upgrades and wild duels; ships stage 3 (mounts aboard, wreck loot, a boarding lock,
  group commissions, a shipyard plot, a heavy class, continent crossing).
- C++ scripts for the top community quest NPCs and instance scripts.
- Faction templates 1682 and 1693 missing (creatures 61015, 61016, 61902): log spam.
