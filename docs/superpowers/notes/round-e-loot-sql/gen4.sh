#!/bin/sh
# gen4.sh <label> <cseedfile> <gseedfile> <lootmodemask> [template]
D="C:/Users/Anwender/AppData/Local/Temp/claude/C--wowstuff-ForgottenLand2-0/63ac9a2a-f034-4f08-a65b-df0c4ee8ca97/scratchpad"
T="${5:-$D/tmpl4.sql}"
CS=$(cat "$2")
GS=$(cat "$3")
awk -v cs="$CS" -v gs="$GS" -v lbl="$1" -v lm="$4" '{gsub(/__CSEED__/,cs); gsub(/__GSEED__/,gs); gsub(/__LABEL__/,lbl); gsub(/__LM__/,lm); print}' "$T" > "$D/_run4.sql"
sh "$D/q.sh" "$D/_run4.sql"
