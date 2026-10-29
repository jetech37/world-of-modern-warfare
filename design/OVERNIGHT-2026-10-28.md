# Overnight 2026-10-27 → 28: work ledger

The owner's brief: "make sure all our systems work well together"; "key word is POLISH.
EVERYTHING! TEST AND RETEST AND POLISH". Local only: no VPS deploy, no push.

## In flight
- `gun-anims`: a reliable capture tool, third-person gun holds for every race, grenade pin and
  throw animations, blended relaxed hands, the first-person mounted view.
- `city-ceilings`: no-fly ceilings over cities, invisible ceilings over unfinished areas, holes
  in the ground seen from the air.
- `quest-audit`: which community quests can be finished, data fixes, hidden quests, EventAI for
  the first bosses.

## Queued (owner requests, 2026-10-27)
1. **Systems integration pass:** every system tested together (action systems, hands, kits,
   companions, skyriding, professions, construction, achievements, Wanderers, bounties), with
   conflicts fixed.
2. **Class abilities:** each class and spec has a distinct, effective playstyle, so guns are one
   option and not the only way to play well (see `design/BALANCE_GUNS_VS_CLASSES.md`,
   `CLASS_CATALOG.md`).
3. **Profession windows:** Construction, Minstrelsy, Inscription and Jewelcrafting use the stock
   TradeSkill window look.
4. **Companion duels:** a Slay-the-Spire-style deck battler instead of the Pokémon-style battles.
5. **Airships and sailing ships:** player-owned and operated, prohibitively expensive, powerful,
   useful and persistent.

## Done tonight
- quest-audit: 1,671 community quests offered (561 fixed, 700 hidden), 42 bosses with EventAI
  (`81_community_fixes.sql`, `design/COMMUNITY_QUEST_AUDIT.md`).
- city-ceilings: city wards and fixed ceilings (`fusion/Airspace.*`), measured centres and pads;
  the hole pass closes nothing on stock tiles; the community pass is opt-in (`--close-city-holes`).
- class-identity: all 27 archetypes win a 25 yd gunless duel (`ClassTuning.*`, `Balance::Duel`).
- prof-windows: custom professions drawn by the stock TradeSkillFrame, with a craft cast bar.
- gun-anims (part): capture tool fixes, grenade pin-pull and throw, blended Utility hands, the
  third-person grip turned 180 degrees.
- Local package updated: client 820/821 (known companion_ui), fusion 939/940 (known furniture).
