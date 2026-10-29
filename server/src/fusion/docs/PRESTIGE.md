# Prestige, field gear and MW2 matches: integration notes

The rules are in `include/fusion/Prestige.h`, `FieldGear.h` and `Match.h` (tests in
`tests/PrestigeTests.cpp`, `FieldGearTests.cpp`, `MatchTests.cpp`). The game side is
`game/Fusion/FusionPrestige.cpp`; the client's is `client/src/prestige.rs`. World data:
`sql/custom/azeroth_warfare/11_prestige_professions.sql`. Everything travels on fusion channel 8.

## Prestige: three tracks, chosen by the player

| Track | Ready when | Resets | Keeps | Stored |
|---|---|---|---|---|
| Level | level 60 | level to 1, experience to 0; gear above level 1 goes to the bags (mail if full); talents follow the level | items, gold, reputation, professions, mounts, quests done, kits, spells | `character_fusion_prestige.level_rank` |
| Guns | every Weapons and Attachments achievement earned | every earned achievement but the Feats, and every counter (they would earn it all back at once) | Feats and their titles; prestige camos | `account_fusion_prestige.gun_rank` (unlocks are the account's) |
| Profession | a profession at 300 | its skill to 1 | its recipes and its trained rank (the max) | `character_fusion_prestige_skill` |

Why the level track keeps quests done: doing them again would pay their gold and items twice. Instead a
prestiged character gets more experience from kills below 60 (`KillXpMultiplier`: +50% a rank, at most
3x; `Player::GiveXP` asks `FusionMgr::PrestigeKillXp`).

Why the profession track keeps recipes: crafting already needs the recipe's skill, so a reset skill
locks them again until it climbs back. Taking recipes away would only punish rare drops and purchases.

Ranks run 1 to 10 a track. Each pays tokens (level 5, guns 5, a profession 3), a title, and at some
ranks a cosmetic (`RankReward`): Red tracers at level 2, Violet at 5, Gold at 10; Red Tiger, Blue
Tiger and Fall camos on every gun at gun ranks 1, 3 and 6, Green tracers at 4, Blue at 8. Prestige camos
are granted on top of the account's achievement unlocks (`GrantPrestigeUnlocks`, through
`FusionMgr::UnlocksOf`), so they survive a gun prestige and show in the kit editor as soon as the gun
is earned again.

**Milestones** pay much more, for having all three maxed together (60, every gun and attachment, a
profession at 300) with every track prestiged at least N times: N = 0 "the Triple Threat", 1 "the
Unbroken", 3 "the Paragon", 5 "Legend of Azeroth", 10 "the Eternal". Each pays a title, gold
(100g to 2500g), tokens, a W5 companion item (90001, 90022, 90024, 90062, 90060) and a cosmetic. The
server looks every 5 seconds and pays each milestone once.

**Titles**: vanilla has none, so the fusion keeps them. The one worn (picked in the Prestige tab) goes
before what the player says in chat, `[Sergeant] hello` (`FusionMgr::TitleChat`). Level titles are
MW2's ranks (Private ... Commander), gun titles "the Rifleman" ... "the Arsenal", profession titles
"Seasoned / Expert / Master / Grand Master / Legendary <Engineer>", milestone titles their names.

**The two-step confirm**: the client asks (`Op::Ask`), the server checks, names what will be lost and
sends a nonce (`Op::Prompt`); only a `Op::Confirm` with that nonce within 30 s does it, after checking
again. Prestige is refused in combat, dead, in a battleground or in the arena match.

**The token shop** is on the Prestige tab (`Prestige::Shop`, the Buy op): stims, spare grenades, the
prestige bandolier and ghillie, a companion carrier, tracer colours and camos on every gun. (The
Prestige Quartermaster NPC that once sold them is gone.)

### Wire (channel 8, little-endian, first byte the version 1, second the op)

Client: 1 Request · 2 Ask (track u8, key u16) · 3 Confirm (nonce u32) · 4 SetTitle (u16) ·
5 SetTracer (u8) · 6 Cancel · 7 Buy (good u8) · 21 Scoreboard (send it).

