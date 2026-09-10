-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: the five Round-E currencies (world database)
--
-- Round E spec section 3 "L2 - the five currencies and their drop rules":
-- every dungeon mob rolls T1-T3 per player, the finale cache rolls T4/T5 per
-- looter, and the Forgotten Talents tree is the only sink. The module never
-- hardcodes these ids - they reach the code through the conf keys
-- ProceduralDungeon.V2.Loot.Currency.Tier1..5.Item, whose defaults are the
-- five entries below.
--
-- Decision D1, confirmed by the operator 2026-09-10 ("so wie in WoW"): the tier
-- ladder IS the WoW quality ladder, so the item Quality column carries the
-- colour and nothing else has to encode it -
--   T1 Faded Remnant     Quality 1 white   every mob, 100 %
--   T2 Gleaming Remnant  Quality 2 green   every mob,   5 %
--   T3 Radiant Remnant   Quality 3 blue    every mob,   1 %
--   T4 Sovereign Remnant Quality 4 purple  finale cache, 50 %, run difficulty >= 50
--   T5 Eternal Remnant   Quality 5 orange  finale cache, 10 %, run difficulty >= 75
--
-- Ids: 06-custom-ids.md reserves items 920100-920149 for PDv2; 920100-920104 are
-- the FL mats of mod_pdungeon_flmats.sql, so this file takes the next five.
-- Verified free before allocation (measured 2026-09-10 against the live world
-- DB): SELECT COUNT(*) FROM item_template WHERE entry BETWEEN 920105 AND 920109
-- returns 0.
--
-- Row shape copied from mod_pdungeon_flmats.sql (class 7, InventoryType 0, no
-- stats, no spells) with the three deliberate differences the spec asks for:
-- bonding 1 (bind on pickup - a currency must not be traded or mailed between
-- characters), stackable 1000 (T1 drops from every mob at 100 %, so a 20-stack
-- would flood the bags), and Quality 1..5 instead of a fixed mat quality.
-- Subclass 0 is the generic "Trade Goods" bucket for the same reason as the
-- mats: the borrowed displays come from elemental reagents, but filing these
-- under a profession subclass would put them in the client's reagent filters
-- and imply a crafting use they do not have. SellPrice 0 - these are only ever
-- spent in the talent tree, and a vendor price would be a second, unbalanced
-- sink.
--
-- Displays: one stock family in five colours, the WotLK "Crystallized" elemental
-- reagents (inv_elemental_crystal_*), so the five tiers read as one ladder in
-- the bag and stay visually distinct from the FL mats, which borrow enchanting
-- displays (56459 / 39198 / 56461 / 58413 / 56465). Each borrowed display is
-- named in its row comment and was confirmed to exist on this realm
-- (measured 2026-09-10: entries 37700/37702/37703/37704/37705 all present, each
-- display used by exactly one stock item). The plan's literal probe -
-- class 7 AND subclass 4 per quality - was not usable above Quality 2: it
-- returns only deprecated and test rows there (zzOLD/zzDEPRECATED Ornate Ruby,
-- "TCHILTON TEST RUBY"), which is a bad thing to borrow art from.
--
-- No client patch is needed: every displayid below already exists in the
-- 3.3.5a client's ItemDisplayInfo.dbc, borrowed from the stock item named in
-- each row's comment. Item.dbc itself is only consulted for items the client
-- must render before the server describes them, which is not the case here.
-- ----------------------------------------------------------------------------

DELETE FROM `item_template` WHERE `entry` BETWEEN 920105 AND 920109;

INSERT INTO `item_template`
    (`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `Flags`,
     `BuyCount`, `BuyPrice`, `SellPrice`, `InventoryType`, `ItemLevel`,
     `RequiredLevel`, `maxcount`, `stackable`, `bonding`, `Material`, `sheath`,
     `Description`) VALUES
    -- display 55240 borrowed from 37700 Crystallized Air (pale white crystal)
    (920105, 7, 0, 'Faded Remnant',     55240, 1, 0, 1, 0, 0, 0, 80, 0, 0, 1000, 1, 2, 0,
     'Currency of the Forgotten Depths. Spent in the Forgotten Talents tree.'),
    -- display 55242 borrowed from 37704 Crystallized Life (green crystal)
    (920106, 7, 0, 'Gleaming Remnant',  55242, 2, 0, 1, 0, 0, 0, 80, 0, 0, 1000, 1, 2, 0,
     'Currency of the Forgotten Depths. Spent in the Forgotten Talents tree.'),
    -- display 60021 borrowed from 37705 Crystallized Water (blue crystal)
    (920107, 7, 0, 'Radiant Remnant',   60021, 3, 0, 1, 0, 0, 0, 80, 0, 0, 1000, 1, 2, 0,
     'Currency of the Forgotten Depths. Spent in the Forgotten Talents tree.'),
    -- display 55243 borrowed from 37703 Crystallized Shadow (purple crystal)
    (920108, 7, 0, 'Sovereign Remnant', 55243, 4, 0, 1, 0, 0, 0, 80, 0, 0, 1000, 1, 2, 0,
     'Currency of the Forgotten Depths. Spent in the Forgotten Talents tree.'),
    -- display 55241 borrowed from 37702 Crystallized Fire (orange crystal)
    (920109, 7, 0, 'Eternal Remnant',   55241, 5, 0, 1, 0, 0, 0, 80, 0, 0, 1000, 1, 2, 0,
     'Currency of the Forgotten Depths. Spent in the Forgotten Talents tree.');
