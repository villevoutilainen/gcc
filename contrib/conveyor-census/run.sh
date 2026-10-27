#!/bin/bash
# Conveyor census: compile every lump in this directory under each library
# configuration and analysis engine, and count the errors located in the
# library's own headers, any internal compiler errors, and the errors in
# the lumps themselves (the checks the library's contracts make of user
# code).
#
#   run.sh [-j N] [-o outdir] [-w] [lump.cc...]
#
# Prints one line per configuration and engine, and writes each compile's
# diagnostics to <outdir>/<config>-<engine>/<lump>.err.  -w records the
# result as the baseline; otherwise the differences from it are reported,
# down to the error sites that appeared or went away.
set -u
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../.." && pwd)
build=${BUILD:-$src/build}
jobs=12
out=${TMPDIR:-/tmp}/conveyor-census
write_baseline=
while getopts "j:o:w" o; do
  case $o in
    j) jobs=$OPTARG ;;
    o) out=$OPTARG ;;
    w) write_baseline=1 ;;
    *) exit 2 ;;
  esac
done
shift $((OPTIND - 1))
lumps=("$@")
[ ${#lumps[@]} -eq 0 ] && lumps=("$here"/*.cc)

cxx=("$build/gcc/xg++" "-B$build/gcc/" -nostdinc++
     -isystem "$build/x86_64-pc-linux-gnu/libstdc++-v3/include/x86_64-pc-linux-gnu"
     -isystem "$build/x86_64-pc-linux-gnu/libstdc++-v3/include"
     -isystem "$src/libstdc++-v3/libsupc++"
     -I"$src/libstdc++-v3/testsuite/util" -I"$here"
     -std=gnu++26 -O0 -fcontracts -fcontract-control-objects -c -o /dev/null)
declare -A configs=(
  [conveyor]="-D_GLIBCXX_CONVEYOR_ASSERTIONS"
  [control]=""
)

mkdir -p "$out"
results=$out/results.txt
: > "$results"
for config in conveyor control; do
  for engine in gimple ast; do
    dir=$out/$config-$engine
    mkdir -p "$dir"
    printf '%s\n' "${lumps[@]}" | xargs -P "$jobs" -I{} sh -c \
      'f={}; '"$(printf '%q ' "${cxx[@]}")"' '"${configs[$config]}"' -fcontract-analysis-engine='"$engine"' "$f" > '"$dir"'/$(basename "$f" .cc).err 2>&1'
    errs=$(cat "$dir"/*.err | grep -c 'libstdc++-v3/include/.*: error:')
    # A site is its header and the text of its source line, not the line
    # number, so that editing a header doesn't move its other sites.
    cat "$dir"/*.err | grep 'libstdc++-v3/include/.*: error:' \
      | sed 's|.*/include/||; s/: error.*//' | sort -u \
      | python3 -c '
import sys
inc = sys.argv[1]
for l in sys.stdin:
    f, line = l.split(":")[:2]
    try:
        text = open(inc + "/" + f).read().split("\n")[int(line) - 1].strip()
    except (OSError, IndexError):
        text = "?"
    print(f + ": " + text)' "$src/libstdc++-v3/include" \
      | sort -u > "$out/$config-$engine.sites"
    sites=$(wc -l < "$out/$config-$engine.sites")
    ices=$(cat "$dir"/*.err | grep -c 'internal compiler error')
    # Errors in the lumps themselves: the user-facing checks the library's
    # contracts make of code like theirs, not library defects -- counted so
    # that a change making the library harder to use shows too.
    # The lumps are compiled from this directory, so their own diagnostics
    # name them without a directory.
    user=$(cat "$dir"/*.err | grep -cE '(^|/conveyor-census/)[^/:]*\.cc:[0-9]+:[0-9]+: (fatal )?error:')
    printf '%-9s %-7s library errors %6d  distinct sites %5d  ICEs %d  user-code errors %d\n' \
      "$config" "$engine" "$errs" "$sites" "$ices" "$user" | tee -a "$results"
  done
done

# The baseline is the counts (baseline.txt) and each configuration and
# engine's distinct error sites (baseline/<config>-<engine>.sites).
if [ -n "$write_baseline" ]; then
  cp "$results" "$here/baseline.txt"
  mkdir -p "$here/baseline"
  cp "$out"/*.sites "$here/baseline/"
elif [ -f "$here/baseline.txt" ]; then
  if diff "$here/baseline.txt" "$results" > /dev/null; then
    echo "same counts as baseline"
  else
    echo "counts differ from baseline:"; diff "$here/baseline.txt" "$results"
  fi
  for f in "$out"/*.sites; do
    b=$here/baseline/$(basename "$f")
    [ -f "$b" ] || continue
    gone=$(comm -23 "$b" "$f"); new=$(comm -13 "$b" "$f")
    [ -n "$gone" ] && printf '%s: sites gone:\n%s\n' "$(basename "$f" .sites)" "$gone"
    [ -n "$new" ] && printf '%s: new sites:\n%s\n' "$(basename "$f" .sites)" "$new"
  done
fi
