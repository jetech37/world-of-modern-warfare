# Overnight 2026-10-12: discovery, fixes, polish

No VPS shipping. Everything is deployed locally, committed and pushed to `jchoxha/womw`.

## Workstreams
- **First-person body:** a real fix (it drew enormous around the camera; off by default since 17:58).
- **Melee animations:** review and polish every swing, block and draw.
- **Pets:** cleanup and audit of the companion system end to end.
- **Class options:** move partial options toward fully working.
- **Live discovery (main session):** class-by-class and system-by-system play sweeps.

## Found
(newest last; each line: what, where, status)
- GM mode on at login makes creatures ignore GMs: live combat tests need `.gm off`. (testing note)
- Player health is 100-300 by design (MW2 scale), not a bug.
- Hurt vignette (hud.rs) drew a hard red box, and the downed rings banded: both now one smooth gradient. FIXED
- Gunners (Orgrimmar Grunt) rush to arm's length instead of standing off at hold range, and clip into the camera. Sent to the gunfight agent.
- Gunners stood off at hold range in an open field after the agent's fix (Goldshire's wall had made them close for sight). FIXED, live-checked
- Own blood spurt filled the first-person view when hit (red blobs over the screen): skipped in first person via fusion::LayerFirstPerson. FIXED
- Test death loop: a downed then dead character makes the screenshot tool refuse; live tests start with `.revive`. (testing note)
- All windows (Kits, Companions, Construction, Journal) render well; Journal values back. Companions index list overflows its inset at the bottom: sent to the companion agent.
- Mage: Fireball readies into the right hand (log); the cast at a target is inconclusive in the probe. Spellcasting is due for the two-hand rework.

- Every login printed "You have unlearned Heroic Strike / Battle Stance" (warrior) and "Frost Armor" (mage): vmangos re-taught default and skill-auto-learned class spells that the kit system then took away. Now skipped (FusionClasses::SkipsDefaultSpell in LearnDefaultSpells and the learnOnGetSkill path). FIXED, live-checked
- Companion cleanup merged: same-map teleports and deaths no longer lose the companion, it persists across logout (live: the tiger came back at login), ranged companions fire from range, catalogue audit. DONE
- My mistake: re-applying 02_companions.sql alone wiped 08's species (97 -> 33) and broke names; re-applied 08/13/15/20; a header note in 02 now warns.
- Class options: 71 of 81 partial options now work (179/189); tier 6 works for every class but druid. Merged; SQL 21 applied. Live checks pending.
- First-person body rework: placement is proven right (0.00 yd off) and the view is clean, but nothing draws looking down. Sent back.
- Testing note: WOW_PROBE_PITCH=-70@40:200 looks down reliably; mouse: probe needs window focus.
- Kit swaps flooded chat with learned/unlearned/rank lines: the layer now quiets stock spell/ability announcements (recipes still announce). FIXED, live-checked
- Melee polish merged: swings 0.40-0.58 s (2H 0.8-1.0), no pops, weapon stays in the fist, fists alternate, dual wield mirrored, third-person ready stance and real cast throw. Live eyeball pending (swings fire on click, not hold).
- Companions follow behind and farther back by size (a mount-sized one stood in the view). FIXED
- Druid: all 8 forms present; cat form shows no gun or hands (live). Paws on swipe: inconclusive in probe.
- Login MOTD still says "Welcome to World of Warcraft! / Patch 1.12...": server config `Motd`; set live with `.server set motd <text>` (left for the user).
- Two-hand spellcasting agent started (charge/hold/release, no target, dual wield, AoE Moonfire).
- Two-hand spellcasting merged: ready by mouse button, charge (0.3-0.9 s), hold, release at the crosshair, instant and castable while moving, early release fizzles, dual-cast +50%, ground AoE Moonfire / Frost Nova / Consecration / Curse of Exhaustion. Live: ready -> charge -> release -> server reply works. Bug: the crosshair ground point is always None, so ground spells can't land: sent back.
- Testing notes: live runs need -Secs about 76+ now (login takes ~16 s before the probe clock starts); `.gm off` persists between runs, and summoned mobs too: use `.gm on` for non-combat tests.
## Fixed

## Left for the morning
