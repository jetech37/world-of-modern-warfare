# Security and exploit review, October 2026

Done before open public playtesting, on branch `exploit-review`. It covers the world server's own
systems (`server/src/game/Fusion/`, area looting, the rules library `server/src/fusion/`), the
web gate (`vps/gate.py`, `vps/aw-admin.sh`, `vps/Caddyfile`) and the launcher. The game client
is treated as untrusted: anyone can modify it and send any bytes.

## In plain words

- **The worst problem:** a modified client could crash the whole world server with one shot. It
  did this by claiming its bullet started far off the map. The same trick let a player start a
  rocket, grenade, flashbang or thrown knife right next to a target behind a wall, or shoot down
  building pieces from across the map. **Fixed.** A shot now always starts at the shooter's own
  eye, give or take 3 yards (5 when mounted) to allow for lag.
- **Endless ammunition:** a client could fire any gun the account had unlocked, not just the
  two in its kit, and each of those guns had its own full magazine. **Fixed.** Only the kit's
  guns, a picked-up gun, or Last Stand's pistol fire.
- **Server shutdown by companion spam:** summoning and dismissing a companion as fast as the
  network allows used up the server's supply of pet IDs. Running out stops the server. Each
  toggle also wrote to the database. **Fixed** with a pace limit (3 at once, then one every 2
  seconds).
- **Money:** Field Revival Kits and Gnomish Revival Coils sold to vendors for about ten times
  what their reagents cost, so crafting them was a money press. **Fixed** by lowering their sell
  price below the reagents' value.
- **Lag and database floods:** several windows (Construction, achievements, prestige, the class
  page) and actions (setting a lock, swinging a door, taking a piece down) did real work on
  every message, with no limit. **Fixed** with one shared allowance: 10 at once, then 5 a
  second. That is plenty for a person and nothing for a bot.
- **Smaller game fixes:**
  - Swapping gathering tools no longer skips the swing wait.
  - Splash spells cost mana even when nobody is in reach.
  - A steward can't sell a plot the player never visited.
  - Nothing can be built from a flight path or a boat.
- **The web gate:**
  - Limits now count an IPv6 home or server as one address. Before, one machine could take a
    fresh address for every guess or sign-up.
  - A few malformed or stalled requests no longer tie up the gate.
  - The launcher refuses an update manifest that tries to write files outside the game folder.
