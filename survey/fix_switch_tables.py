"""Apply hand fixes to XenonAnalyse's jump-table output.

    XenonAnalyse game/files/default.xex game/survey/switch_tables.raw.toml
    python3 survey/fix_switch_tables.py   # writes config/switch_tables.toml
    python3 survey/find_missing_tables.py > config/switch_tables_inline.toml
    python3 survey/fix_switch_tables.py   # again, with the new inline tables
    python3 survey/leaf_switch_bounds.py --write

Reads game/survey/image.bin (survey/xexsurvey.cpp) for the bounds checks.
"""
import re, struct

RAW, OUT = 'game/survey/switch_tables.raw.toml', 'config/switch_tables.toml'

# XenonAnalyse misread code as tables: their "labels" are instruction words.
DROP = {0x825725B0, 0x82576FD0}

# Tables XenonAnalyse missed.
ADD = [
    # 0x825A45D0: lis/addi r12=0x82063BA8; lhzx r0 = half[r5]; r12 = 0x825A45F8 + r0;
    # bctr. No bounds check (r5 is a trusted argument); 18 entries, the next
    # halfword is ASCII ("un"). Offsets relative to 0x825A45F8.
    dict(base=0x825A45D0, r=5, labels=[
        0x825A45F8, 0x825A4618, 0x825A4644, 0x825A465C, 0x825A46C4, 0x825A46E0,
        0x825A4720, 0x825A47C4, 0x825A4848, 0x825A4898, 0x825A48F8, 0x825A492C,
        0x825A4970, 0x825A49A0, 0x825A49CC, 0x825A49F8, 0x825A4A2C, 0x825A47B4]),
]

IMG, IMG_BASE = 'game/survey/image.bin', 0x82000000
img = open(IMG, 'rb').read()
word = lambda a: struct.unpack('>I', img[a - IMG_BASE:a - IMG_BASE + 4])[0]
pdata_starts = set()
for a in range(0x820B1200, 0x820DD6B8, 8):
    pdata_starts.add(word(a))


def is_exit(w):
    """b/ba, blr, bctr, or a bc/bclr/bcctr that always branches (BO 1z1zz)."""
    op, bo = w >> 26, (w >> 21) & 31
    if op == 18:
        return w & 1 == 0
    if op == 16:
        return bo & 0x14 == 0x14 and w & 1 == 0
    if op == 19 and (w >> 1) & 0x3FF in (16, 528):
        return bo & 0x14 == 0x14 and w & 1 == 0
    return False


def borrowed_bounds_check(base):
    """XenonAnalyse's ScanTable takes a table's bounds check from the code
    before its `lis`: walking back up to 32 instructions, the first bgt/ble,
    then the first cmplwi on that cr gives `r`, the label count (immediate+1)
    and `default`. A table with no bounds check of its own (a leaf function
    whose caller guarantees the index) gets a compare from whatever precedes
    it, across a return and into the previous function. Returns (the cmplwi,
    the first exit or .pdata start between it and the table), or None when
    the compare reaches the table."""
    cr = None
    for i in range(32):
        a = base - 4 * i
        w = word(a)
        op, bo, bi = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        if cr is None:
            if (op == 16 or (op == 19 and (w >> 1) & 0x3FF == 16)) and bi % 4 == 1 and bo & 0x1C in (0x0C, 0x04):
                cr = bi // 4
        elif op == 10 and (w >> 21) & 1 == 0 and (w >> 23) & 7 == cr:   # cmplwi on that cr
            cut = [x for x in range(a + 4, base, 4) if is_exit(word(x))]
            cut += [x for x in range(a + 4, base + 4, 4) if x in pdata_starts]
            return (a, min(cut)) if cut else None
    return None


text = open(RAW).read()
head, *blocks = text.split('[[switch]]')
kept, borrowed = [], []
for b in blocks:
    m = re.search(r'base = 0x([0-9A-F]+)', b)
    if m and int(m.group(1), 16) in DROP:
        continue
    # A table whose bounds check is borrowed (see borrowed_bounds_check) has
    # the wrong index register and too few labels. XenonRecomp then emits a
    # switch on that register over those labels with an unreachable default,
    # so the function always takes its first case: every Performance Tuning
    # row got the first row's label ("Steering", GitHub issue #8). Drop it:
    # find_missing_tables.py decodes it from its dispatch, as a table with no
    # bounds check. Regenerate config/switch_tables_inline.toml after this.
    if m and 'default = ' in b:
        cut = borrowed_bounds_check(int(m.group(1), 16))
        if cut:
            borrowed.append((int(m.group(1), 16), *cut))
            continue
    kept.append(b)
assert len(blocks) - len(kept) == len(DROP) + len(borrowed), 'a DROP entry no longer matches the analyzer output'
for base, cmp, cut in borrowed:
    print(f'borrowed bounds check: table 0x{base:08X} took cmplwi 0x{cmp:08X}, cut off at 0x{cut:08X}; dropped')

out = head + '[[switch]]'.join([''] + kept)
out = out.rstrip() + '\n\n# ---- MANUAL (survey/fix_switch_tables.py) ----\n'
for t in ADD:
    out += f"[[switch]]\nbase = 0x{t['base']:08X}\nr = {t['r']}\nlabels = [\n"
    out += ''.join(f'    0x{l:08X},\n' for l in t['labels']) + ']\n\n'
# Inline absolute tables with no bounds check (survey/find_missing_tables.py,
# run against the analyzer output + the fixes above; regenerate it after
# changing either).
out += '# ---- INLINE (config/switch_tables_inline.toml) ----\n'
out += open('config/switch_tables_inline.toml').read()
open(OUT, 'w').write(out)
print(f'{OUT}: {len(kept) + len(ADD)} tables ({len(DROP) + len(borrowed)} dropped, {len(ADD)} added)')
