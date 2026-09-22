#!/usr/bin/env bash
# Downloads Belfast's official paintings and chibi sprites (EN client assets, extracted by
# github.com/Fernando2603/AzurLane) into assets/official/, then builds assets/skins/.
# The art is © Manjuu / Yongshi / Yostar — personal use only, do not redistribute.
set -euo pipefail
cd "$(dirname "$0")/.."
BASE=https://raw.githubusercontent.com/Fernando2603/AzurLane/main/images/skin
UA="Mozilla/5.0"
mkdir -p assets/official
declare -A NAMES=([202120]=default [202121]=iridescent-rosa [202122]=serene-steel [202123]=noble-attendant
  [202124]=shopping-casual [202125]=piping-hot-perfection [202126]=folded-fascination
  [202127]=blissful-service [202128]=pledge-of-claddagh [202129]=retrofit)
for id in "${!NAMES[@]}"; do
  n=${NAMES[$id]}
  for f in painting painting_n chibi; do
    out="assets/official/${id}_${n}_${f}.png"
    [ -s "$out" ] && { echo "keep $out"; continue; }
    code=$(curl -sL --max-time 180 -A "$UA" -o "$out" -w "%{http_code}" "$BASE/$id/$f.png")
    if [ "$code" = "200" ]; then echo "got  $out"; else rm -f "$out"; echo "skip $id/$f ($code)"; fi
  done
done
python3 tools/build_skins.py
