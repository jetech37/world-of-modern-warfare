"""Publish World of Modern Warfare for friends: the launcher and the game, to the VPS's download area.
The server comes from vps/deploy.env.local (AW_VPS, AW_VPS_KEY, AW_GAME_HOST; see
vps/deploy.env.example).

    python package/publish.py            # build the launcher, upload what changed
    python package/publish.py --dry-run  # say what would go up
    python package/publish.py --no-game  # the launcher and client only (a quick test)
    python package/publish.py --launcher-only  # a new launcher; the game as it is up there
    python package/publish.py --hd       # the optional HD pack (dist/AzerothWarfare/hd), alone
    python package/publish.py --note "Swords sit in the right hand" --note "..."
                                         # release notes for the site (newest first)

What friends get (paths under their install, as the launcher keeps them):
  WorldOfModernWarfare.exe   the launcher (the server built in; players sign up with an invite code)
  client/**            the game client, from dist/AzerothWarfare/client
  game/WoW/Data/**, game/MW2/{main,zone}/**, game/MW2/localization.txt
The manifest (every file, its size and sha256) goes up last, so a launcher never sees a
half-made version. The optional HD pack (hd/**) is never in it: --hd publishes it under
hd-manifest.json, which the launcher offers as "Download HD pack" with its size. Everything here goes up over ssh; players fetch it over HTTPS, where Caddy
asks the gate for a game account first, but for the launcher exe (vps/Caddyfile, vps/gate.py).
Hashes are cached in build/publish-cache.json by size and change time.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(REPO, "dist", "AzerothWarfare")
DEPLOY_CONFIG = os.path.join(REPO, "vps", "deploy.env.local")


def deploy_config(path: str = DEPLOY_CONFIG) -> dict[str, str]:
    """The server's settings from vps/deploy.env.local (see vps/deploy.env.example): NAME=value
    lines, # comments; an environment variable of the same name wins, a leading ~ is home."""
    cfg: dict[str, str] = {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                name, value = (s.strip() for s in line.split("=", 1))
                cfg[name] = value
    except OSError:
        pass
    for name in list(cfg) + [n for n in os.environ if n.startswith("AW_")]:
        if os.environ.get(name):
            cfg[name] = os.environ[name]
    return {k: os.path.expanduser(v) if v.startswith("~") else v for k, v in cfg.items()}


CONFIG = deploy_config()


def need(name: str) -> str:
    value = CONFIG.get(name, "")
    if not value:
        sys.exit(f"{name} is not set: copy vps/deploy.env.example to vps/deploy.env.local and fill it in")
    return value


VPS = CONFIG.get("AW_VPS", "")
KEY = CONFIG.get("AW_VPS_KEY", "")
FILES = "/srv/aw/files"
CACHE = os.path.join(REPO, "build", "publish-cache.json")
SSH = ["ssh"] + (["-i", KEY] if KEY else []) + ["-o", "BatchMode=yes", VPS]
LAUNCHER = os.path.join(REPO, "launcher", "target", "release", "WorldOfModernWarfare.exe")
RELEASES = "/srv/aw/releases.json"
NOTICE = "/srv/aw/notice.txt"
# The client's PDB per published build (package/build.ps1 stages it): crash reports name the
# client exe's sha256, and its folder here ends with the first 12 of it.
SYMBOLS = os.path.join(REPO, "build", "symbols")
CLIENT_EXE = "client/benilla-mw2.exe"


def ssh(command: str) -> str:
    need("AW_VPS")
    return subprocess.run(SSH + [command], capture_output=True, text=True, check=True).stdout


def build_launcher() -> None:
    env = dict(os.environ)
    gate = dict(line.split("=", 1) for line in ssh("cat /etc/aw/gate.env").splitlines() if "=" in line)
    # No secret is built in: players log in with their game account.
    env.update(AW_HOST=f"https://{gate['AW_HOST']}", AW_GAME_HOST=need("AW_GAME_HOST"))
    # Where an older client build kept players' settings (copied in once), if there was one.
    if CONFIG.get("AW_OLD_SETTINGS_DIR"):
        env["AW_OLD_SETTINGS_DIR"] = CONFIG["AW_OLD_SETTINGS_DIR"]
    subprocess.run(["cargo", "build", "--release"], cwd=os.path.join(REPO, "launcher"), env=env, check=True)
    # The site's download button points at the VPS's /files/WorldOfModernWarfare.exe (uploaded
    # below; public, unlike the game files): one copy, so docs/ keeps none.


def wanted(game: bool = True) -> dict[str, str]:
    """Each published path, from its file here."""
    out = {"WorldOfModernWarfare.exe": LAUNCHER}
    roots = [
        ("client", os.path.join(DIST, "client")),
        ("game/WoW/Data", os.path.join(DIST, "game", "WoW", "Data")),
        ("game/MW2/main", os.path.join(DIST, "game", "MW2", "main")),
        ("game/MW2/zone", os.path.join(DIST, "game", "MW2", "zone")),
    ]
    if not game:
        roots = roots[:1]
    for prefix, root in roots:
        for d, _, names in os.walk(root):
            for n in names:
                full = os.path.join(d, n)
                rel = os.path.relpath(full, root).replace("\\", "/")
                out[f"{prefix}/{rel}"] = full
    loc = os.path.join(DIST, "game", "MW2", "localization.txt")
    if game and os.path.exists(loc):
        out["game/MW2/localization.txt"] = loc
    return out


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def hashes(files: dict[str, str]) -> list[dict]:
    try:
        cache = json.load(open(CACHE))
    except (OSError, ValueError):
        cache = {}
    entries = []
    for path, full in sorted(files.items()):
        st = os.stat(full)
        key = f"{st.st_size}:{int(st.st_mtime)}"
        if cache.get(full, {}).get("key") != key:
            cache[full] = {"key": key, "sha256": sha256(full)}
        entries.append({"path": path, "size": st.st_size, "sha256": cache[full]["sha256"]})
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    json.dump(cache, open(CACHE, "w"))
    return entries


def upload(files: dict[str, str], paths: list[str]) -> None:
    """Stream the files as a tar into the download area."""
    p = subprocess.Popen(SSH + [f"tar -C {FILES} -xf - && chmod -R a+rX {FILES}"], stdin=subprocess.PIPE)
    with tarfile.open(fileobj=p.stdin, mode="w|") as tar:
        for i, path in enumerate(paths, 1):
            tar.add(files[path], arcname=path)
            if i % 200 == 0 or i == len(paths):
                print(f"  {i}/{len(paths)} files sent", flush=True)
    p.stdin.close()
    if p.wait() != 0:
        sys.exit("upload failed")


def archive_symbols(version: str, entries: list[dict]) -> None:
    """Keep the published client exe's PDB (and the exe) in build/symbols/<version>_<sha12>, once
    per exe: the PDB is never published, and without it a crash's frames are bare offsets."""
    exe = next((e for e in entries if e["path"] == CLIENT_EXE), None)
    if not exe:
        return
    sha12 = exe["sha256"][:12]
    os.makedirs(SYMBOLS, exist_ok=True)
    if any(name.endswith("_" + sha12) for name in os.listdir(SYMBOLS)):
        return
    staged = os.path.join(SYMBOLS, "staged")
    staged_exe = os.path.join(staged, "benilla-mw2.exe")
    pdb = os.path.join(staged, "benilla_mw2.pdb")
    if not (os.path.exists(staged_exe) and os.path.exists(pdb)) or sha256(staged_exe) != exe["sha256"]:
        print("warning: no PDB staged for this client exe (package/build.ps1 stages it); "
              "crash reports from it can't be symbolised")
        return
    dest = os.path.join(SYMBOLS, f"{version.replace(' ', '_').replace(':', '')}_{sha12}")
    shutil.copytree(staged, dest)
    print(f"Symbols kept in {dest}")


def notes() -> list[str]:
    args = sys.argv[1:]
    return [args[i + 1] for i, a in enumerate(args[:-1]) if a == "--note"]


def add_release(version: str, lines: list[str]) -> None:
    """Put this release's notes at the top of the site's list."""
    try:
        old = json.loads(ssh(f"cat {RELEASES} 2>/dev/null || echo '[]'"))
    except (subprocess.CalledProcessError, ValueError):
        old = []
    entry = {"version": version, "date": int(time.time()), "notes": lines}
    data = json.dumps([entry] + [r for r in old if r.get("version") != version], indent=1)
    subprocess.run(SSH + [f"cat > {RELEASES}.new && mv {RELEASES}.new {RELEASES} && chown aw:aw {RELEASES}"],
                   input=data, text=True, check=True)


def launcher_only() -> None:
    """Put up a new launcher and keep every other file the server has, as it has it: the game
    friends play doesn't change."""
    build_launcher()
    remote = json.loads(ssh(f"cat {FILES}/manifest.json"))
    launcher = hashes({"WorldOfModernWarfare.exe": LAUNCHER})[0]
    entries = [e for e in remote["files"] if not e["path"].endswith(".exe") or "/" in e["path"]]
    entries.append(launcher)
    upload({"WorldOfModernWarfare.exe": LAUNCHER}, ["WorldOfModernWarfare.exe"])
    version = time.strftime("%Y-%m-%d %H:%M")
    manifest = json.dumps({"version": version, "files": sorted(entries, key=lambda e: e["path"])})
    subprocess.run(SSH + [f"cat > {FILES}/manifest.json.new && mv {FILES}/manifest.json.new {FILES}/manifest.json && chmod a+r {FILES}/manifest.json && rm -f {NOTICE}"],
                   input=manifest, text=True, check=True)
    if notes():
        add_release(version, notes())
    print(f"Published version {version} (launcher only).")


def publish_hd() -> None:
    """The optional HD pack: hd/** and its own manifest, hd-manifest.json (uploaded last)."""
    root = os.path.join(DIST, "hd")
    if not os.path.exists(os.path.join(root, "womw-hd.txt")):
        sys.exit(f"no HD pack in {root} (package/build.ps1 copies it from private-assets/womw-hd)")
    files = {f"hd/{n}": os.path.join(root, n) for n in os.listdir(root) if os.path.isfile(os.path.join(root, n))}
    entries = hashes(files)
    try:
        old = {e["path"]: e for e in json.loads(ssh(f"cat {FILES}/hd-manifest.json 2>/dev/null || echo '{{}}'")).get("files", [])}
    except (subprocess.CalledProcessError, ValueError):
        old = {}
    changed = [e["path"] for e in entries if old.get(e["path"]) != e]
    size = sum(e["size"] for e in entries if e["path"] in changed)
    print(f"HD pack: {len(changed)} of {len(entries)} files changed, {size / 1e9:.2f} GB to send")
    if "--dry-run" in sys.argv or not changed:
        return
    upload(files, changed)
    manifest = json.dumps({"version": time.strftime("%Y-%m-%d %H:%M"), "files": entries})
    subprocess.run(SSH + [f"cat > {FILES}/hd-manifest.json.new && mv {FILES}/hd-manifest.json.new {FILES}/hd-manifest.json && chmod a+r {FILES}/hd-manifest.json"],
                   input=manifest, text=True, check=True)
    print("Published the HD pack.")


def main() -> None:
    if "--hd" in sys.argv:
        publish_hd()
        return
    if "--launcher-only" in sys.argv:
        launcher_only()
        return
    dry = "--dry-run" in sys.argv
    if not dry:
        build_launcher()
    files = wanted(game="--no-game" not in sys.argv)
    entries = hashes(files)
    try:
        old = {e["path"]: e for e in json.loads(ssh(f"cat {FILES}/manifest.json 2>/dev/null || echo '{{}}'")).get("files", [])}
    except (subprocess.CalledProcessError, ValueError):
        old = {}
    changed = [e["path"] for e in entries if old.get(e["path"]) != e]
    size = sum(e["size"] for e in entries if e["path"] in changed)
    print(f"{len(changed)} of {len(entries)} files changed, {size / 1e9:.2f} GB to send")
    if dry:
        return
    if not changed:
        if notes():
            version = json.loads(ssh(f"cat {FILES}/manifest.json"))["version"]
            add_release(version, notes())
            print(f"Notes added to version {version}.")
        return
    # The site says an update is going up until the manifest is in place.
    big = size > 1e9
    if big:
        ssh(f"echo 'A big update is uploading. New installs will work once it finishes.' > {NOTICE}")
    upload(files, changed)
    version = time.strftime("%Y-%m-%d %H:%M")
    manifest = json.dumps({"version": version, "files": entries})
    subprocess.run(SSH + [f"cat > {FILES}/manifest.json.new && mv {FILES}/manifest.json.new {FILES}/manifest.json && chmod a+r {FILES}/manifest.json"],
                   input=manifest, text=True, check=True)
    if big:
        ssh(f"rm -f {NOTICE}")
    if notes():
        add_release(version, notes())
    archive_symbols(version, entries)
    print(f"Published version {version}.")


if __name__ == "__main__":
    main()
