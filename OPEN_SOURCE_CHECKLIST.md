# Publishing checklist

> Outdated in part: game data is now published (small files in the tree, large archives as
> downloads from womw.net, `tools/release-assets.json`); only secrets are withheld.

For the maintainer: how this repository becomes a public one. The public repository starts with
a fresh history (this one's history is never published), made from a filtered export.

## Before the export

- [ ] Every secret and deployment detail lives outside the tree: `vps/deploy.env.local`
      (gitignored; `vps/deploy.env.example` is the template), ssh keys in your user folder, the
      server's own settings in `/etc/aw/` on the server.
- [ ] `tools/public-exclude.txt` (private) lists everything the public tree must not carry, with
      the reason: data made from game installs and private documents. Review it after adding
      files under `client/assets/`, `server/contrib/fusion/`, `design/` or `docs/`.
- [ ] `tools/private-markers.txt` (private) lists literal strings that must never appear in the
      export: your server's address, user names, provider, local drives.
- [ ] The data made from game installs is kept safe in `private-assets/` (gitignored,
      `private-assets.md`), so builds made from the public tree still work for you.
- [ ] Screenshots for the README (`docs/screenshots/`, then replace the placeholder).
- [ ] A security contact: enable private vulnerability reporting on the new GitHub repository
      (Settings > Code security), as `SECURITY.md` says.
- [ ] Decide GPL-3.0-only (as written now) or GPL-3.0-or-later for our own code, and set the
      `license` fields in `client/Cargo.toml` and `launcher/Cargo.toml` to match.
- [ ] The devlog's first post (`docs/devlog/2026-10-19-the-story-so-far.md`): read it, set
      `draft: false`, run `python tools/build-devlog.py`, and uncomment the devlog links in
      `docs/index.html`.

## The export

```powershell
powershell -ExecutionPolicy Bypass -File tools\make-public-tree.ps1 -Out <empty folder> -Strict
```

It exports the committed tree (`HEAD`), removes what `tools/public-exclude.txt` lists, and scans
the result for keys, tokens, passwords, addresses, e-mail addresses, local paths, game-asset
files and your private markers. Review every line of its report. Expected and fine: upstream
authors' e-mail addresses in vendored code, documentation addresses (`203.0.113.x`), version
numbers that look like addresses, and the project's own public links (womw.net, the Discord).

## Publishing

1. In the export folder: `git init -b main`, set your public name and e-mail for the commit
   (`git config user.name` / `user.email`: the e-mail is public from now on), `git add -A`,
   `git commit -m "World of Modern Warfare"`.
2. Make the new, empty public repository on GitHub (no README or license from GitHub), then
   `git remote add origin <url>` and `git push -u origin main`.
3. On GitHub: description and topics, private vulnerability reporting, issues on, the
   discussion or Discord link, and branch protection on `main` if you want reviews.
4. Check the repository page: README renders, `LICENSE` is detected, no excluded path is there.
5. Website: the public site can keep being served from the private repository's `docs/`, or
   move to the public one (Settings > Pages > `main` / `docs`, then move `docs/CNAME`'s domain
   there; only one repository can hold the custom domain).
6. Link the code from the site and the README of the game's Discord.

## After publishing

- Keep working in the private repository; publish updates the same way (export, then commit the
  export over a clone of the public repository), or move day-to-day work to the public one and
  keep only `private-assets/` and `vps/deploy.env.local` private.
- Never force-push excluded files to the public repository; if one slips through, remove it and
  rotate anything secret it held (a pushed secret counts as leaked).
