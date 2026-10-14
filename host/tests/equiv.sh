#!/bin/sh
# Test d'équivalence : le cœur porté (rscw_host) doit produire exactement le même texte que le
# rscw d'origine (rscw_orig, compilé depuis original-rscw-0.1e/ avec le shim FFTW) sur des
# signaux générés par le noisycw d'origine.
# Usage : tests/equiv.sh [rapide]     (depuis host/, après make)
set -e
cd "$(dirname "$0")/.."
B=build
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
pass=0; fail=0

run() { # $1=nom  $2=options noisycw  $3=options rscw  $4=fichier texte
   $B/noisycw -q $2 < "$4" > "$T/in.u8" 2>/dev/null
   $B/rscw_orig -x $3 < "$T/in.u8" > "$T/a.txt"
   $B/rscw_host $3 < "$T/in.u8" > "$T/b.txt"
   if cmp -s "$T/a.txt" "$T/b.txt"; then
      pass=$((pass+1)); printf 'OK    %-28s %s\n' "$1" "$(tail -c 60 "$T/b.txt" | tr -d '\n')"
   else
      fail=$((fail+1)); printf 'DIFF  %-28s\n' "$1"; diff "$T/a.txt" "$T/b.txt" | head -4
   fi
}

C=tests/corpus.txt
# long : ~4 min de texte, au-delà des rebouclages des tampons de l'original (16 s / 65 s)
for i in 1 2 3 4 5 6; do cat $C; done > "$T/long.txt"
# bruit seul : uniquement des espaces (stress du pool d'hypothèses)
printf '%80s\n' "" > "$T/noise.txt"

for e in 20 8 4 0; do
   for w in 12 20 30; do
      run "eb=$e wpm=$w strongest" "-e $e -w $w -f 800" "-w $w" $C
   done
   run "eb=$e wpm=20 track" "-e $e -w 20 -f 1200" "-w 20 -t" $C
   run "eb=$e wpm=20 fixed" "-e $e -w 20 -f 600" "-w 20 -f 600" $C
   run "eb=$e wpm=20 range+bp" "-e $e -w 20 -f 700 -b" "-w 20 -f 400-1000" $C
done
if [ "$1" != "rapide" ]; then
   run "long eb=6 wpm=25" "-e 6 -w 25 -f 900" "-w 25" "$T/long.txt"
   run "long eb=6 wpm=25 track" "-e 6 -w 25 -f 900" "-w 25 -t" "$T/long.txt"
   run "slow eb=10 wpm=6" "-e 10 -w 6 -f 900" "-w 6" $C
   run "fast eb=10 wpm=50" "-e 10 -w 50 -f 900" "-w 50" "$T/long.txt"
   run "mismatch wpm 20/22" "-e 10 -w 20 -f 900" "-w 22" $C
   run "noise only" "-e 0 -w 20 -f 900" "-w 20" "$T/noise.txt"
fi
echo "equivalence: $pass OK, $fail DIFF"
[ $fail -eq 0 ]
