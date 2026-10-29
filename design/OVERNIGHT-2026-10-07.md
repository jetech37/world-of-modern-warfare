# Overnight build, 2026-10-07: ledger

Scope: everything in `design/GAME_DESIGN.md`, plus world building for the classic map's unfinished
areas. Justin's answers before he left:

- Live tests OK (his client closed); restore Test's position after.
- Ship to the friends' VPS when done (server build in WSL `/root/aw-build`, SQL to the VPS world DB without
  touching player data, restart, `package/publish.py --note ...`). Everything ships, the class rework too.
- Revives only from spells (Resurrection, Redemption, Rebirth, Ancestral Spirit), a First Aid item and an
  Engineering item; both items usable on yourself while downed, slower. No hold-F revive.
- Invite code unchanged; ship the launcher that re-asks when the saved code is rejected.
- Bosses: mechanics untouched, only health and damage scaled.

Machine after his reboot: 31.6 GB RAM visible (he moved the iGPU reservation), 32 threads, ~160 GB free.
At most 3-4 heavy builds at once.

## Work packages (vertical slices, one subagent each, own worktree)

| # | Package | Wave | State |
|---|---|---|---|
| W1 | Kits + named Quartermasters + mobile QM item + ammo refill + launcher invite re-ask | 1 | |
| W2 | Downed state, revives (spells, First Aid kit, Engineering cables), corpse runs, no durability | 1 | |
| W3 | Stat conversion table (handling, bullet armor, weak-spot crit, regen, thresholds, procs from gun hits) | 1 | |
| W4 | Construction rules (spells damage, salvage, reinforcement, sentry → killstreak) + danger scaling (creatures, bosses, auto-gunner camps) | 1 | |
| W5 | Companions: pet slot for everyone, aim-driven pet bar, traits + combos, breeding at stable masters, movesets with ability choices, new lines | 2 | |
| W6 | Class framework: old class spells out, 3 core + 6×3 options per class, trainers sell options, kit talent page, as many abilities as possible | 2 (after W1) | |
| W7 | Weapon pickups + arsenal lore names + Enchanting consumable rounds | 2 | |
| W8 | Prestige tracks + battleground MW2 modes + professions rework | 3 | |
| W9 | Mounted shooting, dismount on hit, brief flight + world building (unfinished areas → housing districts) | 3 | |

ID ranges (so packages never collide):
- creature entries: W1 92002-92199, W4 92200-92299, W5 92300-92499, W8 92500-92599, W9 92600-92699
- item entries: W1 93001-93019, W2 93020-93049, W7 93050-93199, W8 93200-93399; companions keep 90001-90999 (W5)
- custom spell ids (if any): W2 62500-62599, W5 62000-62499, W6 61000-61999, W7 62600-62799, W8 62800-62999
- SQL files in `server/sql/custom/azeroth_warfare/`: W1 05, W2 06, W3 -, W4 07, W5 08, W6 09, W7 10, W8 11, W9 12
- fusion channels: 1 build/loadout, 2 world, 3 achievements, 4 companions exist. New: W2 → 5, W3 → 6,
  W6 → 7, W8 → 8. New ops on existing channels: W1 on channel 1 from op 30; W4 on channel 2 ops 50-59,
  W7 on 2 ops 60-69, W9 on 2 ops 70-79

## Log
- Research: `design/CLASS_CATALOG.md` written (189 designs; routes A 103 / B 79 / C 7; trainer prices 1/5/12/22/35/60 g).
  Rulings on its open questions (Justin can overturn):
  - Ruling: out-of-combat **Utility** spells (resurrections, Aspect of the Cheetah, Healthstone, conjures,
    travel) sit outside the 9-ability limit — they are never used in a fight, so the first-person kit stays
    small. Cost if wrong: a few spells move into tiers.
  - Ruling: rogue energy/combo points and warrior stances go, as drafted — cooldown-limited abilities fit
    the hands-up rule; the archetype resources replace them. Cost if wrong: re-add a resource.
  - Ruling: druid form abilities don't count toward the limit (the form is the option). 
  - Ruling: Grovekeeper's Rebirth stays the only druid revive (vanilla-faithful); druids lean on items.
- Started wave 1 (W1-W4) and research (classes, companions, world). W6 started early against a narrow kit
  interface (6 option ids per kit) since the catalog was ready; join to W1's kit storage at merge.
- Research: `design/WORLD_BUILDING.md` written (measured from the extracted height maps). Findings for W9:
  Hyjal plots are partly on hillsides (re-layout measured, same plot ids); Azshara Crater (map 37) is
  buildable and empty; a flat placeholder plateau in Quel'Thalas (z 129) and a flat valley in NW Stonetalon
  (z 131); no graveyard serves Hyjal/Azshara Crater; the code assumes one district (one gate, upkeep sweep
  only on map 1). W9 does multi-district support + Hyjal re-layout + Azshara Crater + Quel'Thalas, after W4.
