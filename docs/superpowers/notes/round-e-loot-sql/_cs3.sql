WITH bb AS (SELECT DISTINCT c.id AS entry FROM creature c JOIN creature_template ct ON ct.entry=c.id
  WHERE c.map IN (649) AND (ct.`rank`=3 OR ct.ScriptName LIKE 'boss%' OR ct.entry IN (SELECT creditEntry FROM instance_encounters WHERE creditType=0))
  UNION SELECT 34564 UNION SELECT 34496 UNION SELECT 34497 UNION SELECT 34797 UNION SELECT 34780)
SELECT ct.difficulty_entry_2 AS entry FROM creature_template ct JOIN bb ON bb.entry=ct.entry WHERE ct.difficulty_entry_2>0
UNION SELECT ct.difficulty_entry_3 FROM creature_template ct JOIN bb ON bb.entry=ct.entry WHERE ct.difficulty_entry_3>0
