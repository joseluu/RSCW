#!/bin/sh
# Simulation du sketch complet sur PC (host/build/sim_rscw) : vérifie le harnais de bout en bout.
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
check() { if eval "$2"; then echo "OK    $1"; else echo "ECHEC $1"; fail=1; fi; }

# 1. Scénario par défaut : 20 WPM, Eb/N0 20 dB, status / set wpm / log on / scope / status
build/sim_rscw > "$T/out" 2> "$T/err"
tr -d '\r' < "$T/out" > "$T/o"
cer=$(sed -n 's/^CER=//p' "$T/err")
check "identification status (Decoder: RSCW)" "grep -q '^Decoder: RSCW ' '$T/o'"
check "set wpm -> 'wpm = 20.0 (decoder reset)'" "grep -q '^wpm = 20.0 (decoder reset)$' '$T/o'"
check "texte décodé sur le TFT (hors bruit avant/après), CER=$cer < 0.02" "awk 'BEGIN{exit !($cer < 0.02)}'"
check "aucune perte de file ni débordement du pool" "grep -q 'queue_drop=0' '$T/o' && tail -5 '$T/o' | grep -q 'msgovf=0'"
check "capture scope : en-tête" "grep -q 'Time	Sync	Tone	Detect	Power\$' '$T/o'"
n=$(awk -F'\t' 'NF==5 && $1 ~ /^[0-9]+$/' "$T/o" | wc -l)
ok=$(awk -F'\t' 'NF==5 && $1 ~ /^[0-9]+$/ && $3==$4' "$T/o" | wc -l)
check "capture scope : $n lignes (>= 280)" "[ $n -ge 280 ]"
check "capture scope : Detect = Tone sur $ok/$n lignes (>= 90 %)" "[ \$((ok * 100)) -ge \$((n * 90)) ]"
check "capture scope : déclenchée par la pin 14 (1re ligne Sync=1)" "awk -F'\t' 'NF==5 && \$1 ~ /^[0-9]+\$/ {print \$2; exit}' '$T/o' | grep -q 1"

# 2. Langage de commandes
build/sim_rscw -r 1 -c "0.1:help" -c "0.2:set fmode track" -c "0.3:set freq 900" -c "0.4:set range 300 1500" \
   -c "0.5:set wpm 70" -c "0.6:set out0 BIT" -c "0.7:set out1 NOPE" -c "0.8:set gain 10" -c "0.9:set siggen 6" \
   -c "1.0:peak" -c "1.1:frobnicate" -c "1.2:set wpm 12.5" -c "1.3:reset" -c "1.4:log off" -c "30:freq" -c "30.1:status" \
   > "$T/out2" 2>/dev/null
tr -d '\r' < "$T/out2" > "$T/o2"
for r in "fmode = track" "freq = 900.0 (fixed)" "range = 300.0 1500.0" "Error: wpm must be 5..60" \
         "Out Left -> BIT" "Error: unknown source 'NOPE'" "Gain = 10.0" "Sig Gen = 900Hz" "wpm = 12.5 (decoder reset)" \
         "reset ok" "log off" "Unknown command: frobnicate" "WPM: 12.5" "Out Left: BIT" "fmode=track fmin=300.0 fmax=1500.0"; do
   check "réponse « $r »" "grep -qF \"$r\" '$T/o2'"
done
check "réponse « ADC peak max=… »" "grep -q '^ADC peak max=0.[0-9]* min=0.[0-9]*' '$T/o2'"
check "réponse « freq: … found=1 » (porteuse ~900 Hz)" "grep -Eq '^freq: (89[0-9]|90[0-9])\.[0-9] found=1' '$T/o2'"

[ $fail -eq 0 ] && echo "simulation: OK" || { echo "simulation: ECHEC"; exit 1; }
