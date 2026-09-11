-- Pool resolver v4: creature + gameobject sources, reference recursion, LootMode filter
WITH RECURSIVE
ctpl AS (WITH bb AS (
  SELECT DISTINCT c.id AS entry FROM creature c JOIN creature_template ct ON ct.entry=c.id
   WHERE c.map IN (533,615,616,603,249,649)
     AND (ct.`rank`=3 OR ct.ScriptName LIKE 'boss%' OR ct.entry IN (SELECT creditEntry FROM instance_encounters WHERE creditType=0))
  UNION SELECT 33288 UNION SELECT 34564 UNION SELECT 34496 UNION SELECT 34497 UNION SELECT 34797 UNION SELECT 34780)
SELECT entry FROM bb
UNION SELECT ct.difficulty_entry_1 FROM creature_template ct JOIN bb ON bb.entry=ct.entry WHERE ct.difficulty_entry_1>0),
gsrc AS (SELECT 181366 AS entry UNION SELECT 193426 UNION SELECT 190663 UNION SELECT 193597
UNION SELECT 193905 UNION SELECT 193967 UNION SELECT 194158 UNION SELECT 194159
UNION SELECT 195046 UNION SELECT 195047 UNION SELECT 194312 UNION SELECT 194314
UNION SELECT 194307 UNION SELECT 194308 UNION SELECT 194789 UNION SELECT 194956
UNION SELECT 194821 UNION SELECT 194822 UNION SELECT 194324 UNION SELECT 194325
UNION SELECT 194326 UNION SELECT 194327 UNION SELECT 194328 UNION SELECT 194329
UNION SELECT 194330 UNION SELECT 194331 UNION SELECT 195631 UNION SELECT 195632),
cl AS (SELECT DISTINCT ct.lootid AS id FROM creature_template ct JOIN ctpl t ON t.entry=ct.entry WHERE ct.lootid>0),
gl AS (SELECT DISTINCT gt.Data1 AS id FROM gameobject_template gt JOIN gsrc g ON g.entry=gt.entry WHERE gt.type=3 AND gt.Data1>0),
refs AS (
  SELECT DISTINCT clt.Reference AS rid FROM creature_loot_template clt JOIN cl ON cl.id=clt.Entry WHERE clt.Reference<>0 AND (clt.LootMode & 1)
  UNION
  SELECT DISTINCT glt.Reference FROM gameobject_loot_template glt JOIN gl ON gl.id=glt.Entry WHERE glt.Reference<>0
  UNION
  SELECT rlt.Reference FROM reference_loot_template rlt JOIN refs r ON r.rid=rlt.Entry WHERE rlt.Reference<>0),
items AS (
  SELECT DISTINCT clt.Item AS item FROM creature_loot_template clt JOIN cl ON cl.id=clt.Entry WHERE clt.Reference=0 AND clt.Item>0 AND (clt.LootMode & 1)
  UNION
  SELECT DISTINCT glt.Item FROM gameobject_loot_template glt JOIN gl ON gl.id=glt.Entry WHERE glt.Reference=0 AND glt.Item>0
  UNION
  SELECT DISTINCT rlt.Item FROM reference_loot_template rlt JOIN refs r ON r.rid=rlt.Entry WHERE rlt.Reference=0 AND rlt.Item>0),
it AS (SELECT i.item, t.name, t.Quality, t.ItemLevel, t.InventoryType, t.class, t.subclass,
              (t.stat_type1=35 OR t.stat_type2=35 OR t.stat_type3=35 OR t.stat_type4=35 OR t.stat_type5=35
               OR t.stat_type6=35 OR t.stat_type7=35 OR t.stat_type8=35 OR t.stat_type9=35 OR t.stat_type10=35) AS pvp
       FROM items i JOIN item_template t ON t.entry=i.item)
SELECT * FROM (
  SELECT 0 AS k, '== P2c == raw distinct items' AS bucket, COUNT(*) AS n FROM it
  UNION ALL SELECT 1, 'GEAR: Q>=4 AND InvType<>0 AND class NOT IN (10,12)', COUNT(*)
      FROM it WHERE Quality>=4 AND InventoryType<>0 AND class NOT IN (10,12)
  UNION ALL SELECT 1, 'GEAR PvE only (same + no resilience stat)', COUNT(*)
      FROM it WHERE Quality>=4 AND InventoryType<>0 AND class NOT IN (10,12) AND pvp=0
  UNION ALL SELECT 2, CONCAT('  PvE gear ilvl ', LPAD(ItemLevel,3,'0')), COUNT(*)
      FROM it WHERE Quality>=4 AND InventoryType<>0 AND class NOT IN (10,12) AND pvp=0 GROUP BY ItemLevel
  UNION ALL SELECT 3, CONCAT('TOKEN class15 sub0 Q>=4 ilvl ', LPAD(ItemLevel,3,'0')), COUNT(*)
      FROM it WHERE class=15 AND subclass=0 AND InventoryType=0 AND Quality>=4 GROUP BY ItemLevel
) z ORDER BY k, bucket;
