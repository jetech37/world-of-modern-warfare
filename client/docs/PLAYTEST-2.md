# Play-test round 2: what changed (2026-10-05)

Your notes from the first round, and what was done about each. Everything is committed locally
and nothing has been pushed.

## Fixed
- **Guns not aiming down sights.** Every gun was being treated as an akimbo pair, because MW2
  ships left-hand animations for ordinary guns too, and akimbo guns don't aim. Only
  `*_akimbo_*` variants count as pairs now. Attachment variants (`fal_xmags_mp` and so on)
  also inherit their base gun's animations now. Before, they had no aim, reload or raise
  animations of their own.
- **Akimbo.** Each mouse button fires its own gun: left click the right-hand gun, right click
  the left. One gun alone fires at a single gun's rate.
- **Switching objects in build mode also switched your gun.** Build mode now reads the wheel
  and number keys before Create-a-Class can see them.
- **Blueprints failing with "you can't build that here".** The server refuses placements
  closer than 150 ms apart. Blueprints now wait for the server's answer to each piece before
  sending the next.
- **No way to use equipment.** The client's catalog was missing the grenade and killstreak
  entries; they're back. Your equipment and how much is left now shows by the ammo counter:
  `G  Frag x1    Q  Flash Grenade x2`.
- **Arm cut off at the elbow** when knifing or reloading. The upper arm is now drawn too.
- **Skatecraft** is quarantined until you have the Skate 3 files. It's kept on the fork's
  `skate` branch.

## New
- **Bullets, not tracers.** Every round and every shotgun pellet is a small, faint grey round
  flying from the gun's real muzzle to where it lands. NPC and sentry rounds fly the same way,
  each with a muzzle flash.
- **Rockets and grenade launchers.**
  - The round flies: rockets straight with a smoke trail and motor flame, grenades in an arc.
  - Every explosion (launchers, grenades, C4, claymores, Martyrdom, killstreaks) is a fireball
    of WoW's flame, with rolling smoke, a dust ring, sparks, a sound, and a camera shake when
    you're close.
  - These need the server half; see below.
- **Melee with your own weapon.** The knife key swings your equipped main-hand weapon: its WoW
  model is in your hand during the swing. Speed and damage follow its type: dagger fastest,
  then sword, axe, mace, and two-handers slowest and hardest. This needs the server half.
- **Heartbeat sensor on the gun.** The screen sits on the sensor attachment itself and sweeps
  each second: red blips for enemies ahead within 28 yd.
- **Build menu.** B opens a picker:
  - Categories: Wood, Stone, Defence, Camp, Objects, Blueprints. Each piece is a tile with its
    cost; click one to hold it.
  - **Tab** reopens the menu while building. **B** leaves build mode.
  - In the world: LMB places, R turns, MMB takes a piece down, F works doors.
- **WoW's own objects (the Objects tab).** 28 props from your install: tents (human, orc,
  goblin), barrels, crates, grain sacks, haystacks, a hay wagon, a mine cart, lamp posts, an
  anvil, a table, wood and stone fences, a spike wall, a banner, a bonfire, a rock wall, a
  gate, a water hut, a well and a lantern. Each is solid with its real size.
- **Real models.** The campfire is WoW's Elwynn campfire and the bedroll is the Westfall bed.
  The sentry gun is MW2's sentry turret.
- **Sandbags** are two staggered layers of burlap bags.

## The server half (in progress)
A background agent is changing the server:
- **Health model.** 100 base, 300 at level 60, and 600–1000 with tank gear. Creatures scale
  from 100 at level 1 to 250 at level 60, with elites ×3, dungeon bosses ×10 and raid bosses
  ×30.
- **Damage.** Fresh level 60s deal about 3×, geared ones 6–10×, and low-level enemies stay
  dangerous.
- **Armor** cuts up to 30% of incoming damage.
- **NPCs.**
  - Gunners miss more, close in while firing, and switch to a knife up close.
  - Melee mobs are much faster.
  - Casters cast 5× faster.
- **Sentries** fire only in their front ±80°.
- **Equipped weapon** speed and damage for melee.
- **Rockets** have real flight time and splash.
- **Pro perks.** A GM command, `.fusion pro on|off`, to test them.

Still to come:
- the server side of props;
- Bling Pro's second secondary attachment, which changes the class format on both sides;
- creatures pathing around your buildings;
- racial building styles;
- placeable WoW houses.

## Questions for you
- **Shotguns.** Ours use MW2's own numbers: SPAS-12 8 pellets, 40→20 damage between 11 and 19
  yards, a 5° cone at the hip and 5.5° aimed. What feels off: reach, spread, damage, or rate
  of fire?
