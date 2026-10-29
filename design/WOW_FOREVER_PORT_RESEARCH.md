# WoW Forever port research

Date: 2026-10-26. Status: research only, nothing built.

## 1. What "WoW Forever" is (confirmed)

- **World of Warcraft: Forever** is an **official Blizzard product**, announced at BlizzCon 2026. The community calls it "Classic+" and Blizzard's internal codename is "Camelot". It is a third permanent branch that runs next to retail and Classic. It is not a private server or a fan client.
- **Launch:** 4 November 2026 on Windows and macOS. It is included in a normal WoW subscription. The new Skyborne race needs an optional paid pack.
- **Content:** It is set in the first year of the 2004 game and keeps the level cap at 60. It adds new zones (Mount Hyjal, Riverglades, Shen'dralas, Zephras Isle), dungeons, raids, Camping, account-wide Collections and more than 1,000 quests.
- **Realms:** There are no named realms. Each region has one "mega-realm" per ruleset (Normal, PvP, RP, RP-PvP, with Hardcore coming later).
- **Client version:** Patch **1.60.1**. Beta builds 69977 and 70291 were reported, and beta change notes are dated 2026-10-08. Addons target this version, which is why CurseForge lists "WoW Forever 1.60.1" as a game version.
- **Addon API:** It uses the modern Classic-family Lua API, not the 1.12 API. The beta notes mention restricted addon messages for trial accounts and Lua `require` being renamed to `import`. Addons are packaged separately from Classic Era addons.

Sources: [warcraft.wiki.gg Patch 1.60.1](https://warcraft.wiki.gg/wiki/Patch_1.60.1), [1.60.1 API changes](https://warcraft.wiki.gg/wiki/Patch_1.60.1/API_changes/notes), [Icy Veins beta build](https://www.icy-veins.com/wow-forever/news/a-new-beta-client-build-arrives-for-wow-forever/), [Warcraft Tavern announcement](https://www.warcrafttavern.com/forever/news/warcraft-forever-classic-announced-at-blizzcon-2026/), [TechSpot](https://www.techspot.com/news/113832-world-warcraft-forever-launches-november-4-new-race.html), [Windows Central](https://www.windowscentral.com/gaming/blizzard/world-of-warcraft-forever-is-blizzards-take-on-classic-revamping-vanilla-azeroth), [expcarry addon guide](https://expcarry.com/wow-forever-best-addons).

### Technical profile (inferred, not confirmed by a source)

These points are inferred from the 1.60.1 version number and from how the existing modern Classic clients (1.13 to 1.15) work. I did not find a public technical breakdown.

- **Engine:** The current retail/Classic engine, not the 2006 1.12 engine. Expect DX12/Metal, the modern shader pipeline and a modern UI framework.
- **Data:** It almost certainly uses CASC instead of MPQ, and DB2 (WDC4/WDC5) instead of DBC. Models would be chunked M2 (MD21) with separate `.skin`/`.anim`/`.skel` files, and WMO and ADT would be the split-file layouts (`_obj0`, `_tex0`, ...). Files are addressed by FileDataID, not by path.
- **Protocol:** The modern Battle.net (BNet RPC/TLS) login plus the modern encrypted world protocol, which is nothing like vmangos's 1.12 protocol. The client only connects to Blizzard's servers. Community emulators such as TrinityCore and AzerothCore track only certain retail builds, and none target Forever.
- **Addons:** They run in Blizzard's sandbox. There is no file I/O, no network access except addon messages and chat, many actions are protected or secure (no automated combat), and addons cannot add new rendering such as custom meshes, cameras or shaders.

## 2. Legal and ToS constraints

- Blizzard's EULA forbids emulators and private servers ("unauthorized server emulators"), reverse engineering, and modifying or redistributing the client or its data.
- Pointing a Forever client at our own server would mean patching or redirecting the client and running an emulator. That is a direct ToS violation and carries copyright and DMCA risk, which is higher than for 1.12 because this is a current, actively monetized product.
- Shipping or converting Blizzard assets from a Forever install is redistribution. The same is true today for 1.12 assets, which is why the design doc already has an own-IP goal.
- Publishing addons for official Forever realms is allowed, within the addon policy (free, no obfuscation, no paid features).

## 3. Porting options

### (a) Our server mod, played with a Forever client
- **Feasibility:** Very low. It needs a new emulator that speaks the 1.60.1 protocol. That means a new login/auth stack, every opcode, and DB2-hash and FileDataID-aware data, followed by a full port of vmangos gameplay. It means chasing every Blizzard patch. It also needs a patched client, which breaks the ToS.
- **What we would lose:** Everything we render ourselves. That includes first-person hands and gunplay, the fusion channel UI, construction previews, custom weapon models and VFX, and the custom camera. Addons could cover HUD and menu pieces only (ammo counters, companion/profession/construction panels driven by addon messages). They cannot cover gun rendering, aim-down-sights or recoil cameras.
- **Effort:** Years. The legal risk is high. **Not recommended.**

### (b) Our client layer on Blizzard's engine
- Impossible without injecting into a closed client, which is cheating and gets accounts banned. **Rejected.**

### (c) Teach benilla to read Forever data files
- **Feasibility:** Technically possible, because open-source CASC, DB2 and modern M2/WMO readers exist (CascLib, wow.export, WoWDBDefs), and Bevy can render the results.
- **Effort:** Large, about 3 to 6 months:
  - a CASC reader with the listfile and FileDataID mapping
  - DB2 WDC4/5 parsing with matching schemas
  - MD21 M2 with external skin/anim files, the new WMO and ADT split layouts, and BLP texture-format support
  - mapping Forever DB2 rows to the 1.12 ids vmangos expects
- Even then, the new zones, quests and races exist only on Blizzard's servers, not in vmangos, so we would just get prettier versions of old assets.
- **Risks:** The encryption keys for some data may be withheld. Blizzard can change formats any patch. Reading local files from a legally owned install is a grey area, but redistributing anything stays prohibited.
- **Verdict:** A possible later "asset-source" option. It is not a port of our gameplay.

### (d) Other options
1. **Forever companion addon:** a small, legitimate addon on official realms. Examples are a WOMW-branded UI theme, or a "Wanderers"-style flavour addon. It would be marketing, not a port. Effort is weeks.
2. **Carry on with the own-IP path:** finish replacing Blizzard assets in our client and server. That removes the dependence on any Blizzard client, which is our biggest legal exposure today.
3. **Watch the market:** if Blizzard ever opens custom or community rulesets for Forever, revisit option (a). Nothing like that has been announced.

## 4. Recommendation

Don't try to run WOMW gameplay on WoW Forever. It is Blizzard's official, closed, server-locked client, so options (a) and (b) are either ToS violations or impossible, and they would also strip out our signature first-person gunplay.

Keep benilla plus vmangos as the platform and push the own-IP asset work. If we want a presence among Forever players, build a small, rule-abiding Forever addon as outreach. Consider (c), reading Forever-format data in benilla, only after own-IP is settled, and only for files from the player's own install, never redistributed.

## 5. Open questions to verify after launch (4 Nov 2026)

- Confirm the CASC/DB2/M2 versions by inspecting the live install with wow.export or CascLib.
- Read the final Forever addon API and its restrictions on warcraft.wiki.gg.
- Check whether any official custom or community realm features are announced.
