-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: FL procedural-dungeon mats (world database)
-- 01 §8 "Custom FL mats drop as a module-side bonus roll on top of the native
-- table, scaled by lootMult"; 01 §12 reserves items 920100-920149 for them.
-- This file allocates the first five; the vault's 06-custom-ids registry is
-- updated by the planner, not by this file.
--
-- Verified free before allocation (measured 2026-08-07 against the live world
-- DB): item_template holds nothing between 920100 and 920149; the existing
-- custom item block starts at 920920.
--
-- Column shape copied from a stock trade good (23445 Fel Iron Bar / 36913
-- Saronite Bar): class 7, InventoryType 0, no stats, no spells, bonding 0.
-- Subclass 0 is the generic "Trade Goods" bucket on purpose - the crystalline
-- display ids are borrowed from enchanting mats, but filing these under
-- subclass 12 would put them in the client's enchanting-reagent filters and
-- imply a profession use they do not have.
--
-- No client patch is needed: every displayid below already exists in the
-- 3.3.5a client's ItemDisplayInfo.dbc, borrowed from the stock item named in
-- each row's comment. Item.dbc itself is only consulted for items the client
-- must render before the server describes them, which is not the case here.
--
-- Quality ladder (01 §8 leaves the exact split open): 2/2/3/3/4, sell price
-- ascending with it, ItemLevel 80 across the board because everything the
-- dungeon spawns is normalised to 80.
-- ----------------------------------------------------------------------------

DELETE FROM `item_template` WHERE `entry` BETWEEN 920100 AND 920104;

INSERT INTO `item_template`
  (`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `Flags`,
   `BuyCount`, `BuyPrice`, `SellPrice`, `InventoryType`, `ItemLevel`,
   `RequiredLevel`, `maxcount`, `stackable`, `bonding`, `Material`, `sheath`,
   `Description`) VALUES
  (920100, 7, 0, 'Forgotten Shard',    56459, 2, 0, 1,      0,   2500, 0, 80, 0, 0, 20, 0, 2, 0,
   'Torn loose from the Forgotten Depths.'),
  (920101, 7, 0, 'Forgotten Sliver',   39198, 2, 0, 1,      0,   5000, 0, 80, 0, 0, 20, 0, 2, 0,
   'Torn loose from the Forgotten Depths.'),
  (920102, 7, 0, 'Forgotten Fragment', 56461, 3, 0, 1,      0,  12500, 0, 80, 0, 0, 20, 0, 2, 0,
   'Torn loose from the Forgotten Depths.'),
  (920103, 7, 0, 'Forgotten Core',     58413, 3, 0, 1,      0,  25000, 0, 80, 0, 0, 20, 0, 2, 0,
   'Torn loose from the Forgotten Depths.'),
  (920104, 7, 0, 'Forgotten Relic',    56465, 4, 0, 1,      0,  50000, 0, 80, 0, 0, 20, 0, 2, 0,
   'Torn loose from the Forgotten Depths.');
