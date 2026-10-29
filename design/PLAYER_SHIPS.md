# Player ships: airships and sailing ships

Players commission, own and sail their own ships: a **sailing ship** on the sea and lakes, and an
**airship** in the open sky. A ship costs about as much as a rich guild's bank, so most players
ride one long before they own one. It's worth the price: it carries a party across a continent,
other players can walk its moving deck, and the ship stays where it was left across restarts.

This file is the design. The rules live in `server/src/fusion/include/fusion/Ships.h` (tested
by `ShipTests.cpp`), the game glue in `server/src/game/Fusion/FusionShips.cpp`, the world rows in
`server/sql/custom/azeroth_warfare/36_player_ships.sql`, and the client in `client/src/ships.rs`.
The one benilla hook, a server-streamed transport drive, is in
`benilla/crates/benilla-app/src/transport.rs`. "Built" marks what the first slice delivers;
everything else is the plan.

## 1. Acquisition (built)

A ship is commissioned from a **Shipwright** (creature 91810, gossip `npc_fusion_shipwright`),
who stands at a dock. Commissioning takes four things:

1. **A deed.** The Shipwright sells a *Sloop Deed* (100 g) and an *Airship Charter* (300 g). Deeds
   aren't soulbound, so they can be traded or sold on the auction house. A guild can buy one for
   its captain.
2. **Gold.** Paid when the keel is laid: a Sloop costs 400 g and an Airship 1,200 g.
3. **Rare materials** (stock vanilla trade goods, so every profession feeds into a ship):
   - **Sloop:** 20 Mithril Bar, 10 Thorium Bar, 40 Bolt of Mageweave, 20 Thick Leather.
   - **Airship:** 20 Thorium Bar, 6 Arcanite Bar, 4 Gyrochronatom, 2 Enchanted Thorium Bar,
     40 Bolt of Runecloth, 20 Rugged Leather.
4. **Time.** After the keel is laid, the ship takes real time to build: 2 hours for a Sloop,
   6 hours for an Airship. Come back to the Shipwright to launch it.

That comes to roughly 800 g for a Sloop and 2,500 g or more for an Airship at vanilla auction
prices. That's "prohibitively expensive" in a 1.12 economy, and it's meant to be.

Limits: each character owns at most one ship of each class.

**Group and guild effort (next):** a commission open to contributions, where any player can hand
the Shipwright materials toward someone else's keel, with a ledger. Later, guild-owned ships run
off the guild bank, with officer helm rights.

**Shipyard plots (next):** a Construction "Shipyard" structure on a coastal homestead plot (see
`CONSTRUCTION.md`) that does the Shipwright's job at the owner's own dock. The slice uses Shipwright
NPCs only.

## 2. Classes