Server: 1 State (`EncodeState`) · 2 Prompt (nonce u32, track u8, key u16, text str16) · 3 Result (ok
u8, text str16) · 4 Equipment (slot u8 0 lethal / 1 tactical, count u8) · 5 Stim (kind u8, ms u32) ·
6 Look (tracer u8, r g b) · 7 ShopList (n u8, n x (good u8, price u32, owned u8, name str8)) · 20 Hud ·
21 Scoreboard · 22 MatchEnd (winner u8, text str16).

## Professions, first pass (field gear)

Professions never make guns stronger. Vanilla recipes are client spells we cannot add, so field
recipes live in the world table `fusion_trade_recipe` and are taught and made by the fusion: a
profession trainer of Engineering, Leatherworking, Tailoring or Alchemy shows a "Field recipes"
gossip option. Learn a recipe there (its skill and fee), then make it at any trainer of that
profession; colours and skill-ups are vanilla's (Construction's rules).

| Profession | Items | Effect |
|---|---|---|
| Engineering | Spare Thorium Grenade, Sticky Bomb, Claymore, Seaforium Charge, Throwing Knives, Arcane Flash Bomb, Concussion Charge, Smoke Canister (93200-93207) | Use: one more charge of the kit's own lethal or tactical for this life, at most two over the kit's own (`AddCharge`). Only the kit's own kind fits. |
| Leatherworking | Light Bandolier, Bandolier, Heavy Bandolier (+ Prestige) (93220-93223) | An ammo pouch worn in a bag slot: +25/50/75/100% spare ammunition (the reserve) for every gun, never a bigger magazine (`GunAmmo::SetReserveBonus`). |
| Tailoring | Woven / - / Master Ghillie Mantle (+ Prestige) (93240-93243) | A cloak: creatures notice you at 85/75/65/60% of the radius (`AggroRadiusMultiplier`), and enemy clients' UAV sweep skips you (crate::hud, by the visible cloak). |
| Alchemy | Focus, Second Wind, Adrenaline Stim (93260-93262) | Focus 60 s: 40% less sway, less recoil and flinch. Second Wind 12 s: regeneration after 1 s, 2.5x as fast. Adrenaline 20 s: sprint without tiring. |

Enchanting rounds are W7's, Construction W4's, the First Aid and Engineering revive items W2's.

## MW2 modes

- **Battlegrounds**: Warsong Gulch is Capture the Flag, Arathi Basin Domination, Alterac Valley Ground
  War. Vanilla's rules and win conditions stand; each `UpdatePlayerScore` also gives MW2's points
  (`Match::Points`: a kill 100, a flag capture 500, a return 100, a base/tower/graveyard taken 200,
  defended 100). Killstreaks already count player kills there.
- **Team Deathmatch** is the Gurubashi Arena (Stranglethorn, already a free-for-all PvP area, so no
  queue or instance): inside the floor (40 yards of the chest's spot), the Alliance and the Horde
  score a point for each enemy-faction kill; first to 30, or ahead after 10 minutes, wins; the clock
  starts at the first kill; 15 s of intermission, then a fresh match. A player who dies there comes
  back on the arena floor after 5 s, no corpse run (`FusionMgr::UpdateArena`). A cleaner BG-queue
  variant (Warsong Gulch without flags) would need a new BattleGroundTypeId the 1.12 client's
  battlemaster UI cannot show, so the arena it is.
- **HUD and scoreboard**: once a second in a match the server sends the HUD (mode, team scores, the
  limit and clock, the player's score, kills, deaths); with Tab held the client asks for the
  scoreboard every second (best first; objectives are captures, returns and defences).

## GM commands (`.fusion prestige`)

- `.fusion prestige`: the character's prestige.
- `.fusion prestige ready level | guns | <skill line>`: make a track ready (level 60; every Weapons
  and Attachments achievement earned; the profession, if known, at 300).
- `.fusion prestige tokens <n>`, `.fusion prestige clear`.
- `.fusion prestige tdm`, `.fusion prestige tdm end`: the arena match's state; start it afresh.
