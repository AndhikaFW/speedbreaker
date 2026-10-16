#!/usr/bin/env bash
# Check that the guest functions this fork hooks (runtime/game/colour_grade.cpp) still look the way they
# did in the supported default.xex. Run it after an upstream update, before building.
#
#   scripts/fork/check-fork-hooks.sh [ppc-dir] [xex]
#
# Defaults: ppc-dir=ppc, xex=game/files/default.xex. Exit status 1 if anything is missing or different.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
PPC="${1:-ppc}"
XEX="${2:-game/files/default.xex}"
XEX_SHA=aebdf3c14d1e4e4ed2c130d94bc1a0e97bd6d5bf39bdfdef33c3909c9cc4ba4c
fail=0
ok()  { echo "ok    $*"; }
bad() { echo "FAIL  $*"; fail=1; }

[ -d "$PPC" ] || { echo "no $PPC directory: generate the recompiled sources first" >&2; exit 1; }

if [ -f "$XEX" ]; then
  if [ "$(sha256sum "$XEX" | cut -d' ' -f1)" = "$XEX_SHA" ]; then ok "default.xex hash"; else bad "default.xex differs from the supported build; every address below must be re-found"; fi
else
  echo "skip  $XEX not present, hash not checked"
fi

body() { # print the recompiled body of a guest function
  awk -v f="PPC_FUNC_IMPL(__imp__$1)" 'index($0,f){p=1} p{print} p&&/^}/{exit}' "$PPC"/ppc_recomp.*.cpp
}
has() { grep -q "$2" <<<"$(body "$1")"; }

for f in sub_82458C20 sub_82449820 sub_824491F8 sub_82448FE0 sub_82223BB8 sub_82454B58 sub_82454B60 sub_82454C18 sub_82589EA8 sub_8245D1B8; do
  [ -n "$(body "$f")" ] && ok "$f exists" || bad "$f missing"
done

has sub_82454B58 'li r3,1600' && ok "shadow size getter B58 returns 1600" || bad "sub_82454B58 is not 'li r3,1600'"
has sub_82454B60 'li r3,1600' && ok "shadow size getter B60 returns 1600" || bad "sub_82454B60 is not 'li r3,1600'"

# Hooks that key on the caller's return address.
n=$(body sub_82223BB8 | grep -c 'sub_82449820(')
[ "$n" = 4 ] && ok "sub_82223BB8 calls sub_82449820 four times" || bad "sub_82223BB8 calls sub_82449820 $n times (expected 4)"
body sub_82223BB8 | grep -q '0x82223CB0' && ok "return address 0x82223CB0 (grade block send)" || bad "0x82223CB0 not found in sub_82223BB8"
body sub_82454C18 | grep -q '0x82454C7C' && ok "return address 0x82454C7C (shadow size call)" || bad "0x82454C7C not found in sub_82454C18"
body sub_82454C18 | grep -q '0x82454CB8' && ok "return address 0x82454CB8 (shadow size call)" || bad "0x82454CB8 not found in sub_82454C18"
grep -h -A1 'ctx.lr = 0x8222441C;' "$PPC"/ppc_recomp.*.cpp | grep -q 'sub_824491F8(' && ok "return address 0x8222441C (bloom scale)" || bad "no sub_824491F8 call with lr 0x8222441C"

echo
[ "$fail" = 0 ] && echo "all hooks match." || echo "SOME HOOKS DIFFER: see docs/PORTING.md, section 'Re-finding a hook'."
exit $fail
