# Security policy

## Reporting a vulnerability

Please **do not** open a public issue for a security problem. Report it privately instead:

- **GitHub:** use "Report a vulnerability" on this repository's **Security** tab (private
  vulnerability reporting), or
- **Discord:** send a direct message to a maintainer on the project's Discord and ask for a
  private channel; don't post details in public channels.

Include what is affected (the gate, the launcher, the game server, the client), how to reproduce
it, and what an attacker could do with it. We aim to answer within a week and to fix serious
issues before anything about them is made public. We'll credit you in the fix unless you'd rather
we didn't.

## Scope

In scope: the code in this repository, for example the gate (`vps/gate.py`: sign-up, the download
check, crash reports, attestations), the launcher (`launcher/`: downloads, the kept password,
crash reports), the admin scripts (`vps/aw-admin.sh`), and the game server's handling of the
fusion packets (`server/src/game/Fusion`, `server/src/fusion`).

Out of scope: someone else's server running this code (report it to its operator), and issues
in upstream projects (vmangos, benilla, IW4L) that our changes don't touch; please report those
upstream.

## If you run a server

Keep your `vps/deploy.env.local`, ssh keys and `/etc/aw/` private, turn ssh password logins off,
and keep the gate's database user to the grants `vps/setup-server.sh` gives it. `vps/ADMIN.md`
has the details.
