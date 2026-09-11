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
-- ============================================================================
-- WP12 (2026-09-11): THE FIVE ARE CURRENCY TOKENS, NOT TRADE GOODS
-- ============================================================================
--
-- They now live in the character sheet's Currency tab (inventory slots
-- CURRENCYTOKEN_SLOT_START..END = 118..149, Player.h:722-723) instead of the
-- bags. Three reasons, in the order they were raised:
--
--   1. BAG SPACE. T1 drops from every mob at 100 %, so a Remnant stack is in
--      every player's bag for ever. A currency token occupies one of the 32
--      hidden token slots and no bag slot at all.
--
--   2. THE STORAGE AND THE LOOT FILTER CAN NO LONGER TAKE THEM. Both storage
--      predicates are written in terms of the item CLASS:
--      mod-loot-filter's IsStorageEligible (src/LootFilter.cpp:377-386) and
--      the Endless Storage's own Lua IsEligible
--      (dcore\lua_scripts\Storage\endless_storage_server.lua:88-99) both accept
--      only class 9, class 0 subclass 5, or class 7 / class 3 with a stack > 1.
--      As class 7 trade goods with stackable 1000 the Remnants PASSED, and a
--      deposited Remnant is a Remnant the talent tree cannot see (WP11 had to
--      guard GrantMaterial by entry against exactly that). As class 10 they
--      fail both predicates on the class alone, and the storage's own
--      "Deposit All Materials" button never even looks at them: it walks bag 255
--      slots 23-38 and bags 19-22 (same file, :170-195) and the token slots are
--      neither.
--
--   3. THE TALENT TREE STILL SEES THEM. mod-forgotten-talents spends the
--      Remnants through Player::HasItemCount / GetItemCount / DestroyItemCount
--      (src/ForgottenTalentsService.cpp:814, 827, 842), and all three iterate
--      KEYRING_SLOT_START..CURRENCYTOKEN_SLOT_END alongside the bags -
--      PlayerStorage.cpp:337-340 (GetItemCount), :671-680 (HasItemCount) and
--      :3220-3247 (DestroyItemCount). Buying a talent therefore works out of
--      the Currency tab with no change in mod-forgotten-talents.
--
-- Three columns move, and only three:
--
--   class      7 -> 10   ITEM_CLASS_MONEY, what the stock emblems are
--                        (measured: 40752 Emblem of Heroism, 45624 Conquest,
--                        49426 Frost are all class 10 subclass 0). It is also
--                        what fails both storage predicates.
--   BagFamily  0 -> 8192 BAG_FAMILY_MASK_CURRENCY_TOKENS (ItemTemplate.h:240).
--                        THIS bit alone is what the engine reads:
--                        ItemTemplate::IsCurrencyToken() is
--                        `BagFamily & BAG_FAMILY_MASK_CURRENCY_TOKENS`
--                        (ItemTemplate.h:725) and Player::CanStoreItem routes on
--                        it into CURRENCYTOKEN_SLOT_START..END
--                        (PlayerStorage.cpp:1462-1464).
--   Flags      0 -> 2048 ITEM_FLAG_MULTI_DROP, the flag both stock emblems
--                        carry. LootMgr.cpp:400 turns it into `freeforall`, so
--                        every player who opens Chromie's Cache gets their own
--                        T4/T5 roll instead of the first clicker taking it. The
--                        per-kill tiers already grant per player, so this makes
--                        the cache behave like the rest of the funnel.
--
-- Kept exactly as they were, deliberately: entry, name, displayid, Quality 1..5
-- (the tier ladder), subclass 0, InventoryType 0, BuyCount 1, BuyPrice 0,
-- SellPrice 0, ItemLevel 80, RequiredLevel 0, maxcount 0, bonding 1, Material 2,
-- sheath 0 and the Description. In particular:
--   stackable stays 1000. The stock template does NOT demand 2147483647 -
--   measured over all 41 currency tokens on this realm the stacks are
--   2147483647 (21), 20000000 (6), 100 (4), 255 (4), 5000 (2), 500000, 200,
--   9999 and 1 - and FL's own existing tokens are finite too (920920 Paragon
--   Point 9999, 251144 Mount Token 100, 251145 Cosmetic Token 200). A stack
--   past 1000 simply takes a second of the 32 token slots, and every count and
--   destroy path sums across them.
--   maxcount stays 0 (no cap), which is what the stock emblems carry too.
--   Material stays 2. Material only selects the pick-up sound, and a token can
--   never be picked up: the core refuses to move one out of the hidden bag
--   (PlayerStorage.cpp:2124-2131).
-- None of this is enforced against Item.dbc, because these entries are not IN
-- Item.dbc - ObjectMgr::LoadItemTemplates skips the whole enforceDBCAttributes
-- block on `if (!dbcitem) continue;` (ObjectMgr.cpp:3488-3491).
--
-- THE currencytypes_dbc ROWS BELOW ARE NOT OPTIONAL. Without them the
-- BagFamily bit is silently REMOVED at startup and this file does nothing at
-- all: ObjectMgr::LoadItemTemplates walks the BagFamily bits and, for the
-- currency bit, demands a CurrencyTypes.dbc record for the item -
--
--     if (BAG_FAMILY_MASK_CURRENCY_TOKENS & mask)
--         if (!sCurrencyTypesStore.LookupEntry(itemTemplate.ItemId))
--             ... "remove bit"; itemTemplate.BagFamily &= ~mask;
--
-- (ObjectMgr.cpp:3820-3828). The store is the DBC FILE plus this world table:
-- DBCStores.cpp:295 loads "CurrencyTypes.dbc" with the db table
-- "currencytypes_dbc", and LoadDBC reads the file first and merges the table
-- over it (DBCStores.cpp:222+240, DBCDatabaseLoader.cpp:37-133), so the 43 file
-- records survive and these five are added. The order is safe: LoadDBCStores is
-- World.cpp:380 and LoadItemTemplates is World.cpp:525.
--
-- BitIndex is a bit number in the 64-bit PLAYER_FIELD_KNOWN_CURRENCIES, set as
-- `1 << (BitIndex - 1)` by Player::AddKnownCurrency (Player.cpp:14358-14362)
-- whenever a token is stored into a currency slot (PlayerStorage.cpp:2721-2722).
-- Measured on this realm's CurrencyTypes.dbc (43 records, already FL-patched):
-- bit indices 1..46 are in use except 4, 6 and 26, so 47-51 are the first five
-- free ones above the high-water mark and stay well inside the 64-bit field.
-- Row ids 361-365 follow the file's highest id, 360.
-- The id order MUST follow the ItemID order: DBCDatabaseLoader::Load sizes its
-- index table from the FIRST row of `ORDER BY ID DESC` but indexes by ItemID
-- (DBCDatabaseLoader.cpp:38-64), so a high id with a low ItemID would size the
-- table too small. 361->920105 .. 365->920109 rises with both.
--
-- CategoryID 46 is "Forgotten Dungeon" in the realm's already-patched
-- CurrencyCategory.dbc (11 records: 1 Miscellaneous, 2 Player vs. Player,
-- 3 Unused, 4 Classic, 21 Wrath of the Lich King, 22 Dungeon and Raid,
-- 23 Burning Crusade, 41 Test, 43 Forgotten Land, 46 Forgotten Dungeon,
-- 47 Abyssal Mastery). It is the header the five appear under in the client and
-- the server never reads it - the DBC format string "xnxi" (DBCfmt.h:44) skips
-- it - so reusing an existing category is what keeps this change to ONE
-- client-side DBC instead of two.
--
-- CLIENT HALF, STILL OWED (not done here, this task deploys nothing): the
-- Currency TAB is drawn by the client from the client's OWN CurrencyTypes.dbc,
-- so the five records below must also be added to the client patch before the
-- tab lists them. Today the three copies are byte-identical (md5
-- 3dbeeea4183ba2bc84a0e7df39fd2ac3, 43 records) -
--   C:\wowstuff\dcore\Data\dbc\CurrencyTypes.dbc          (server, read at boot)
--   C:\wowstuff\ForgottenLand2.0\output\DBFilesClient\    (client staging)
--   C:\wowstuff\ForgottenLand2.0\dist\dbc\                (client patch)
-- and the generator is the workspace's scripts\29_build_w19_currency_dbc.py.
-- Everything in THIS file works without that patch - no bag space, not
-- storable, talents still payable - only the tab stays empty until it ships.
-- ----------------------------------------------------------------------------

