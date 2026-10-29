# Generates client/assets/class_options.json from design/CLASS_CATALOG.md (the "What it does"
# column) and the server's catalog (server/src/fusion/src/Classes.cpp: the option names and routes),
# so the kit window's talent tooltips can describe the custom options. Run from the repo root: python client/scripts/gen_class_options.py
import json, re, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else '.'
doc = open(f'{ROOT}/design/CLASS_CATALOG.md', encoding='utf-8').read().splitlines()
cpp = open(f'{ROOT}/server/src/fusion/src/Classes.cpp', encoding='utf-8').read()

CLASS_IDS = {'Warrior': 1, 'Paladin': 2, 'Hunter': 3, 'Rogue': 4, 'Priest': 5, 'Shaman': 7, 'Mage': 8,
             'Warlock': 9, 'Druid': 11}

# The server's rows, in order: each class's {cls, archetypes, core x3, tiers 6 x 3}.
row_re = re.compile(r'\{([ABC]), (Ab|Pa|Fo), "((?:[^"\\]|\\.)*)", \{[^}]*\}, (true|false)\}')
blocks = re.split(r'\n\s*\{k(Warrior|Paladin|Hunter|Rogue|Priest|Shaman|Mage|Warlock|Druid), \{', cpp)
server = {}
for i in range(1, len(blocks), 2):
    cls = CLASS_IDS[blocks[i]]
    rows = row_re.findall(blocks[i + 1])[:21]
    assert len(rows) == 21, (blocks[i], len(rows))
    for n, (route, kind, name, _cap) in enumerate(rows):
        if n < 3:
            tier, arch = 0, n
        else:
            tier, arch = 1 + (n - 3) // 3, (n - 3) % 3
        oid = cls * 100 + tier * 10 + arch + 1
        server[oid] = {'route': route, 'name': name.replace('\\"', '"')}

# The option's first spell, for the refit lookup below.
spell_re = re.compile(r'\{([ABC]), (Ab|Pa|Fo), "((?:[^"\\]|\\.)*)", \{(\d+)[^}]*\}, (true|false)\}')
first_spell = {}
for i in range(1, len(blocks), 2):
    cls_id = CLASS_IDS[blocks[i]]
    for n, m in enumerate(spell_re.findall(blocks[i + 1])[:21]):
        tier, arch = (0, n) if n < 3 else (1 + (n - 3) // 3, (n - 3) % 3)
        first_spell[cls_id * 100 + tier * 10 + arch + 1] = int(m[3])

# The audit (Classes.cpp SpecRows): status and note per option.
STATUS = {'W': 'working', 'P': 'partial', 'Status::Working': 'working', 'Status::Partial': 'partial',
          'Status::Unavailable': 'unavailable'}
spec_re = re.compile(r'\{(\d+), (W|P|Status::\w+), \w+, \d+, \d+, [\w:]+, \d+, \d+, "((?:[^"\\]|\\.)*)"\}')
audit = {int(m[0]): (STATUS[m[1]], m[2].replace('\\"', '"')) for m in spec_re.findall(cpp)}
# The refits (Classes.cpp RefitRows): a vanilla spell whose effects the server replaces.
refit_re = re.compile(r'\{(\d+), -?\d+, -?\d+, -?\d+, ([A-Z_| 0]+)\}')
replaced_spells = {int(m[0]) for m in refit_re.findall(cpp) if 'REPLACE' in m[1] or 'SR' in m[1].split()}

out = {}
cls = None
counts = {}
for line in doc:
    m = re.match(r'^## \d+\. (\w+)', line)
    if m:
        cls = CLASS_IDS.get(m.group(1))
        counts = {}
        continue
    if cls is None or not line.startswith('| '):
        continue
    cells = [c.strip() for c in line.strip().strip('|').split('|')]
    if len(cells) < 6 or cells[0] in ('Slot', '---') or cells[0].startswith('-'):
        continue
    slot = cells[0]
    if slot == 'Core':
        tier = 0
    elif slot.isdigit():
        tier = int(slot) // 10
    else:
        continue
    arch = counts.get(tier, 0)
    counts[tier] = arch + 1
    oid = cls * 100 + tier * 10 + arch + 1
    name, kind, text = cells[2], cells[3], cells[4].replace('**', '').replace('`', '')
    s = server.get(oid)
    if not s:
        print('not on the server:', oid, name, file=sys.stderr)
        continue
    if s['name'].lower() != name.lower():
        print(f'name differs: {oid} doc "{name}" server "{s["name"]}"', file=sys.stderr)
    status, note = audit.get(oid, ('working', ''))
    row = {'route': s['route'], 'type': kind, 'text': text, 'status': status, 'note': note}
    if s['route'] == 'A' and first_spell.get(oid) in replaced_spells:
        row['replaced'] = True
    out[str(oid)] = row

# The class identity pass (server/src/fusion/src/ClassTuning.cpp and include/fusion/ClassTuning.h):
# each tuned option's line, the gap-closers' guard, Ambush's sundering, the traps' snap, Ghost Wolf.
tuning_cpp = open(f'{ROOT}/server/src/fusion/src/ClassTuning.cpp', encoding='utf-8').read()
tuning_h = open(f'{ROOT}/server/src/fusion/include/fusion/ClassTuning.h', encoding='utf-8').read()
def const(name):
    return float(re.search(name + r' = ([\d.]+)f?;', tuning_h).group(1))
def oid_of(cls_name, tier, arch):
    return CLASS_IDS[cls_name] * 100 + int(tier) * 10 + int(arch) + 1
identity = {}
def add(oid, text):
    identity.setdefault(oid, []).append(text)
row_t = re.compile(r'\{OptionId\(k(\w+), (\d), (\d)\), ([\d.]+)f, (true|false), "([^"]*)"\}')
for cls_name, tier, arch, dmg, homing, why in row_t.findall(tuning_cpp):
    oid = oid_of(cls_name, tier, arch)
    if float(dmg) != 1.0:
        add(oid, f'Hits {float(dmg):g}x as hard as its spell says.')
    if homing == 'true':
        add(oid, 'Its bolts home on the target while it is in the kit.')
gap = re.search(r'bool IsGapCloser.*?return true;', tuning_cpp, re.S).group(0)
guard = f'Charging through fire: {round((1 - const("kGapGuardBulletCut")) * 100)}% fewer bullets for {const("kGapGuardMs") / 1000:g} s after it.'
for cls_name, tier, arch in re.findall(r'case OptionId\(k(\w+), (\d), (\d)\):', gap):
    add(oid_of(cls_name, tier, arch), guard)
add(411, f'Sunders: a player it lands on is disarmed {const("kSunderDisarmMs") / 1000:g} s (no gun, no weapon).')
for oid in (312, 332, 342):
    add(oid, f'Snap: going off on a player, it stuns them {const("kTrapSnapMs") / 1000:g} s first.')
add(701, f'A spirit wolf is hard to hit: {round((1 - const("kGhostWolfBulletCut")) * 100)}% fewer bullets in the form.')
for oid, lines in identity.items():
    if str(oid) in out:
        out[str(oid)]['identity'] = ' '.join(lines)
    else:
        print('identity for an unknown option:', oid, file=sys.stderr)

missing = sorted(set(map(str, server)) - set(out))
if missing:
    print('no text for:', missing, file=sys.stderr)
no_audit = sorted(set(server) - set(audit))
if no_audit:
    print('no audit row for:', no_audit, file=sys.stderr)
json.dump(out, open(f'{ROOT}/client/assets/class_options.json', 'w', encoding='utf-8'), indent=0,
          ensure_ascii=False, sort_keys=True)
print(len(out), 'options')
