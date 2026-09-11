-- P6: materials pool, expansion-classified
WITH m AS (
  SELECT entry, name, class, subclass, Quality, ItemLevel, stackable,
         CASE WHEN entry < 21000 THEN 0 WHEN entry < 33000 THEN 1 ELSE 2 END AS expansion,
         CASE WHEN class=3 THEN 'gem'
              WHEN subclass=5  THEN 'cloth'
              WHEN subclass=6  THEN 'leather'
              WHEN subclass=7  THEN 'metal_stone'
              WHEN subclass=8  THEN 'cooking_meat'
              WHEN subclass=9  THEN 'herb'
              WHEN subclass=10 THEN 'elemental'
              WHEN subclass=12 THEN 'enchanting'
              WHEN subclass=4  THEN 'jewelcrafting'
              WHEN subclass=1  THEN 'parts'
              WHEN subclass=2  THEN 'explosives'
              WHEN subclass=3  THEN 'devices'
              WHEN subclass=11 THEN 'other'
              WHEN subclass=13 THEN 'materials'
              WHEN subclass IN (14,15) THEN 'vellum'
              ELSE CONCAT('tg_sub', subclass) END AS category
  FROM item_template
  WHERE class IN (3,7)
    AND entry < 56000                       -- everything >= 56000 in class 3/7 is FL-custom on this realm
    AND (Flags & 0x10) = 0                  -- ITEM_FLAG_DEPRECATED
    AND Quality > 0
    AND displayid > 0
    AND (stackable >= 2 OR class = 3)
    AND name NOT REGEXP '(?i)(deprecated|unused|monster|zzold|zzz|debug|placeholder|\\(old\\)|test|qa )'
)
SELECT category,
       SUM(expansion=0) AS classic, SUM(expansion=1) AS tbc, SUM(expansion=2) AS wotlk,
       COUNT(*) AS total
FROM m WHERE category IN ('cloth','leather','metal_stone','herb','elemental','enchanting','cooking_meat','gem','jewelcrafting') GROUP BY category WITH ROLLUP ORDER BY total DESC;
