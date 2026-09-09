WITH bb AS (
  SELECT DISTINCT c.id AS entry FROM creature c JOIN creature_template ct ON ct.entry=c.id
   WHERE c.map IN (574,575,576,578,595,599,600,601,602,604,608,619,632,650,658,668)
     AND (ct.`rank`=3 OR ct.ScriptName LIKE 'boss%' OR ct.entry IN (SELECT creditEntry FROM instance_encounters WHERE creditType=0))
  UNION SELECT 36658 UNION SELECT 35451 UNION SELECT 26529 UNION SELECT 26530 UNION SELECT 26532 UNION SELECT 31134 UNION SELECT 26668 UNION SELECT 29932 UNION SELECT 29573)
SELECT ct.difficulty_entry_1 AS entry FROM creature_template ct JOIN bb ON bb.entry=ct.entry WHERE ct.difficulty_entry_1>0