- **Decisions, now taken and fixed** (branch `exploit-followups`, under
  [Follow-up fixes](#follow-up-fixes-decisions-taken)):
  - Reloads take time on the server: a reload can't finish sooner than 0.7 of the gun's own time
    after its last shot.
  - Pieces built for free pay nothing back, and salvaged materials and token-shop goods sell for
    nothing.
  - No companion or catalyst drops from gray creatures or summons (critters excepted, at their
    full rate).
  - The 100g prestige milestone pays once an account; companion duel experience is capped per
    account.
  - Rockets and grenades are held closer to their fire rate.
  - A plot owner breaking a builder's piece sends the salvage to the builder.
  - The web gate slows down guessing one account's password from many addresses, without
    letting anyone lock the player out.
  - Lock codes stay 4 to 8 digits.

Area looting (multi-corpse windows, coin, rolls, group loot, despawns), the loadout checks (pro
perks, attachments, stacking), downed revives, pickups, rocket reports, sky streaks, feedback,
the stash and repair maths, and `aw-admin` all held up: no exploit was found there.

## Findings

Severity:

- **Critical:** crashes the server or breaks fights for everyone.
- **High:** a big unfair edge, or a reliable way to stop the server.
- **Medium:** a real gain or lag source with limits.
- **Low:** small, or hard to use.

| # | Severity | Area | Finding | Status |
|---|---|---|---|---|
| 1 | Critical | Gunfire | Off-map shot origin reaches the map's height and line-of-sight asserts: server crash | Fixed |
| 2 | Critical | Gunfire | Shot origin trusted for blasts, grenades, launchers, flash/stun/smoke, C4 and semtex throws: explosions anywhere, through walls | Fixed |
| 3 | High | Gunfire | Thrown knife (one-hit kill) from an origin beside a target behind cover | Fixed (by #2) |
| 4 | High | Construction | Bullets damage pieces from an origin anywhere (with a DB write each) | Fixed (by #2) |
| 5 | High | Gunfire | Any unlocked gun fires, each with its own full magazine: endless ammo, every gun at once | Fixed |
| 6 | High | Companions | Summon/dismiss spam uses up pet GUIDs (the server stops when they run out) and floods the DB | Fixed (pace limit; see note) |
| 7 | Medium | Gunfire | Shots stamped in the past all go off at once: a launcher's whole magazine in one packet | Fixed |
| 8 | Medium | Gathering | Switching tools between strikes skipped the swing wait: instant harvesting | Fixed |
| 9 | Medium | Spells | A splash spell with nobody in reach hit everyone near the point without paying its cost | Fixed |
| 10 | Medium | DoS | No pace limit on costly window asks and actions (Construction window, stash total query, lock set, door, remove, reinforce, achievements book, prestige saves, class page) | Fixed |
| 11 | Medium | Economy | Revival kits sold for 25s / 40s, from reagents worth about 2-4s: a money press | Fixed |
| 12 | Medium | Gate | IPv6 counted per address: per-address limits (wrong passwords, sign-ups, crash reports) passed by rotating addresses in one /64 | Fixed |
| 13 | Medium | Gunfire | Reload is instant on the server (the client says when it finished) | Fixed (follow-up) |
| 14 | High* | Construction | Pieces built free (`Fusion.Build.Free`, or a GM's free build/reinforce) still refund, salvage and pack into materials, and salvage sells | Fixed (follow-up) |
| 15 | Low | Homestead | "Claim the plot I visited" took any plot id from the client | Fixed |
| 16 | Low | Construction | Building from a flight path or boat (walling off roads in passing) | Fixed |
| 17 | Low | Gate | A negative Content-Length read until the client hung up; refused crash bodies held whole in memory; stalled clients held a thread for good | Fixed |
| 18 | Low | Launcher | Manifest paths with `..`, a drive or a root were written outside the install | Fixed |
| 19 | Low | Decoders | The class meters decoder kept entries from a count the message didn't hold (client side) | Fixed; every decoder now fuzzed |
| 20 | Low | Gunfire | Fire-rate check allows 1.25x the gun's rate (0.8 slack for jitter) | Fixed for explosives (0.9); bullets keep 0.8 |
| 21 | Low-Med | Economy | Companion and catalyst drops from gray mobs and critters (plus 3x respawns) | Fixed (follow-up; critters see #21 below) |
| 22 | Low | Economy | Prestige milestone 0 (100g, no prestige needed) is per character: an alt faucet | Fixed (once an account) |
| 23 | Low | Economy | Salvaged materials and token-shop goods sell to vendors | Fixed (sell price 0) |
| 24 | Low | Economy | Gold boosts stack: Scavenger 2x, companion trait 1.25x, 3x respawns | Balance note |
| 25 | Low | Construction | A plot owner who shoots a permitted builder's pieces keeps the salvage | Fixed (to the builder) |
| 26 | Low | Locks | 4-digit codes: about 10 guesses an hour per lock (falls in weeks); anyone can keep a lock in lockout | Decided: kept as is (4-8 digits) |
| 27 | Low | Companions | Duel: a full heal kept by leaving early; outside damage can't kill a duelling companion | Open (low) |
| 28 | Low | Companions | Kill XP goes to the companion out at the kill (swap in a level 1 for the last hit) | Open (low) |
| 29 | Low | Companions | A companion ability's duration clamp shortens another caster's aura of the same spell | Open (low) |
| 30 | Low | Companions | Duel XP cap (5 a day) is per character pair: alts multiply it | Fixed (per account, kept across restarts) |
| 31 | Low | Companions | Trading a companion to a character with saving off wipes its progress | Open (edge) |
| 32 | Low | Gathering | Strikes check distance (5.5 yd) but not line of sight | Open (low) |
| 33 | Low | Gate | No per-account wrong-password limit (only per address); a good login stays cached 10 minutes after a ban or password change | Fixed (soft per-account limit); cache unchanged |
| 34 | Info | Core | `IsInTrade()` is only set during the trade accept, so item counts include items in an open trade window. Not exploitable today. | Hardening note |

\* #14 is High only while `Fusion.Build.Free` is on, or after a GM builds or reinforces a
player's pieces for free. With the setting off and no GM help it pays nothing extra.

"Fixed (follow-up)" and the other decided items were fixed after the review on branch
`exploit-followups` (see [Follow-up fixes](#follow-up-fixes-decisions-taken)).

## The fixes in detail

### 1-4. Shot origin (`HandleShots`, `FusionMgr.cpp`)

**How it worked before:**

- The client sends each shot with an origin and a direction. The decoder only checks that the
  numbers are finite.
- Only the per-body hit test compared the origin with the shooter's eye. Every other use of the
  origin took it as sent:
  - explosions and the grenade launcher's arc;
  - thrown frags, semtex, C4, flash, stun and smoke;
  - thrown knives;
  - building-piece damage;
  - the world segment used for tracers and impacts.
- An origin outside the map (for example x = 20000) reached `Map::GetHeight` and
  `isInLineOfSight`, which `ASSERT` on valid coordinates. That ends the world thread.

**Exploit:**

- One packet with an off-map origin crashes the server.
- An origin 1 yard from a target behind a wall puts the rocket, grenade or knife right there.
- An origin beside an enemy wall shoots it down from safety.

**Fix:**

- `Fusion::ShotOrigin(claimed, eye, tolerance)` (`HitValidation.h`) keeps the client's origin
  only when it is finite and within the existing tolerance of the server's eye: 3 yards, or 5
  when mounted. Anything else becomes the eye.
- `HandleShots` applies it to every shot before anything else, so all the paths above get the
  checked origin.
- An honest client's shots are unchanged: they already start within the tolerance, or the hit
  test would have refused them.

**Tests:** `security_a_shot_starts_at_the_shooters_eye`.

### 5. Guns not in hand (`HandleShots`)

**How it worked before:**

- `EffectiveWeapon` returns any catalog gun.
- The shot path only checked that the account had unlocked it.
- `AmmoFor` makes a full magazine and reserve the first time each gun id is used.

**Exploit:** fire every unlocked gun, including launchers, by changing the weapon id on each shot.
That gives endless ammunition.

**Fix:**

- `Fusion::GunInHand` limits primary and secondary guns to these, when a class is equipped:
  - the equipped class's primary or secondary;
  - a picked-up gun;
  - Last Stand's pistol while down.
- Underbarrel weapons were already checked against their gun.

**Tests:** `security_only_a_gun_in_hand_fires`.

### 6. Companion summon spam (`UseCompanionItem`, `RideCompanion`)

**The problem:**

- Each summon takes a new pet GUID from the map, and these are never reused while the server runs.
- At 16.7 million on a map, the core calls `World::StopNow`.
- Each toggle also wrote one or two database rows.
- The anti-flood check allows about 300 packets per tick.

**Fix:** a per-player allowance, `companionSummons`: 3 at once, then one every 2 seconds, for
summon, put away and ride. Using a fainted companion's item (the Reviving Draught prompt) is not
counted.

**Remaining risk:** at the allowed pace, one player would still need months of nonstop toggling to
run a map out. The cause is in the vmangos core, which also hands out GUIDs for vanilla pet
summons. Regular restarts (every deploy) reset the counter.

### 7. Saved-up shots (`HandleShots`)

**The problem:**

- The client stamps each shot's time.
- The fire-rate check only compares each shot with the previous one.
- After waiting, a client could send up to 16 shots stamped over the past seconds, and all of
  them were accepted at once.
- Bullets were already held to the 1-second rewind window, but explosives were not.

**Fix:** `Fusion::ShotFresh` drops any shot stamped more than 1 second before the server's now,
for every weapon.

**Tests:** `security_shots_saved_up_dont_all_go_off_at_once`.

### 8. Tool switching (`SetUtilityTool`)

**The problem:**

- Readying a tool reset the strike progress, including the last swing's time.
- Alternating two tool ids between strikes skipped the swing wait.
- Each strike counted at once: instant mining, herbing and skinning, and fishing-cast spam.

**Fix:** `Fusion::Gathering::ToolChanged` clears the count but keeps the last swing's time.

**Tests:** `security_changing_tools_keeps_the_swing_wait`.

### 9. Free splash spells (`FusionSpellcasting.cpp`)

**The problem:**

- When no enemy was within the spell's reach of the caster, the splash only set the cooldown.
- It then cast the spell, triggered and free, on everyone near the point.

**Fix:** the spell is now paid as a release is paid (`PayRelease`: power, cooldown and the global
cooldown). Without enough power it is refused.

### 10. Costly asks (`fusion/Throttle.h`, `FusionMgr::HandleChannel`)

One shared allowance per player, `costlyAsks`: 10 at once, then 5 a second.

**What it covers:**

- The Construction window ask and act (ops 52/53). Each one copies the plot registry, scans the
  map's pieces and runs a blocking `SUM` query.
- Reinforce.
- The achievements book. Each request rebuilds every page.
- Every prestige op except the scoreboard. Title and tracer changes each wrote 2 or more
  `REPLACE` rows.
- The class page ask.
- Setting a lock: 2,049 SHA-256 rounds, a write and a broadcast.
- Opening a portal's menu.
- Swinging a door: a scan of the map's players and a broadcast.
- Taking a piece down: the support solver.

**What it leaves alone:** gunfire, movement, spell aim, placing (it has its own 150 ms pace),
trying a code (its own limiter), and the scoreboard.

**Tests:** `security_costly_asks_are_throttled_and_gunplay_is_not`.

### 11. Revival kit prices (`server/sql/custom/azeroth_warfare/06_downed.sql`)

| Item | Old sell price | New sell price | Reagents' vendor value |
|---|---|---|---|
| Field Revival Kit | 25s | 2s | about 2-3s |
| Gnomish Revival Coil | 40s | 3s50c | about 4s |

The kits are meant to be used, not sold. **Re-apply the file** to the world database for this to
take effect.

### 12, 17. Gate (`vps/gate.py`)

- **IPv6:** every per-address limiter now counts an IPv6 address by its /64 (`network_of`). One
  home line or rented server is handed a whole /64. The limiters are:
  - wrong download passwords;
  - sign-up tries;
  - accounts per address;
  - crash reports per address.

  The ban check and the log still see the full address.
- **Content-Length:** a missing, unreadable or negative value is read as 0 (`body_length`).
- **Refused crash reports:** the body is read in 64 KB pieces and thrown away, not held whole.
- **Stalled clients:** the handler has a 60-second socket timeout.

**Tests:** in `vps/test_gate.py`:

- `Addresses`;
- `test_ipv6_wrong_tries_count_by_the_64`;
- `test_accounts_per_ipv6_network`;
- `test_a_negative_length_is_answered_not_waited_on`;
- `test_a_refused_big_report_is_still_answered`.

### 15, 16. Plots and building

- **"Claim visited":** now needs the plot the player last walked on. Admins are exempt.
- **Building while moving:** `BuildPlace` refuses while on a taxi or a transport. Admins are
  exempt.

### 18. Launcher

`inside_install` refuses any manifest path that:

- is empty;
- is absolute or starts at a root;
- names a drive or an NTFS stream (contains `:`);
- has an empty, `.` or `..` part.

The update stops on such a path. **Test:** `manifest_paths_stay_inside_the_install`.

### 19. Decoder fuzzing

`server/src/fusion/tests/FuzzTests.cpp` feeds every wire decoder (client to server and server to
client) three kinds of input:

- random bytes of many lengths, behind every first byte;
- buffers of 0xFF and 0x00;
- every cut of a long random message.

A decoder must not throw or hang, and must not make more entries than the message has bytes. It
found the class meters decoder keeping the partial entries of a truncated message. That decoder
runs only on the client, and the count was capped at 255; it now makes none.

## Follow-up fixes (decisions taken)

Done on branch `exploit-followups`. Regression tests are in `fusion_tests` (`ReloadTests.cpp`,
`SecurityTests.cpp`, `PlotTests.cpp`, `CompanionTests.cpp`) and `vps/test_gate.py`
(`AccountGuardCheck`). **Re-apply the world SQL** (`24_construction_rules.sql`,
`26_handed_out_goods.sql`; every ship does) for #14 and #23. The characters-database columns and
tables are added by the server on start.

### 13. Reload time on the server (`fusion/Reload.h`, `FusionMgr::HandleState`, `SettleReload`)

- **First, the numbers.** The client times a reload from the held definition's WeaponDef
  (`ReloadTiming`), times `reload_scale` (Sleight of Hand: 0.5), times its handling's reload share
  (ranged attack power, which the server holds to 0.8-1). Sleight of Hand and the handling share
  are now the server's too (`Fusion::ReloadFactor`). Checked against every definition in the MW2
  install (`mw2_export`):
  - Every gun's own reload matched the catalog.
  - **The launchers did not:** the catalog kept the old table's times (AT4, Stinger and Javelin
    4.0 s, RPG 3.0 s), the client uses MW2's (3.0, 3.0, 3.0 and 3.292 s). The AT4, Stinger and
    Javelin would have held honest reloads to within 7% of their real time. They now take MW2's
    times (`make_seed_catalog.py`, `catalog.json`).
  - Combinations (akimbo, attachments) are definitions of their own. None reloads quicker than
    its gun today except by a hair (the M9 with a tactical knife, 1.625 s against 1.63), but the
    catalog now carries each one's `reload_time` / `reload_empty_time`, and `Catalog::Effective`
    takes the **quickest** of every definition the client could be holding (its own, the pairs
    it falls back to with three attachments, the base), so the server's time is never longer
    than the client's.
- **The rule.** Every round fired notes the time (the server's clock, on arrival). A "reload
  finished" that arrives sooner than **0.7 x** the quickest honest time after the gun's last round
  is not thrown away: it waits, and lands when that time comes (`ReloadPace`). Each shell of a
  shell-by-shell reload needs 0.7 x a shell's time. An honest client is never held up: its reload
  takes the full time, and both messages travel with the same lag.
- **Perks and stats.** The factor used is the quickest of the last 10 seconds (`RecentLeast`), so
  a reload begun with Sleight of Hand or a ranged-power buff that ends mid-reload still counts it.
- **Tests:** `reload_*` in `ReloadTests.cpp`.

### 14. Free-built pieces pay nothing back (`Build::Piece::paid`)

- Each piece records what was paid for it: a bit for the piece's own recipe and one per
  reinforcement tier (`fusion_build_piece.paid`, added with `ADD COLUMN IF NOT EXISTS`, default
  15 = paid in full, so **every existing piece pays out exactly as before**).
- Built free (`Fusion.Build.Free`, or a GM) clears the piece's bit; a tier put on free clears that
  tier's bit, a paid one sets it.
- Refund, salvage and plot packing (`Construction::Refund(Piece)`, `Salvage(type, tier, paid)`,
  `Plots::Pack`) give back only the paid parts. A free piece breaks for nothing (not even the one
  main material salvage otherwise always gives).
- Taking a piece down now always refunds its paid parts to its builder, also when a GM or a free
  realm takes it down (that used to give nothing, paid or not).
- Salvaged materials (93600-93649) also sell for nothing (#23).
- **Tests:** `security_pieces_built_free_pay_nothing_back`, and the `Pack` checks in
  `PlotTests.cpp`.

### 20. Explosives' fire rate (`Fusion::FireRateSlack`)

Anything with a splash (launchers, the underbarrel grenade launcher) is held to 0.9 of its fire
time (1.11x at most); bullets keep 0.8 for jitter. **Test:**
`security_explosives_are_held_closer_to_their_fire_rate`.

### 21. Companion and catalyst drops (`Companions::DropsCompanionGoods`)

- Nothing from a summon or pet (a temporary summon, anything with an owner, a totem).
- Nothing from a creature gray to the highest-level member of the tapping group.
- **Critters:** they are always gray, but the alley cat and the wild rabbit drop only from
  critters, so the gray rule spares them. This follow-up first lowered critters to 0.2% and kept
  catalysts off them; the owner saw no reason to restrict critters (2026-10-21), so they are back
  at their normal 0.5% companion chance with no critter-only rule. No critter catalyst exists
  (`Companions::CatalystFor`), so they still drop none. The ban stays for summons, pets, totems
  and gray mobs. Set the critter rate in `Companions::DropChance` to change it.
- **Test:** `security_no_companion_goods_from_gray_kills_or_summons`.

### 22. Prestige milestone 0 (`Prestige::kAccountMilestones`)

The Triple Threat (100g, 10 tokens, a companion, a title) is claimed once an account. The
account's claims live in `account_fusion_prestige.milestones` (added on start; every character's
claim from before is folded into its account, so the account's other characters won't claim it
again). The other milestones need prestige ranks and stay per character. **Test:**
`security_the_first_prestige_milestone_pays_once_an_account`.

### 23. Handed-out goods' vendor price

Salvaged materials (written by `24_construction_rules.sql`) and the token shop's items
(`26_handed_out_goods.sql`) sell for 0. A test keeps the SQL list in step with `Prestige::Shop()`.
**Test:** `security_handed_out_goods_sell_for_nothing`.

### 25. Plot owner and builders' salvage (`Plots::SalvageToBuilder`)

When the breaker is the plot's owner (any of the account's characters), a builder on it or a GM,
and not the piece's builder, the salvage goes to the builder's stash at the Plot Steward, as
taking it down already sent the refund. **Test:**
`security_a_plot_owner_breaking_a_builders_piece_sends_the_salvage_to_the_builder`.

### 26. Lock codes

Kept as they are (4 to 8 digits, the per-guesser limiter). Nothing changed.

### 30. Companion duel experience (`Companions::PayDuelWin`)

At most 5 paid wins a day for the winner's **account** (all its characters together), whoever the
foe. Kept across restarts in `account_fusion_companion_duel` (one row an account). **Test:**
`security_companion_duel_experience_is_capped_per_account`.

### 33. Gate: wrong passwords for one account (`vps/gate.py`, `AccountGuard`)

- **Why neither simple fix works:** a per-address limit alone lets a guesser with many addresses
  try one account without end; a per-account lockout alone lets anyone keep a player from
  downloading by getting their password wrong on purpose.
- **What it does:** while an account has fewer than 50 wrong passwords in 15 minutes from
  everywhere, nothing changes. Past that, the account is "under attack": each address (an IPv6
  /64 counts as one) gets at most 3 wrong tries for that account in the window, counted against
  that address only, and the next is answered 429 without checking. A thousand guessing addresses
  get 3 guesses each instead of 10.
- **Why the player isn't locked out:** the guessers only use up their own addresses' tries. The
  player's own address keeps its tries, and the right password still gets in. An address that
  has signed in to the account in the last 30 days is never held up by it at all, however many
  guesses come from elsewhere.
- **What's left:** a guesser on the same network as the player (a shared NAT) could use up that
  network's 3 tries; an address that signed in before still gets in. The limits live in memory (a
  gate restart forgets them). The login cache is unchanged at 10 minutes.
- **Tests:** `AccountGuardCheck` in `vps/test_gate.py`.

## Decisions needed (as written at the review; all taken above)

1. **Reload time on the server (#13).**
   - **The problem:** the client sends "reload finished" when its own timer ends, and the server
     fills the magazine at once. A modified client sends it right after the last shot, so it
     never pauses to reload.
   - **Recommendation:** the server remembers when each gun last fired and refuses a reload
     finish sooner than about 0.7 x the gun's reload time after it. This needs the server's
     reload times to match the client's.
     - The client takes them from the WeaponDef (`client/src/weapon.rs` `ReloadTiming`), with
       perks, attachments and the stats' `ReloadTime` multiplier.
     - The server has `WeaponProfile::reloadTime` / `reloadEmptyTime`.
     - Compare the two before turning this on, or it will cut honest reloads.
2. **Free-built pieces paying out (#14).**
   - **The problem:** a piece doesn't record whether it was paid for. Refund (50%), salvage (40%
     when destroyed) and plot packing all pay out either way.
   - **Recommendation:** add a `paid` flag (or the materials actually spent) to the piece and its
     `fusion_build_piece` row. Refund, salvage and pack only from that. Until then, leave
     `Fusion.Build.Free` off on the public server, and have GMs not build or reinforce for
     players.
   - **Alternative:** give the salvaged items (93600-93626) a sell price of 0 so they are only
     building currency. That also covers #23 for salvage.
3. **Fire-rate slack (#20).** The 0.8 factor allows a sustained 1.25x rate. Tightening it to 0.9
   would cut that to 1.11x, at some risk to players with jittery connections. Recommendation:
   0.9 for explosives only.
4. **Companion and catalyst drops (#21).** Recommendation: no companion or catalyst drop from
   creatures that give no XP to the killer (gray), totems or summons. Lower the critter rate.
5. **Prestige milestone 0 (#22):** 100g and an item, per character, with no prestige needed.
   Recommendation: claim it once per account.
6. **Vendor value of handed-out goods (#23):** token-shop goods and salvaged materials sell to
   vendors. Recommendation: sell price 0 for token-shop items.
7. **Plot owner and builders' salvage (#25).** Recommendation: when the breaker is the plot's
   owner, or holds a permit on it, and is not the piece's builder, send the salvage to the
   builder's stash.
8. **Lock codes (#26).**
   - **The problem:** 4-digit codes allow about 10 guesses an hour per lock, and the limiter
     forgets on a restart. Anyone can also keep a lock in lockout, which shuts out its legitimate
     users.
   - **Recommendation:** a 6-digit minimum for new codes, and a lockout per guesser only (which
     the limiter already keys on) with a longer cap.
9. **Duel XP cap per pair (#30).** Recommendation: count it per winner's account per day.
10. **Gate (#33).**
    - **Recommendation:** a soft per-account limit on wrong download passwords (for example 50 in
      15 minutes) to slow distributed guessing.
    - **Trade-off:** it would let a guesser hold up that player's first download.
    - **Login cache:** cutting it from 10 to 2 minutes means a ban or a new password takes effect
      sooner, at the cost of more password checks during an install.

The remaining open Low items (#27-#29, #31, #32) are small. Suggested fixes:

- **#27:** cap the health share at the pre-duel value on leaving a duel, and clamp only the foe
  pet's damage.
- **#28:** credit the item recorded at the hit.
- **#29:** look the aura up by its caster.
- **#31:** don't forget the companion in memory when the item object lives on.
- **#32:** a line-of-sight check from the eye.

## What was checked and held up

- **Area looting:**
  - Every take re-checks the corpse: alive, skinned or pickpocketed, distance, may-loot rights and
    permission.
  - Rows must still match what the window showed.
  - Coin is zeroed as it is paid.
  - Master-loot windows are refused.
  - Rolls stay with the group-loot code.
  - Despawned and respawned corpses drop out.
- **Loadouts:**
  - Every field is unlock-checked.
  - Pro bits need the pro unlock.
  - Duplicate attachments are refused.
  - Stacking and attachment room are capped.
  - Saves are rate limited.
- **Downed:** a kit must be owned, the target in range and friendly, the reviver standing still,
  and the kit is consumed.
- **Pickups:** reach, map and instance are checked.
- **Rocket reports:** each step is speed-limited and the impact recomputed.
- **Sky streaks:** the origin is clamped and the streak must be earned.
- **Prestige and achievements:**
  - Prestige needs a confirmed nonce, with the standing re-checked.
  - Title and tracer ownership are checked.
  - Achievements are granted only by the server.
- **Companions:**
  - Ownership through the player's own bags.
  - Range and line-of-sight checks.
  - Stable-master checks for breed, revive and repick.
  - Breeding limits.
  - Money checked before it is charged.
  - Reviving Draught: 15s to buy, 3s75c to sell.
- **Construction:**
  - Ownership and permits are checked.
  - Placement reach and sight are checked.
  - Repair rounds up and costs more than a refund returns.
  - The stash is decremented before it is given out.
  - Crafting takes fresh items only.
  - Upkeep is worked out in 64 bits.
- **Money:** every `ModifyMoney` charge is checked against the player's money on the same thread.
- **Feedback:** prepared statements and an account limiter.
- **`aw-admin`:** every value that reaches SQL is checked against a fixed list or a strict
  pattern, or passed as hex.
- **The gate:**
  - SQL values are checked against strict patterns.
  - Invite codes are claimed by one conditional `UPDATE`.
  - Crash report ids are made by the server, so there is no path traversal.
  - `X-Forwarded-For` takes Caddy's last entry.
