#!/usr/bin/env bash
# Runs each scenario in scenarios/ against a fresh copy of sample_data/ and compares
# the program's actual output with the expected lines in the matching .expect file.
#
#   tests/run_scenarios.sh            run all scenarios
#   tests/run_scenarios.sh 03         run the scenario whose name starts with 03
#
# A .txt scenario is a list of CLI commands piped into the program. A line "# args: ..."
# passes extra program arguments. A .sh scenario is a script that receives the binary
# and the data directory. Each line of the .expect file must appear in the output, in
# order. The full actual output is kept in scenarios/actual/<name>.out.

BIN=./library_tracker
[ -x "$BIN" ] || { echo "Build first: make"; exit 1; }
mkdir -p scenarios/actual
pass_total=0; fail_total=0; scen_failed=0

for scen in scenarios/${1:-}*.txt scenarios/${1:-}*.sh; do
    [ -f "$scen" ] || continue
    name=$(basename "${scen%.*}")
    expect="scenarios/$name.expect"
    out="scenarios/actual/$name.out"
    work=$(mktemp -d)
    cp -r sample_data/. "$work"/

    if [[ "$scen" == *.sh ]]; then
        bash "$scen" "$PWD/$BIN" "$work" > "$out" 2>&1
    else
        args=$(sed -n 's/^# args: //p' "$scen")
        # shellcheck disable=SC2086
        grep -v '^#' "$scen" | printf '%s\nexit\n' "$(cat)" | "$BIN" "$work" --difficulty 1 $args > "$out" 2>&1
    fi
    rm -rf "$work"

    echo "=== $name"
    mapfile -t lines < "$out"
    pos=0; fails=0
    while IFS= read -r want; do
        [ -z "$want" ] && continue
        found=-1
        for ((i = pos; i < ${#lines[@]}; i++)); do
            if [[ "${lines[i]}" == *"$want"* ]]; then found=$i; break; fi
        done
        if [ $found -ge 0 ]; then
            echo "  PASS  expected: $want"
            pos=$((found + 1)); pass_total=$((pass_total + 1))
        else
            echo "  FAIL  expected: $want   (not found in order; see $out)"
            fails=$((fails + 1)); fail_total=$((fail_total + 1))
        fi
    done < "$expect"
    [ $fails -gt 0 ] && scen_failed=$((scen_failed + 1))
done

echo "---"
echo "Checks passed: $pass_total, failed: $fail_total"
[ $fail_total -eq 0 ] && echo "ALL SCENARIOS PASSED" || { echo "SCENARIOS FAILED: $scen_failed"; exit 1; }
