WITH bb AS (
  SELECT DISTINCT c.id AS entry FROM creature c JOIN creature_template ct ON ct.entry=c.id
   WHERE c.map IN (533,615,616,603,249,649)
     AND (ct.`rank`=3 OR ct.ScriptName LIKE 'boss%' OR ct.entry IN (SELECT creditEntry FROM instance_encounters WHERE creditType=0))
  UNION SELECT 33288 UNION SELECT 34564 UNION SELECT 34496 UNION SELECT 34497 UNION SELECT 34797 UNION SELECT 34780)
SELECT entry FROM bb
UNION SELECT ct.difficulty_entry_1 FROM creature_template ct JOIN bb ON bb.entry=ct.entry WHERE ct.difficulty_entry_1>0
