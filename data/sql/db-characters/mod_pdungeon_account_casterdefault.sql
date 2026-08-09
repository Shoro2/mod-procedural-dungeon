-- The caster-ratio default moved from 60 to 40 at the first in-game test
-- (operator, 2026-08-07): runs default to 40 % casters / 60 % melee. The loot
-- multiplier's anchor moved with it in PDv2GameMath.h (0.8 + 0.5 r), so the
-- default still advertises exactly x1.00.
--
-- The data UPDATE targets exactly the old default value: every row holding 60
-- got it from the schema default or the INSERT seeding, never from a player
-- choice (the panel shipped after this file). Once players CAN choose 60
-- deliberately this file has long been applied everywhere it will ever run,
-- and the updater never re-runs an unchanged file - do not edit it later.

ALTER TABLE `pdungeon_account` ALTER `cfg_caster_pct` SET DEFAULT 40;

UPDATE `pdungeon_account` SET `cfg_caster_pct` = 40 WHERE `cfg_caster_pct` = 60;