| | Sloop (sailing ship) | Airship |
|---|---|---|
| Moves on | water: the sea, lakes, rivers deep enough for its draft | air |
| Model (stock) | `Transport_Ship` (the Ratchet to Booty Bay ship), display copied from gameobject 20808 | `Transport_Zeppelin` (the Orgrimmar zeppelin), display copied from 164871 |
| Top speed | 14 yd/s (an epic mount's pace) | 20 yd/s |
| Reverse | 3 yd/s | 4 yd/s |
| Turn | 18 °/s | 26 °/s |
| Climb | | 6 yd/s |
| Hull | 10,000 | 8,000 |
| Upkeep | 5 g a day | 15 g a day |
| Summon to a dock | 10 g | 40 g |

**The sailing ship** holds its keel to the water surface the server reads from the map's liquid
data. A ship never leaves the water: it stops at a shore, a shoal shallower than its draft (3 yd),
or the map edge. Running aground at speed scrapes the hull.

**The airship** keeps at least 10 yd over the ground under it, and climbs no higher than 450 yd
over the ground or the world ceiling (z 1,200). It obeys the **city wards** (`Fusion::Wards`, the
same airspace table skyriding uses): over a city it's held under the ward's ceiling, and
"Unfinished" airspace (the broken map edges) pushes it out, the same way it pushes a rider. Like
the sailing ship, it stops at the map edge. Flying into terrain it can't clear at speed scrapes
the hull.

### Models (patch-9's Wrath assets)

The stock models fit the slice: the ship and zeppelin collision meshes are the ones benilla
already rides as deck colliders. Wrath options for later, by display path (VERIFY each display id
from `GameObjectDisplayInfo.dbc` in patch-9 before using it):

- `World\wmo\transports\transport_gnomish_airship\`: no such WMO exists, but the gnomish
  flying machine M2s (`Creature\GnomishFlyingMachine`) are too small for a deck.
- `Transport_Alliance_Battleship` / `Transport_Horde_Battleship` (Wrath's Valiance, Kraken)
  make good **heavy** sailing ships, as a third class.
- `Transport_Icebreaker_Ship` (the Northrend ferries) would make a mid-size ship.
- `Transport_Zeppelin_Wrath` / the Wrath "Airship" (`Transport_Alliance_Airship` used for the Valiance
  Keep to Stormwind run) would make a heavy airship.
- **Skybreaker and Orgrim's Hammer** (`Transport_Icecrown_Gunship_*`) are too big: about 200 yd,
  past the 250 yd passenger-offset limit vmangos enforces. They're ruled out.

## 3. Crew and passengers (built)

- **The owner pilots, and grants the rest (built).** At any Shipwright the owner toggles the
  crew's rights for each ship (`Fusion::Ships::Rights`, `ShipCrew.h`):
  - the party, and the guild, may take the helm (both off at first);
  - the party, and the guild, may man the guns (both on at first), or anyone aboard may (off);
  - the party, and the guild, may respawn aboard (both on at first).
  The party is the owner's group; the guild is the owner's guild when the owner was last near the
  ship. A helmsman may also anchor. Only the owner rings the bell, opens the hold, repairs and
  summons.
- **Anyone can board.** A player walks or jumps onto the deck. Their client reports the ship's
  transport guid and a deck-local offset, vmangos's `MovementHandler` adds them as a passenger,
  and the server carries them with the ship from then on (`GenericTransport::UpdatePassengerPositions`).
  Stepping off the ship ends the ride. This is exactly how the stock boats and zeppelins work, so
  walking, jumping and fighting on a moving deck all behave as they do on the Booty Bay boat.
- **Pets, companions and mounts (next):** vmangos's transport code can carry followers
  (`AddFollowerToTransport`). Carrying a mount as cargo, and stabling one on board, comes after
  the slice.

## 4. Controls (built)

Standing on a ship you own or crew, a small **Ship** frame shows in the classic style: the ship's
name, hull, speed and state, with **Take the Helm**, **Drop Anchor / Weigh Anchor**, **Ring the
Bell**, **Man a Gun** and **Cargo Hold** (each greyed when it isn't yours to use).
At the helm, your own movement bindings steer (the key table, `LayerBoundKeys`; the stock keys
before it's in):

- **Move forward / back** (W / S) move the engine telegraph one notch: Full Astern, Stop, Slow
  Ahead, Half Ahead, Full Ahead. The notch holds, so the ship keeps sailing with no key held.
- **Turn or strafe left / right** (A / D, Q / E) turn while held.
- **Jump / Sit** (Space / X) climb and descend while held (airship only).
- **Leave the Helm** (the button, or Escape) hands control back. You stay on deck.
- `/ship` shows or hides the Ship frame anywhere.

At the helm, the server roots the pilot to the deck, and the client sends only which keys are held.
The ship's motion is entirely the server's. A pilot who logs out, dies, is teleported or steps off
the deck leaves the helm, and the ship coasts to a stop.

**Anchoring:** anchoring needs the ship below 2 yd/s. An anchored ship doesn't move, and a pilot
must weigh anchor before it sails. An unpiloted ship that's stopped drops anchor by itself.

**Docking:** a ship counts as docked at a Shipwright when it's within 120 yd of them. Docked is
where it can be repaired. "Summon my ship here" moves it to the Shipwright's dock for a fee (see
"Persistence").

## 5. Usefulness and power

- **Fast travel for a group (built):** a full party crosses a continent at an epic mount's pace or
  faster, together, while able to fight.
- **The Ship's Bell (built), a mobile hearth:** the owner can return to the deck from anywhere in
  the world once every 30 minutes, when the ship isn't wrecked. Type `/shipbell`, or press **Ring the Bell** on the Ship frame
  (`/ship` shows it off the deck). It's the slice's "mobile bind point".
- **Cargo hold (built):** a ship's own bank, 16 slots for a Sloop and 24 for an Airship, each slot
  one stack. The owner opens it aboard (**Cargo Hold** on the Ship frame), and the window lists
  the hold's stacks (**Take**) and the bags' goods that may go in (**Stow**). Only plain goods go
  in: nothing soulbound, enchanted, with a random property, charges or durability, so a stack
  comes out as new items with nothing lost (`aw_ship_cargo`, entry and count per slot). An item
  leaves the bags before it enters the hold, never both. A wrecked ship's hold is kept for
  salvage.
- **Respawn aboard (built):** releasing your spirit within 2,000 yd of an anchored ship you own,
  or crew with the respawn right, brings you back to life on its deck at half health, as a bedroll
  does. It's once every 10 minutes per ship, and not while the ship has been hit in the last
  60 seconds.
- **Deck guns (built):** three mountings per ship (`MountsOf`). The Sloop has port, starboard and
  bow M240s; the Airship has port and starboard M240s and a stern RPG rack. A crewman standing by
  one presses **Man a Gun**. The server roots them there and hands them the mounting's arsenal gun
  as a weapon pickup, full, so it fires through the fusion's own shots: the same
  server-authoritative hit validation, fire rate, ammunition and damage as any gun. Leaving the
  gun (the button, Escape, stepping away, dying) gives back what they held before.
- **Hull damage and ship-to-ship combat (built):** any round that hits no body is tested against
  every ship's hull box on the map (`RayVsHull`), except the deck the shooter stands on and ships
  they crew. The nearest hull takes the round's damage. A blast (a rocket, a grenade, C4) hurts
  each hull within its radius, 4 times as much, falling off with distance (`BlastShare`). So a
  Sloop takes about 200 M240 rounds, or 17 rockets. Deck guns fired at another ship make
  ship-to-ship combat. Ships are fair game in the open world, like open-world structures.
- **Loot from a wreck (next):** a wrecked ship drops a share of its cargo.

## 6. Limits (built)

- **Upkeep:** a commission includes 7 days of upkeep, and the owner pays more in 7-day blocks at
  any Shipwright (35 g for a Sloop, 105 g for an Airship). An overdue ship is **neglected**: its
  top speed is halved, and it loses 4% of its hull a day. After 14 days overdue the harbour
  **impounds** it: it's wrecked, and comes back only through salvage.
- **Damage and repair:** the hull wears from scraping ground and from neglect (and later from guns).
  Repairs happen at a Shipwright with the ship docked, at 1 g per 1% of hull for a Sloop and 3 g
  for an Airship. A ship under 25% hull is **listing**, at half speed.
- **Sinking and crashing:** at 0 hull a sailing ship sinks and an airship crashes. Its passengers
  are thrown off; an airship's passengers fall. The wreck is gone from the world, and the row is
  kept as "wrecked". **Salvage** at a Shipwright raises it again at that dock, at full hull, for
  30% of its build gold with no materials (120 g for a Sloop, 360 g for an Airship). Cargo is lost,
  once there is cargo.

## 7. Persistence (built)

Ships live in the characters database's `aw_ship`, which the server creates idempotently at
startup (`FusionShips.cpp`):

`id, owner, class, name, state (building / afloat / wrecked), map, x, y, z, o, hull, anchored,
ready_at, upkeep_until, created, bell_at`

- **Logout safety (built):** a ship's guid is minted again at each spawn, so a rider who logs out
  aboard is saved with their deck offset in `aw_ship_rider`. At login:
  - if the ship is still afloat on that map, they're put back on its deck at their place on it,
    wherever it has sailed since;
  - if it's gone (wrecked, summoned away), they come ashore at the dock it last left, on the
    planks: the Shipwright it launched from, or where a GM stood (on land) when they gave it;
  - if there's no such dock on that map, they wake at their hearth.
  They're never put in the sea or in the sky.
- A ship stays where it was left. It's saved when it anchors, when its pilot leaves the helm, every
  30 seconds while it moves, on the hourly upkeep sweep, and at shutdown. At startup every afloat
  ship is spawned back where it was, anchored.
- **Summon to a dock:** at any Shipwright, for the class's fee, a ship with no one aboard is moved
  to that Shipwright's dock. This is the answer to "I left it on the other continent".
- **One owner, with permissions:** see the crew's rights in section 3. Anyone can ride along. A
  "no boarding" lock comes next.
- Ships stay on their own continent: there's no crossing between maps.

## 8. Anticheat (built)

- **Server-authoritative movement:** the client sends only held-key bits (6 bits) for its own ship,
  and only while the server says it's at that ship's helm. The server integrates speed, heading
  and altitude from the class's limits, clamps them to water, ground clearance, ward ceilings and
  the map edge, and streams the pose. A client can't move a ship any other way.
- **Passenger offsets** are vmangos's own: deck-local offsets beyond 250 yd (or 100 yd vertically)
  are refused by `MovementHandler`.
- **Keels float on water and the ocean only**, never magma or slime (`SailableLiquid`).
- The pilot is rooted while at the helm, so the helm can't be used to carry a flying character.
- The channel is rate-limited like every fusion channel (`Throttle.h`). Key bits are taken at most
  20 times a second.

## 9. Feasibility: how it's built on vmangos and benilla

**Server.** vmangos has two transport kinds: `ShipTransport` (type 15, `MO_TRANSPORT`, a fixed
taxi-path cycle) and `ElevatorTransport` (type 11, `TRANSPORT`, a `TransportAnimation.dbc` loop).
Both derive from `GenericTransport`, which keeps passengers and carries them in
`UpdatePosition(x, y, z, o)`. A player ship is a **`PlayerShip : ElevatorTransport`** with no
animation and its own `Update`. Each map tick, it steps the fusion rules' kinematics and calls
`UpdatePosition`. It lives in the map's transport set (`Map::Add<GenericTransport>`), so:

- the map sends it to every player on the map at login and on map entry (`SendInitTransports`),
  as it does elevators;
- `Map::GetTransport(guid)` finds it (guid with an entry), so a client reporting
  `MOVEFLAG_ONTRANSPORT` with its guid boards it with no change to vmangos's movement code;
- a guid is minted per spawn from the map (`GenerateLocalLowGuid`), and the persistent identity is
  the `aw_ship.id`.

**The pose stream.** A 1.12 client computes a transport's position itself, from the timetable or
the keyframes, and nothing streams it. A player ship has no path, so the server streams the pose
on fusion channel 14: position, heading, velocity and turn rate, 5 times a second while it moves,
and every 2 seconds at rest, to each player on the map.

**Client (benilla).** benilla parks a type-11 object with no `TransportAnimation` path at its spawn
pose. The hook adds a third drive, **`Drive::Streamed`**: the layer writes the latest server pose
into `benilla_app::fusion::StreamedDecks`, and `transport.rs` arms any streamed guid as a
`Transport`, dead-reckoning it with the velocity and easing out the error each frame. Everything
benilla already does for decks then just works, because riding keys on `Transport`, never on its
drive: the deck collider, `ride::carry` for our own body, `compose_riders` for other passengers,
and the `ON_TRANSPORT` movement tail.

**Client (our layer).** `client/src/ships.rs` decodes channel 14, keeps the ships, fills
`StreamedDecks`, draws the Ship frame and helm HUD, and sends the held keys while at the helm.

## 10. Built so far, and next

Built:

- Sloop and Airship classes, with their numbers in `Fusion::Ships`.
- The Shipwright: deeds, commission, build timer, launch, upkeep, repair, salvage and summon.
- The helm: telegraph, rudder and altitude, anchoring, and server kinematics with water, ground,
  wards and map edges.
- Passengers on a moving deck, through vmangos's own transport passengers.
- `aw_ship` persistence, with ships respawned at startup.
- The Ship's Bell.
- Second slice: crew rights, deck guns, hull damage from weapons and ship-to-ship fire, the cargo
  hold, respawn aboard, logout safety, the player's own helm bindings, and keels that float on
  water only.
- GM tools: `.ship give | list | hull | upkeep | finish | summon | bell | delete`.

Next, in order: carrying mounts and companions; loot from a wreck; a no-boarding lock;
group-funded commissions; a shipyard structure on plots; a heavy third class on Wrath models;
crossing between continents.
