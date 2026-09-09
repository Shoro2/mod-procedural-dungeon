#!/bin/sh
# percase.sh <label> <mapclause> <supplement-union> <slot> <goids>
D="C:/Users/Anwender/AppData/Local/Temp/claude/C--wowstuff-ForgottenLand2-0/63ac9a2a-f034-4f08-a65b-df0c4ee8ca97/scratchpad"
LABEL="$1"; MAPS="$2"; SUPP="$3"; SLOT="$4"; GOS="$5"
if [ "$SLOT" = "0" ]; then
  SEL="SELECT entry FROM bb"
else
  SEL="SELECT ct.difficulty_entry_$SLOT AS entry FROM creature_template ct JOIN bb ON bb.entry=ct.entry WHERE ct.difficulty_entry_$SLOT>0"
fi
cat > "$D/_cs.sql" <<EOF
WITH bb AS (
  SELECT DISTINCT c.id AS entry FROM creature c JOIN creature_template ct ON ct.entry=c.id
   WHERE c.map IN ($MAPS)
     AND (ct.\`rank\`=3 OR ct.ScriptName LIKE 'boss%' OR ct.entry IN (SELECT creditEntry FROM instance_encounters WHERE creditType=0))
   $SUPP)
$SEL
EOF
if [ -z "$GOS" ]; then
  echo "SELECT 0 AS entry WHERE 1=0" > "$D/_gs.sql"
else
  echo "SELECT * FROM (VALUES ROW(0)) v(entry) WHERE 1=0" > /dev/null
  printf 'SELECT %s AS entry' "$(echo "$GOS" | cut -d, -f1)" > "$D/_gs.sql"
  echo "$GOS" | tr ',' '\n' | tail -n +2 | while read -r x; do [ -n "$x" ] && printf ' UNION SELECT %s' "$x" >> "$D/_gs.sql"; done
fi
printf '\n' >> "$D/_gs.sql"
sh "$D/gen4.sh" "$LABEL" "$D/_cs.sql" "$D/_gs.sql" "${6:-1}" 2>/dev/null | grep -E "^(1|2|3)	"