DELETE FROM `item_template` WHERE `entry` BETWEEN 920105 AND 920109;

INSERT INTO `item_template`
    (`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `Flags`,
     `BagFamily`, `BuyCount`, `BuyPrice`, `SellPrice`, `InventoryType`,
     `ItemLevel`, `RequiredLevel`, `maxcount`, `stackable`, `bonding`,
     `Material`, `sheath`, `Description`) VALUES
    -- display 55240 borrowed from 37700 Crystallized Air (pale white crystal)
    (920105, 10, 0, 'Faded Remnant',     55240, 1, 2048, 8192, 1, 0, 0, 0, 80, 0, 0, 1000, 1, 2, 0,
     'Currency of the Forgotten Depths. Spent in the Forgotten Talents tree.'),
    -- display 55242 borrowed from 37704 Crystallized Life (green crystal)
    (920106, 10, 0, 'Gleaming Remnant',  55242, 2, 2048, 8192, 1, 0, 0, 0, 80, 0, 0, 1000, 1, 2, 0,
     'Currency of the Forgotten Depths. Spent in the Forgotten Talents tree.'),
    -- display 60021 borrowed from 37705 Crystallized Water (blue crystal)
    (920107, 10, 0, 'Radiant Remnant',   60021, 3, 2048, 8192, 1, 0, 0, 0, 80, 0, 0, 1000, 1, 2, 0,
     'Currency of the Forgotten Depths. Spent in the Forgotten Talents tree.'),
    -- display 55243 borrowed from 37703 Crystallized Shadow (purple crystal)
    (920108, 10, 0, 'Sovereign Remnant', 55243, 4, 2048, 8192, 1, 0, 0, 0, 80, 0, 0, 1000, 1, 2, 0,
     'Currency of the Forgotten Depths. Spent in the Forgotten Talents tree.'),
    -- display 55241 borrowed from 37702 Crystallized Fire (orange crystal)
    (920109, 10, 0, 'Eternal Remnant',   55241, 5, 2048, 8192, 1, 0, 0, 0, 80, 0, 0, 1000, 1, 2, 0,
     'Currency of the Forgotten Depths. Spent in the Forgotten Talents tree.');

-- The CurrencyTypes records that make the BagFamily bit stick (see above).
-- Entry-exact on both sides, so nothing else in the table or the DBC is touched.
DELETE FROM `currencytypes_dbc` WHERE `ItemID` BETWEEN 920105 AND 920109;
DELETE FROM `currencytypes_dbc` WHERE `ID` BETWEEN 361 AND 365;

INSERT INTO `currencytypes_dbc` (`ID`, `ItemID`, `CategoryID`, `BitIndex`) VALUES
    (361, 920105, 46, 47),
    (362, 920106, 46, 48),
    (363, 920107, 46, 49),
    (364, 920108, 46, 50),
    (365, 920109, 46, 51);
