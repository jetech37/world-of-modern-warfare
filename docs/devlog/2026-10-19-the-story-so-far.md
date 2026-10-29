---
title: The story so far, and going open source
date: 2026-10-19
summary: What World of Modern Warfare is, everything built in its first weeks, the first playtest, and why the code is about to be public.
draft: true
---

World of Modern Warfare (WOMW) started with a simple question: what if the classic world of
Azeroth played like a modern first-person shooter? Not a minigame bolted onto the side, but the
whole thing: you walk the same zones, take the same quests and meet the same creatures, only now
you see it through your own eyes, aim with the mouse and fight with a modern arsenal.

This is the first post of the devlog. It covers what has been built so far, what we learned from
the first playtest, and the next big step: the project is going open source.

## How it works

WOMW has three parts that grew up together:

- **The server** is a fork of vmangos, the open-source 1.12 server. All of the shooter's rules
  live in a library of their own, with its own tests: guns, hit detection, kits, perks,
  killstreaks, companions, construction. The game server only wires them in.
- **The client** is built on benilla, a from-scratch 1.12 client written in Rust and Bevy. Our
  layer on top of it adds first person, the guns, the HUD and every new window.
- **The launcher** installs the game, signs you up, keeps you up to date and starts it.

The server referees everything. When you fire, your client sends the shot; the server checks it
against its own copy of the world, rewound to the moment you pulled the trigger, and decides
whether it hit, where, and for how much. That is what makes it a shared world rather than a
single-player mod.

## What's been built

**Guns first.** Hitscan and projectile weapons with damage by range and hit location,
aim down sights, recoil, sway and holding your breath, stances (crouch, prone, sprint, mantling
over ledges), optics and thermal scopes, attachments, camos, akimbo, underbarrel launchers,
tracers, a heartbeat sensor, a minimap radar, riot shields and thrown equipment. Every weapon,
attachment and perk has a lore-friendly name in the world.

**Kits, perks and killstreaks.** Each character keeps five kits (guns, attachments, perks and
equipment), edited at Quartermasters, class trainers and innkeepers. Perks and deathstreaks
change how you fight; killstreaks were reimagined for Azeroth: a goblin guided rocket, a dwarven
sentry turret, gnomish sky hornets, gryphon and wind rider gunships, siege bombers and a
gyrocopter gunship. Everything unlocks through in-game achievements that reward playing every
way.

**Classes that answer guns.** If everyone has a gun, what is a class for? Our answer: classes do
what guns can't. Each class has three core abilities and six picks, one per tier, from three
archetypes, so a rogue can be an assassin, a bard or anything in between. Spells are cast from
your hands: ready a spell, charge it, hold it, and release it at the crosshair, with both hands
for a stronger dual cast and ground-targeted area spells. Melee is in first person too, with
whatever weapon you hold, and druids get their forms with bars of their own.

**Companions.** Collect companions that fight beside you, level up, learn moves, evolve (some of
them into mounts), breed and trade. They can be downed and revived, just like you.

**Construction.** Build destructible structures anywhere in the open world, under rules that
keep towns and quest hubs clear, or rent a protected homestead plot in one of the unfinished
zones and build there in peace.

**A more dangerous world.** Creatures hear gunfire, call for help and chase harder; gunner camps
shoot back; enemy tracers and damage-direction arcs show where fire is coming from; grenades
bounce and can be cooked; the dead fall as ragdolls; and you loot every corpse around you in one
window. Being downed instead of dying means your friends can get you back up.

**The launcher and the site.** One small exe installs itself, makes your account, downloads the
game and fetches only what changed after that. If the game crashes, it offers to send the crash
report, so we can fix what broke. womw.net shows the server's status live. In the game,
**Esc > Report Bug / Suggest** sends a report straight to us, with where you were standing.

## The first playtest

The first playtests, with friends, came in the first week of October, and on 12 October the
server opened to playtesters through womw.net. Their notes shaped a lot of what came next: the
second round fixed aiming on attachment variants, akimbo firing, the arm being cut off at the
elbow, blueprints that failed to place, and equipment you couldn't use, and it added real
flying rounds, rockets and grenade launchers, melee with your own weapon and much more. The
in-game bug reports and crash reports exist because of that round. Thank you to everyone who
played, broke things and told us about it.

## Going open source

WOMW is about to become an open-source project. The code (the server's rules, the client layer,
the launcher, the hosting scripts, this website and the design documents) will be public under
the GNU GPL v3; the server keeps vmangos' GPL v2 and the client base keeps benilla's MIT/Apache
license.

The repository will hold code only. It contains no game files and nothing read from them: the
programs read your own installs. To play, you must own and have legal access to World of
Warcraft and the original Call of Duty: Modern Warfare 2 (2009), and when you sign up you'll be
asked to confirm exactly that.

Opening the code means you can see how it all works, report bugs with the source at hand, send
fixes and features, and even host a server of your own for your friends.

## What's next

Next up: the bounty board, more lore killstreaks, better first-person bodies, and a lot of
polish from your reports. Follow along here, on Discord and on r/womw.
