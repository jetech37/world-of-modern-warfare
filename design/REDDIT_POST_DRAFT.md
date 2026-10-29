# Reddit post: draft and capture plan (private working doc)

## Where to post
- **Main:** r/wowservers allows private-server posts. Read its current rules first: flair, the
  self-promotion ratio, and any required format such as rates or a server-info block.
- **Also:** r/wowmodding for the tooling and mods angle, r/rust (a "this is my game" style
  post) for the Bevy client, and r/gamedev Screenshot Saturday.
- **Avoid:** r/wow and r/classicwow; both remove private-server posts.

## Why the last post was probably auto-removed (check before reposting)
- Reddit's site-wide spam filter acts on new or low-karma accounts, link-only posts, URL
  shorteners and domains it doesn't know. A new domain like womw.net can be filtered silently.
- The subreddit's AutoModerator may require flair, a minimum account age or karma, or a text post.
- Links to client downloads or copyrighted files get pulled fast.
- **What to do this time:** make a text or image post with links inside the body, not a link
  post. Set the flair. Don't link client downloads; link the GitHub repo and the site. Message the
  mods first, or modmail them right after posting with "my post was filtered, could you review?".

## Title options
1. World of Modern Warfare: an open-source WoW 1.12 + MW2 mash-up. Guns, skyriding, ships,
   companion card duels. Playtesters wanted
2. I've been building a "multi-game mod" on vanilla WoW: MW2 gunplay, two-hand spellcasting,
   skyriding, airships. It's all open source now
3. [Open Source] WoW 1.12 with Modern Warfare 2 gunplay, plus a dozen standalone mods you can lift
   for your own server

## Body draft

> **World of Modern Warfare** is a non-commercial fan project: vanilla WoW 1.12, rebuilt around
> Modern Warfare 2 gunplay, on a custom open-source client and a vmangos-based server. It's now
> **fully open source** and still **very much in development**. I'm looking for **playtesters**,
> and the test server is open to anyone.
>
> **What's in it**
> - **MW2 gunplay**: 57 MW2 guns, aim-down-sights, grenades, killstreak-style abilities, hit
>   validation on the server. Guns are one way to play, not the only one.
> - **Class rework**: every class has distinct archetypes that can win without a gun (mobility,
>   control, healing and protection that "answer" guns).
> - **Two-hand action systems**: each hand holds its own weapon, spell or tool. Charge spells per
>   hand, dual-cast, swap between Modern / Classic / Utility / Companion bars.
> - **Skyriding**: Dragonflight-style momentum flying with Launch and Surge on flying mounts.
> - **Companions**: tame, level and evolve companions, ride them, and fight other tamers in a
>   Slay-the-Spire-style **card duel** system.
> - **Player ships and airships**: expensive, crewed, persistent, with deck guns.
> - **New professions**: Construction (build on plots), Minstrelsy (rhythm-game music buffs),
>   Inscription and Jewelcrafting in the classic profession window.
> - **Wanderers**: named NPC adventurers who live in the world.
> - **New and returning places**: Wrath's Dalaran placed in Alterac, plus merged community
>   zones and islands, and an optional HD texture pack.
>
> **Take what you want.** Every system is written to be separable. `MODS.md` in the repo lists
> each mod, where it lives and what it depends on, if you want to lift one into your own project:
> - the client improvements;
> - the community-content merge tool;
> - the CASC reader;
> - the world-map fixes;
> - the city no-fly ceilings;
> - the capture tool.
>
> **Links**
> - Source: github.com/<repo>
> - Site, devlog and the extra downloads: womw.net
> - Server: play.womw.net. You need your own WoW 1.12 client.
>
> Feedback, bug reports and contributors are very welcome. It's a hobby project, so expect rough
> edges. That's what the playtesting is for.
>
> *Not affiliated with Blizzard, Activision or any other rights holder. Non-commercial.*

## Image plan (about 8–10 images; a gallery post works better than one image)
Take them at 2560x1600 with the UI on, unless noted. Use golden-hour or noon light, and add
a short caption to each gallery image.
1. **Hero:** a third-person character aiming an M4 over Goldshire or Stormwind at sunset.
   Clean UI showing the action bars.
2. **First person:** ADS down a rifle in a dungeon or Duskwood, muzzle flash mid-shot.
3. **Two-hand casting:** first person, a charged fireball in one hand and a frost spell in the
   other, both cast bars visible.
4. **Skyriding:** a gryphon mid-bank over Elwynn or Lordamere with wind streaks and wingtip
   trails; a second shot of the Launch burst from below.
5. **Ships:** an airship over Stormwind harbor or a sloop at sea, with the player at the helm or
   a deck gun.
6. **Card duel:** the duel window mid-fight, with a hand of cards and two companions facing off.
7. **Companions:** a group shot of several evolved companions, then mounted on one.
8. **Dalaran in Alterac:** a wide shot over Lordamere Lake showing it level with the land.
9. **Profession window:** Construction or Minstrelsy in the classic window, or a built player
   house.
10. **Class without guns:** a caster or melee class fight with no gun, to show guns aren't
    required.
- **Optional:** a 20–40 s clip (skyriding into a gun fight) does very well. Upload it natively
  to Reddit, not as a link.

The capture tool (client/docs/CAPTURE.md) can stage 1, 2, 3, 4, 7, 8 and 9 offline. Live play
is better for 5, 6 and 10.

## Before posting (checklist)
- [ ] The public repo is pushed (owner go), and the README build steps were tested by a fresh
      clone.
- [ ] womw.net/downloads hosts the extra files, and the fetch script works against it.
- [ ] The server is open to anyone: registration works, the attestation checkbox is present,
      the server has capacity, and the crash fix is deployed (owner go for the VPS).
- [ ] A Discord or feedback channel link is ready for testers.
