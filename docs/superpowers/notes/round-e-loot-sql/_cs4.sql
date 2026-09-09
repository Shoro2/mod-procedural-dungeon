WITH bb AS (SELECT DISTINCT c.id AS entry FROM creature c JOIN creature_template ct ON ct.entry=c.id
  WHERE c.map IN (631) AND (ct.`rank`=3 OR ct.ScriptName LIKE 'boss%' OR ct.entry IN (SELECT creditEntry FROM instance_encounters WHERE creditType=0))
  UNION SELECT 36853)
SELECT entry FROM bb
UNION SELECT ct.difficulty_entry_1 FROM creature_template ct JOIN bb ON bb.entry=ct.entry WHERE ct.difficulty_entry_1>0
