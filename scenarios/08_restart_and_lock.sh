# Scenario 8: state survives a restart; a tampered chain.dat locks the system on the next start.
BIN="$1"; DIR="$2"
printf 'borrow_book B001 M001\nmine_solo MINER1\nexit\n' | "$BIN" "$DIR" --difficulty 1
echo "=== second start: the chain is loaded and re-validated"
printf 'view_copy B001-C1\nchain_view\nexit\n' | "$BIN" "$DIR" --difficulty 1
echo "=== altering the sealed_by field of block 1 inside chain.dat (MINER1 -> HACKER)"
python3 - "$DIR/chain.dat" <<'PY'
import sys
p = sys.argv[1]
b = open(p, 'rb').read()
open(p, 'wb').write(b.replace(b'MINER1', b'HACKER', 1))
PY
echo "=== third start: tampered file"
printf 'borrow_book B002 M002\nmine_solo\nvalidate_chain\nexit\n' | "$BIN" "$DIR" --difficulty 1