- Research: `design/COMPANION_CATALOG.md` written: 64 new lines (species 101-164, items 91101-91164,
  catalysts 90105-90107), sizes measured from model.MPQ (stage 1 ~0.42x, stage 3 ~1.7x), movesets with
  vanilla spell ids, 30 traits + 14 combinations, breeding numbers, pet bar layout. Notes: rideable stage 3
  should ride at its own scale (benilla can scale mounts) and many models need a seat offset in benilla.
- W3 stats merged (bd652ca9c). Note: gun damage is now level-only (gear no longer raises it); regen must
  skip downed players (W2 told). W1 kits finished (branch worktree-agent-a2c3c8f7781663444): 75 named
  Quartermasters (92002-92076), ledger item 93001 (op 30), loadout v3 with 6 talent picks
  (talent1..6 columns), launcher re-asks on 403 — merge after main's build; ship launcher via publish.py.
  W8 started.
- W1 merged (20a7bcfbc). W4 merged (132396871; union-resolved Chat/CMake/FusionMgr.h conflicts).
  Main's first link failed because the fusion library globs sources at configure time: reconfigure
  build/server and build/fusion after merges that add files. W4 numbers to review with Justin:
  creature health normal x2, elite x3.5, dungeon boss x32, raid boss x80 (assumes ~600 gun DPS at 60);
  knobs Fusion.Danger.Health/Damage in mangosd.conf. Deploy must copy the new catalog.json
  (sentry_gun_mp) to <DataDir>/fusion on dist and the VPS. W9 started from main.
- W7 and W2 merged via the `integrate` worktree (../aw-integrate) then fast-forwarded main (55f6a587f).
  Hand merges: gun hit keeps W3's weak-spot roll + W7's round damage scale, both GunProcs and RoundHit;
  fork ui_items keeps both ledger_use and downed_item_use (+ tests); regen skips downed players.
  Minor (deferred): armor-piercing rounds' give-back uses the raw armor cut, so with FMJ it slightly
  over-compensates. SQL to apply locally and on the VPS: 05, 06, 07, 10 (after 04).
- API usage limit hit ~05:30 cut off W5, W6, W8, W9 mid-work; resumed after reset (their commits kept).
- Fixed: arsenal names lacked W4's sentry_gun_mp (5e4922395). W8 merged (0aab4a6b6): 385 fusion tests,
  286 client tests, server builds. Local deploy needs SQL applied BEFORE the server starts (W8's code
  reads a new world table at startup) — deploy script fixed; ship-server.sh already does stop→SQL→start.
- Live-tested on local (Test at Goldshire): `.fusion stats`, rounds load (INCENDIARY tag), dropgun
  (Thorium Siege Rifle on the ground), downed view + countdown + back up, Sentry Gun killstreak deploy,
  KITS window (5 kits, lore names, origins, combat stats), achievements Prestige tab, prestige/TDM
  reports. Polish noted: the downed red overlay reads as a solid frame; K also opens WoW's Skills pane.
- W9 merged (247c92495): 3 housing sites / 304 plots / 11 launch points load; live: Azshara Crater site
  NPCs and site report OK; Thalassian plateau loads and is walkable but is bare placeholder terrain
  (needs scenery); Hyjal gate loads. W9 used item ids 93400-93405 (deeds) and creature guids 9260001+.
  Test restored to Goldshire (-9449.02, 63.02, 56.02) via the DB while offline.
- W6 merged (a7986aaf1): ~105/189 options built as written, ~80 partial (status in CLASS_CATALOG.md §15);
  no SQL (trainer lists in code). Chat.cpp whole-file conflict was CRLF vs LF: took main's + W6's two commands.
- Polish (a1464b809): downed vignette in rings; K/Y owned keys (K unverified via probe).
- W5 merged (de45452cf). All nine packages in main; 429 fusion tests, 293 client tests, server + fork build.
- Bugs found in integration and fixed: downed client called a Lua GetItemCount the fork lacks (bag scan
  now); SQL 08 + the code's companion range started at 91001 and wiped/hijacked Construction's items
  91001-91099 (now 91100-91999, with a test); SQL 04 re-run collided with 05's guids (fixed);
  ship-server.sh hung 2 h on an ssh host-key prompt (now BatchMode + accept-new) and stopped on a
  failing SQL file with the servers down (now reports and still starts).
- SHIPPED: VPS server rebuilt (WSL, 6 jobs — more crashes WSL), world SQL 01-12 applied, clean start;
  client + launcher published as version 2026-10-07 10:10 with release notes; site installer updated.
