# Azeroth Warfare server

Modern Warfare 2 combat on a 1.12.1 world: this is [vmangos](https://github.com/vmangos/core) with
the fusion rules (`src/fusion`, hooked in by `src/game/Fusion`). Play it with
the game client in this repository's `client/` (on the benilla fork in `benilla/`), which fires
real rounds the server referees.
A stock 1.12 client can still log in and plays plain vanilla.

## What you need

- A Linux box (or WSL2) with Docker and Docker Compose.
- Your own **WoW 1.12.1 client (build 5875)**, for the map data the server extracts.
- About 15 GB of disk: the world database, maps, vmaps and mmaps.

## Set it up

From this folder (`azeroth-warfare/`):

```sh
# 1. Build the image (compiles the server, its tests and the map extractors; 20-40 minutes).
docker compose build setup

# 2. Extract the map data from your client into ./data (mmaps take an hour or more;
#    add --no-mmaps for a first try, creatures then walk straight lines).
./scripts/extract-data.sh /path/to/WoW-1.12.1

# 3. Start the database and load it (downloads the vmangos world snapshot once).
#    Set AW_PUBLIC_ADDRESS to the address your friends connect to.
export AW_DB_ROOT_PASSWORD=pick-one AW_PUBLIC_ADDRESS=203.0.113.7
docker compose up -d db
docker compose --profile setup run --rm setup

# 4. Start the login and world servers.
docker compose up -d realmd mangosd

# 5. Make accounts (gmlevel is optional; 3 is a game master).
docker compose run --rm realmd /opt/aw/scripts/create-account.sh alice secret
docker compose run --rm realmd /opt/aw/scripts/create-account.sh you secret 3
```

The world server's console works too (`docker compose attach mangosd`, then
`account create alice secret`; Ctrl-P Ctrl-Q detaches).

Open ports 3724 (login) and 8085 (world) to your friends.

## The weapon table

The server reads `data/fusion/weapons.json`. The first start copies in the seed table
(`contrib/fusion/weapons.json`), built by `contrib/fusion/make_seed_weapons.py` from approximate
public MW2 numbers: 43 guns with damage, falloff ranges, fire rate, magazines, reloads, move
speeds, hit-location multipliers and MW2 unlock ranks. Edit it or regenerate it, then restart
mangosd. Give the client the same file (`MW2_FUSION_WEAPONS`), since the two agree on weapon ids.

## The rules, and where to tune them

All in `etc/mangosd.conf` (from `src/mangosd/mangosd.conf.dist.in`, section AZEROTH WARFARE):

| Setting | Default | What it does |
|---|---|---|
| `Fusion.Enable` | 1 | Gunfire packets, stance aggro and speed, ammo, killstreaks, the FFA flag |
| `Fusion.Damage.PvE` | 1.0 | MW2 damage against creatures: par creatures die in MW2's shots-to-kill |
| `Fusion.Damage.PvP` | 0.4 | Stretches player time-to-kill to 0.6-1.0 s between equals |
| `Fusion.PvP.LowLevelProtection` | 1 | No shooting players 10+ levels below you unless they started it |
| `Fusion.CatalogFile` | "" | The content catalog (guns, attachments, camos) with MW2's own numbers; empty means `data/fusion/catalog.json`, refreshed from the image every start. `contrib/fusion/make_seed_catalog.py` makes it from your MW2 install |
| `Fusion.Admin.MinSecurity` | 3 | Accounts at this GM level or above count as level 60 for every gun, round and perk unlock (an admin test character). 4 or more turns it off for a GM 3 account |
| `DurabilityLoss.Enable` | 0 | No durability |
| `Rate.XP.Kill` | 0.4 | Questing outpaces grinding |

The numbers behind each rule are in `src/fusion` with a unit test each (`fusion_tests`, run by the
Docker build): damage and health conversion (`Damage.cpp`), hitboxes and lag compensation
(`HitValidation.cpp`), stance aggro (`Detection.cpp`), speeds, sprint, mantle and the knife
lunge (`Movement.cpp`: MW2's sprint ×1.5, crouch ×0.6, prone ×0.15), the client clock sync for
lag compensation (`Clock.cpp`), unlocks for admins (`Access.cpp`), PvP and the FFA
flag (`Pvp.cpp`), killstreaks (`Killstreak.cpp`), ammo and the quartermaster (`Ammo.cpp`), XP and
regen (`Progression.cpp`), creature profiles (`Creatures.cpp`).

## Kits (Create-a-Class)

Each character has up to five kits (`character_fusion_class`, each with six talent picks for the
class trainers' options) and an equipped one (`character_fusion`, with the Field Quartermaster's
Ledger's cooldown). The client's kit editor (K) sends `CMSG_FUSION_LOADOUT` (831); the server checks
every gun, attachment and camo against the catalog and the player's level (`Fusion/Loadout.cpp`),
saves, and equips at once out of combat, else on leaving combat or respawning, answering with
`SMSG_FUSION_LOADOUT` (832). Kits are viewed anywhere but edited and switched only beside a
Quartermaster (one per inn and class trainer area, `sql/custom/azeroth_warfare/05_quartermasters_named.sql`),
within the ledger's window, or by a GM (`Fusion/Kits.cpp`). Talking to a Quartermaster refills
ammunition, and every life starts with full reserves.

## Load test

From the benilla fork (`benilla/` at the repository root):

```sh
for i in $(seq 1 100); do
  docker compose run --rm realmd /opt/aw/scripts/create-account.sh awbot$i awbot
done
cargo run --release -p benilla-protocol --bin fusion-bot -- localhost --bots 100 --seconds 300
```

Each bot logs in (alternating factions, mixed stances), raises its FFA flag and fires the starter
rifle at the nearest unit; the bot prints shots per second and the server's hits, headshots,
kills and hurts every 5 seconds.

## Not done yet

- Killstreak rewards are earned and announced, but none acts on the world yet (UAV, airstrikes).
- Creatures keep vmangos's AI, plus the fusion's chase speed (beasts and brutes close faster on a
  distant shooter). The other profiles (soldiers taking cover, casters, the unreachable-target
  rules) are written and tested in `src/fusion/Creatures.cpp` but not yet driving behaviour.
- Spells keep their 1.12 numbers; the minimum-value rule for spells against gunfire
  (`MinSpellValue`) is ready for a pass over the spell table.
- Creatures are hit where they stand now; players are rewound to where the shooter saw them.
