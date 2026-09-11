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
-- A CurrencyTypes RECORD PER ITEM IS NOT OPTIONAL. Without one the BagFamily
-- bit is silently REMOVED at startup and this file does nothing at all:
-- ObjectMgr::LoadItemTemplates walks the BagFamily bits and, for the
-- currency bit, demands a CurrencyTypes.dbc record for the item -
--
--     if (BAG_FAMILY_MASK_CURRENCY_TOKENS & mask)
--         if (!sCurrencyTypesStore.LookupEntry(itemTemplate.ItemId))
--             ... "remove bit"; itemTemplate.BagFamily &= ~mask;
--
-- (ObjectMgr.cpp:3820-3828). The store is the DBC FILE plus this world table:
-- DBCStores.cpp:295 loads "CurrencyTypes.dbc" with the db table
-- "currencytypes_dbc", and LoadDBC reads the file first and merges the table
-- over it (DBCStores.cpp:222+240, DBCDatabaseLoader.cpp:37-133). The order is
-- safe: LoadDBCStores is World.cpp:380 and LoadItemTemplates is World.cpp:525.
--
-- SHIPPED STATE (2026-09-11): THE FILE ALREADY CARRIES THESE FIVE. The
-- workspace generator C:\wowstuff\ForgottenLand2.0\scripts\
-- 29_build_w19_currency_dbc.py was re-run, and the rebuilt CurrencyTypes.dbc
-- (789 bytes, WDBC, 48 records of 16 bytes = ID / ItemID / CategoryID /
-- BitIndex, md5 55220aad4bab60a2dc60befaa64728b7) is byte-identical in all
-- three places -
--   C:\wowstuff\dcore\Data\dbc\CurrencyTypes.dbc          (server, read at boot)
--   C:\wowstuff\ForgottenLand2.0\output\DBFilesClient\    (client staging)
--   C:\wowstuff\ForgottenLand2.0\dist\dbc\                (client patch source)
-- Its rows 361-365 are THE SAME FIVE RECORDS as the INSERT below, field for
-- field, and patch-9.MPQ was rebuilt from the staging copy and deployed to
-- C:\wowstuff\FL2-Client\Data\, so the client draws the Currency tab entries
-- from its own patched copy. Nothing about the DBC half is owed any more.
--
-- WHICH HALF ACTUALLY SATISFIES THE SERVER: the FILE record does. The rows
-- below are the checked-in, fresh-clone copy of the same five, and they are NOT
-- an equivalent substitute on this core version - see the index-table
-- invariant below.
--
-- BitIndex is a bit number in the 64-bit PLAYER_FIELD_KNOWN_CURRENCIES, set as
-- `1 << (BitIndex - 1)` by Player::AddKnownCurrency (Player.cpp:14358-14362)
-- whenever a token is stored into a currency slot (PlayerStorage.cpp:2721-2722).
-- Measured over the 48 records: bit indices 1..51 are in use except 4, 6 and
-- 26, i.e. 47-51 are these five and they stay well inside the 64-bit field.
-- Row ids 361-365 follow 360, the highest id the file had before them.
--
-- THE INDEX-TABLE INVARIANT (measured, and it is not "ids must follow ItemIDs"):
-- the two loaders key the store DIFFERENTLY.
--   * The FILE loader indexes by the format's index field, which for
--     CurrencyTypesfmt "xnxi" (DBCfmt.h:44) is field 1 = ItemID. It sizes the
--     index table to max(ItemID) + 1 over ALL file records and writes each
--     record at indexTable[ItemID] (DBCFileLoader.cpp:199-215, :231). That is
--     what makes sCurrencyTypesStore.LookupEntry(itemTemplate.ItemId) succeed.
--     Today the sizing record is file row ID 354 / ItemID 920930 (Dust of
--     Fallen Souls), so the table is 920931 entries wide.
--   * The DB merge keys on the table's FIRST COLUMN, `ID` - not on ItemID.
--     DBCDatabaseLoader::_sqlIndexPos is a hard-wired 0: the constructor
--     computes the format's index position into a local and drops it
--     (DBCDatabaseLoader.cpp:24-35), so Load() takes fields[0] both for the
--     sizing (max(fileRecords, highest `ID` + 1), from the first row of
--     `ORDER BY ID DESC`) and for the write, indexTable[ID]
--     (:56, :74, :122-125).
-- Consequences, both of which hold today:
--   a) a row that exists ONLY here is reachable as LookupEntry(ID) and never as
--      LookupEntry(ItemID), so it does NOT satisfy the ObjectMgr check above.
--      A NEW currency item therefore needs a FILE record; adding it here alone
--      would leave the BagFamily bit stripped at startup.
--   b) the merge is only harmless while no file record carries an ItemID equal
--      to one of these row IDs, because such a record would be silently
--      overwritten by the merged row. Measured: the file's lowest ItemID is
--      8000 and the row IDs here are 361-365, so nothing collides.
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
-- CLIENT HALF, DONE (see SHIPPED STATE above): the Currency TAB is drawn by the
-- client from the client's OWN CurrencyTypes.dbc, so the five had to reach the
-- client patch before the tab could list them - that is what the patch-9
-- rebuild delivers. Everything ELSE in this file never depended on it: no bag
-- space, not storable and talents payable all follow from the server-side
-- template plus the server's own DBC copy.
--
-- A fresh host still needs the DBC file deployed next to the SQL; that is a
-- client-patch + server-DBC job in its own right (scripts 35/36/39/40 style)
-- and it carries its own migration-ledger entry.
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

-- The same five CurrencyTypes records the shipped CurrencyTypes.dbc carries as
-- its rows 361-365 (see above) - the checked-in copy of them, and the record of
-- which ids, categories and bit indices this module claimed. The BagFamily bit
-- is made to stick by the FILE record, not by this merge, because the merge
-- keys on `ID` rather than `ItemID` (the invariant above).
-- Entry-exact on both sides, so nothing else in the table is touched.
DELETE FROM `currencytypes_dbc` WHERE `ItemID` BETWEEN 920105 AND 920109;
DELETE FROM `currencytypes_dbc` WHERE `ID` BETWEEN 361 AND 365;

INSERT INTO `currencytypes_dbc` (`ID`, `ItemID`, `CategoryID`, `BitIndex`) VALUES
    (361, 920105, 46, 47),
    (362, 920106, 46, 48),
    (363, 920107, 46, 49),
    (364, 920108, 46, 50),
    (365, 920109, 46, 51);
