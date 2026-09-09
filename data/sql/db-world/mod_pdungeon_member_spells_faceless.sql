-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: combat kits for pack 8 "Ahn'kahet Deep" (world DB)
--
-- 25 rows for the 8 members of pack 8 (see
-- mod_pdungeon_packs_faceless.sql) - three per creature, and FOUR for the
-- BOSS 29309 Elder Nadox, which gained a second minDiff 1 row in Round C /
-- C6 the same way 25352 and 29620 did. Column order and cadence rule match
-- the shipped mod_pdungeon_member_spells.sql exactly:
--
--   MELEE + BOSS   position 1  cd 6000-8000 ms   minDiff 1
--                  position 2  cd 8000-10000 ms  minDiff 50
--                  position 3  cd 8000-12000 ms  minDiff 75
--   RANGE          position 1  the slot-0 filler, cd 0, minDiff 1
--                  position 2  cd 8000-10000 ms  minDiff 50
--                  position 3  cd 8000-12000 ms  minDiff 75
--   CC             ALWAYS cd 60000, and NEVER position 1
--
-- with that file's one Round C / C6 exception: a BOSS carries a SECOND row at
-- position 1 at cd 9000 ms, past the 6000-8000 band on purpose, so two base
-- abilities do not double a boss's output. Trash kits are unchanged.
--
-- `slot` is 0 for the filler and 1 for everything else; the "position" of a
-- non-filler row is carried by minDiff, not by slot, exactly as the two
-- shipped kit files do it. Row order is priority order - the loader queries
-- ORDER BY entry, slot, minDiff, spellId and the AI casts the first ready row
-- (PDv2PackMgr.cpp:229-232, PDv2CreatureAI.cpp:1436-1455).
--
-- Every id below was checked against Data\dbc\Spell.dbc (55100 records - the
-- file the worldserver actually loads, NOT acore_world.spell_dbc, which is an
-- unrelated custom table and holds none of these ids) on 2026-09-09, with the
-- same guard the two sibling packs use: identity (the spell is what the
-- comment claims, not merely that some spell exists at that id), a slot-0
-- filler range >= 25 yd, flat ManaCost 0 on every 0-mana creature, CC at
-- cd 60000 and never in slot 0, no summon/knockback/pull/jump effect, no heal,
-- no self-damage, no scriptless-dummy-only spell, no zero-radius area target,
-- no infinite-duration periodic aura, no rangeMin, and no (entry, spellId)
-- primary-key duplicate. Result on these 25 rows: `checked 25 rows, 0
-- problems`.
--
-- ----------------------------------------------------------------------------
-- SPELL IDENTITY - every id, read out of Spell.dbc rather than named from
-- memory. "own" = the creature's own stock rotation, taken from smart_scripts
-- (or, for the boss, from src/server/scripts/Northrend/AzjolNerub/ahnkahet/
-- boss_elder_nadox.cpp). Every single one is ManaCost 0 flat AND 0 percent.
--
--  spell  name                what Spell.dbc says it IS                     on
--  48640  Strike              WEAPON_PERCENT_DAMAGE, 5 yd, instant          30176
--  22644  Blood Leech         HEALTH_LEECH, caster-centred radius 10        30176
--  69900  Spirit Burst        SCHOOL_DAMAGE, caster-centred radius 15       30176
--  42746  Cleave              WEAPON_PERCENT_DAMAGE, 5 yd, instant   own    30277, 29309
--  56643  Triple Slash        PERIODIC_TRIGGER_SPELL -> 56645, self  own    30277
--  56646  Enrage              MOD_MELEE_HASTE 30, self, 6 s          own    30277
--  67879  Claw                WEAPON_PERCENT_DAMAGE, 5 yd, instant          31104
--  50729  Carnivorous Bite    SCHOOL_DAMAGE + BLEED DoT, 5 yd, 15 s         31104
--  60845  Shadow Nova         SCHOOL_DAMAGE, caster-centred radius 10       31104
--  61567  Fireball            SCHOOL_DAMAGE, 40 yd, INSTANT          own    30111
--   9034  Immolate            PERIODIC_DAMAGE + direct, 30 yd, 21 s         30111
--  61568  Flamestrike         DEST_AREA damage + PERSISTENT_AREA_AURA,
--                             30 yd, radius 5, 2.0 s cast, 8 s      own    30111
--  69211  Shadow Bolt         SCHOOL_DAMAGE, 30 yd, 2.2 s cast              30278
--  56632  Tangled Webs        MOD_ROOT, Mechanic 7 ROOT, caster-centred
--                             radius 25, 1.5 s cast, 8 s        pack-native 30278
--  17228  Shadow Bolt Volley  SCHOOL_DAMAGE, caster-centred radius 30       30278
--  60015  Shadow Bolt         SCHOOL_DAMAGE, 40 yd, 3.0 s cast              30179
--  61570  Lightning Shield    PROC_TRIGGER_DAMAGE 1600, self, 600 s  own    30179
--  47960  Shadowflame         PERIODIC_DAMAGE, 100 yd, 8 s                  30179
--  61562  Shadow Bolt         SCHOOL_DAMAGE, 40 yd, 1.5 s cast      own    30319
--  61563  Corruption          PERIODIC_DAMAGE, 30 yd, 12 s          own    30319
--  13338  Curse of Tongues    MOD_CASTING_SPEED_NOT_STACK -50, 30 yd,
--                             15 s                                  own    30319
--  56130  Brood Plague        PERIODIC_DAMAGE, 5 yd, 30 s           own    29309
--  26662  Berserk             MOD_DAMAGE_PERCENT_DONE 500 +
--                             MOD_MELEE_HASTE 150, self, 300 s      own    29309
--  54889  Shadow Shock        SCHOOL_DAMAGE, caster-centred radius 25       29309
--
-- 26662 is SPELL_ENRAGE in boss_elder_nadox.cpp:38; its Spell.dbc
-- Name_Lang_enUS is "Berserk". The comment column below carries the DBC name,
-- because that is the name the guard verifies against.
--
-- 31104 Ahn'kahar Watcher casts 42746/56643/56646 in its own smart_scripts
-- rows - the SAME three as 30277 Ahn'kahar Slasher. It is given the generic
-- claw/bite/nova kit instead ON PURPOSE: two members of one pack sharing one
-- rotation makes the pack read as two copies of a creature. 30277 keeps the
-- native rotation, 31104 gets the deep-tunnel sentinel's.
--
-- ----------------------------------------------------------------------------
-- RANGE - what "reaches" means here
--
-- UpdateCasterCombat plants a role-1 mob wherever it happens to be INSIDE
-- ProceduralDungeon.V2.CastRangeYd - me->IsWithinCombatRange(victim,
-- castRange), not "at exactly 25 yd" (PDv2CreatureAI.cpp:1475-1500). So a
-- role-1 row that cannot reach 25 yd silently fails from the far edge of the
-- hold, and the cooldown is spent on the ATTEMPT (PDv2CreatureAI.cpp:
-- 1450-1453), which means a spell that refuses costs the mob a whole rotation
-- slot rather than being retried. Every row on 30278, 30179 and 30319
-- therefore reaches >= 25 yd:
--
--   69211 30 yd   60015 40 yd   61562 40 yd   47960 100 yd   61563 30 yd
--   13338 30 yd   61570 30 yd   56632 radius 25   17228 radius 30
--
-- "range 0" means caster-centred, not "no range": 17228 Shadow Bolt Volley,
-- 54889 Shadow Shock, 60845 Shadow Nova and 69900 Spirit Burst are all
-- TARGET_SRC_CASTER + TARGET_UNIT_SRC_AREA_ENEMY, and it is the RADIUS that
-- decides whether they reach - 30, 25, 10 and 15 respectively. That is why
-- 60845 and 69900 sit only on the two melee nerubians and 17228 sits on the
-- caster.
--
-- ----------------------------------------------------------------------------
-- POWER COST
--
-- All 24 distinct spell ids used below are ManaCost 0 flat AND 0
-- ManaCostPercentage - free forever, not merely affordable. That matters
-- twice. First, the three unit_class 1 members (30176, 30277, 31104) have
-- basemana 0 at level 80, and SpellInfo::CalcPowerCost adds a FLAT cost
-- unconditionally, so Spell::CheckPower would fail with NO_POWER on any flat
-- cost at all. Second, Creature::Regenerate gives an in-combat creature only
-- Spirit/5 + 17 mana per interval, so even on the unit_class 2 casters a
-- percentage-cost filler empties the pool in six to nine casts and then
-- silently fails for the rest of the fight - the trap the shipped file's
-- header argues at length about 47809/47857/42842.
--
-- The three fillers are 69211 Shadow Bolt (30 yd), 60015 Shadow Bolt (40 yd)
-- and 61562 Shadow Bolt (40 yd), one per role-1 member, each identical to
-- that member's casterSpellId in mod_pdungeon_packs_faceless.sql, and each
-- comfortably past the 25 yd hold.
--
-- ----------------------------------------------------------------------------
-- CC CLASSIFICATION (aura/mechanic read out of Spell.dbc, not guessed)
--
--  spell  name           why it is CC                              placed
--  56632  Tangled Webs   Mechanic 7 ROOT, aura MOD_ROOT, 8 s       30278 @50
--
-- ONE CC in the whole pack, at cooldownMs 60000 and minDiff 50, never in
-- slot 0 - the rule the shipped file's own CC table enforces. No other row
-- here carries a CC mechanic or aura; MECHANIC_BLEED on 50729 is not CC, and
-- neither is 13338's casting-speed debuff.
--
-- The CC is a ROOT and that is a deliberate choice over the fears this
-- creature family also offers (34322 Psychic Scream was on the shortlist).
-- Map 760 has no terrain outside the generated platform, so a fear can walk a
-- player off the edge; a root cannot. It is the same reasoning that keeps
-- 56640/59106 Web Grab (SPELL_EFFECT_PULL_TOWARDS) out of this file, and the
-- same reasoning the shipped file records for 62129's knock-back.
--
-- ----------------------------------------------------------------------------
-- EXCLUDED, AND WHY - every one of these fails SILENTLY in game
--
--  56119 / 56120  Summon Swarmers / Summon Swarm Guardian (Nadox's own)
--                 SPELL_EFFECT_SUMMON. A summoned add is not a pack member,
--                 never enters _roomAlive, and the room counter is the whole
--                 clear condition (PDv2InstanceScript.cpp:707-709).
--  59465          Brood Rage (Nadox's own, heroic)
--                 TARGET_UNIT_NEARBY_ENTRY - it buffs a nearby swarmer. With
--                 the summons excluded there is no ally to buff, so the cast
--                 finds nothing.
--  56354          Sprint (npc_ahnkahar_nerubian's own)
--                 SPELL_AURA_MOD_INCREASE_SPEED. A movement-speed buff fights
--                 the module's own grid waypoint motion.
--  56281          Swarm (Nadox's own)
--                 a fine self buff, but SpellDuration -1: it never falls off.
--                 The finite 26662 Berserk (300 s) takes the slot instead.
--  56698 / 59102  Shadow Blast (30278's own, and its ONLY real nuke)
--                 6000 ms cast time and 900 flat ManaCost. ScriptedAI::
--                 DoMeleeAttackIfReady returns early on UNIT_STATE_CASTING,
--                 so a 6 s cast is most of a rotation spent not swinging.
--  56702 / 59103  Shadow Sickle (30278's own)
--                 range 0 with no area target and 250 flat ManaCost - it
--                 cannot reach from a 25 yd hold. 30278's whole native
--                 rotation is these four ids, which is why its kit is built
--                 from the generic pool plus the pack-native 56632.
--  56711 / 56713  Image Channel (30111's, 30179's and 30319's own)
--                 SPELL_AURA_DUMMY with no consumer once Jedoga's script is
--                 unbound - the cast would visibly do nothing. The guard's
--                 dummy rule does not catch this one (the EFFECT is
--                 APPLY_AURA, not DUMMY); it is a manual exclusion.
--  11986          Healing Wave (30179's own)
--                 SPELL_EFFECT_HEAL. No self-heal loops in a pack kit -
--                 ModifyHealReceived is deliberately not hooked
--                 (PDv2Scaling.cpp:317-321).
--  17290 / 56898  Fireball / Corruption (30111's and 30319's own)
--                 usable, but 90 and 80 FLAT ManaCost against the free
--                 61567 / 61563 that do the same job. Kept as documented
--                 alternatives, not shipped.
--  60833 / 60848  Shadow Crash (Herald Volazj's signature)
--                 rangeMin 10 - it refuses whenever the player is closer than
--                 10 yd. Excluded with its caster (see the packs file).
--
-- ----------------------------------------------------------------------------
-- ONE OBSERVED OVERLAP, RECORDED RATHER THAN LEFT TO BE FOUND
--
-- 30179 Twilight Apostle spawns with creature_template_addon.auras 12550
-- "Lightning Shield" (PROC_TRIGGER_DAMAGE, 2 damage) and its t50 row casts
-- 61570 "Lightning Shield" (PROC_TRIGGER_DAMAGE, 1600 damage). Two different
-- spell ids with the same name; the cast one is by far the stronger and the
-- pair is harmless either way. It is also the only member of this pack with a
-- shield visual, which is the point of giving it that row.
--
-- ----------------------------------------------------------------------------
-- ONE CAST-TIME COST, ACCEPTED ON PURPOSE
--
-- 30111 Twilight Worshipper is role 0 (melee) and its t75 row 61568
-- Flamestrike has a 2.0 s cast, so that mob stops swinging for two seconds
-- once every 12 s at difficulty 75+. It is kept because it is the zealot's
-- own signature and because a robed cultist that occasionally plants a fire
-- patch is what makes it read as a cultist rather than another melee body.
-- Its two lower rows (61567 Fireball, 9034 Immolate) are both instant.
--
-- ----------------------------------------------------------------------------
-- The CREATE TABLE IF NOT EXISTS block below is a DELIBERATE deviation from
-- the mod_pdungeon_member_spells_undead_demon.sql precedent. The AC updater
-- applies every .sql under the tree in FILENAME order (UpdateFetcher.cpp:
-- 64-96), and this filename sorts BEFORE mod_pdungeon_packs.sql - the only
-- file that carries the table definition today. On this box the table already
-- exists and the block is free; on a FRESH world database it is what keeps
-- this file from failing to apply.
--
-- THIS FILE SHIPS ZERO creature_template AND ZERO spell_dbc ROWS.
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_member_spells` (
  `entry` INT UNSIGNED NOT NULL,
  `spellId` INT UNSIGNED NOT NULL,
  `slot` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `cooldownMs` INT UNSIGNED NOT NULL DEFAULT 8000,
  `minDiff` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`entry`, `spellId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Idempotent re-apply: delete this file's own entries, then insert. Never
-- DROP - an operator who gave a creature of their own a kit keeps it. The
-- delete is an EXPLICIT entry list, not a BETWEEN: the entries run from 29309
-- to 31104, and a range delete over that span would wipe an operator's rows
-- for nearly two thousand unrelated templates.
DELETE FROM `pdungeon_member_spells` WHERE `entry` IN
 (29309,30111,30176,30179,30277,30278,30319,31104);

INSERT INTO `pdungeon_member_spells`
  (`entry`, `spellId`, `slot`, `cooldownMs`, `minDiff`, `enabled`) VALUES
  -- ==========================================================================
  -- PACK 8 "Ahn'kahet Deep" - MELEE (role 0)
  -- ==========================================================================
  -- 30176 Ahn'kahar Guardian  npc_ahnkahar_nerubian  (uc1, type 6, lootid 0)
  (30176, 48640, 1,  6000,  1, 1),  -- Strike            weapon damage, 5 yd        t0
  (30176, 22644, 1, 10000, 50, 1),  -- Blood Leech       10 yd area leech           t50
  (30176, 69900, 1, 12000, 75, 1),  -- Spirit Burst      15 yd area damage          t75
  -- 30277 Ahn'kahar Slasher  (uc1, type 6) - its whole native rotation, intact
  (30277, 42746, 1,  7000,  1, 1),  -- Cleave            weapon damage, 5 yd   own  t0
  (30277, 56643, 1, 10000, 50, 1),  -- Triple Slash      self, triggers 56645  own  t50
  (30277, 56646, 1, 12000, 75, 1),  -- Enrage            self melee haste, 6 s own  t75
  -- 31104 Ahn'kahar Watcher  (uc1, type 6, addon aura 18950 = detection only)
  (31104, 67879, 1,  6000,  1, 1),  -- Claw              weapon damage, 5 yd        t0
  (31104, 50729, 1, 10000, 50, 1),  -- Carnivorous Bite  damage + bleed, 15 s       t50
  (31104, 60845, 1, 12000, 75, 1),  -- Shadow Nova       10 yd area damage          t75
  -- 30111 Twilight Worshipper  (uc2, type 7, role 0 by demotion - see the packs file)
  (30111, 61567, 1,  7000,  1, 1),  -- Fireball          40 yd single, instant own  t0
  (30111,  9034, 1, 10000, 50, 1),  -- Immolate          30 yd DoT, 21 s            t50
  (30111, 61568, 1, 12000, 75, 1),  -- Flamestrike       30 yd AoE, 2.0 s cast own  t75
  -- ==========================================================================
  -- PACK 8 "Ahn'kahet Deep" - RANGE (role 1) - filler + two cooldown spells
  -- ==========================================================================
  -- 30278 Ahn'kahar Spell Flinger  (uc2, type 6, mana 19970)
  (30278, 69211, 0,     0,  1, 1),  -- Shadow Bolt       30 yd single, 0 mana       t0 FILLER
  (30278, 56632, 1, 60000, 50, 1),  -- Tangled Webs      25 yd  CC (MOD_ROOT)       t50
  (30278, 17228, 1, 12000, 75, 1),  -- Shadow Bolt Volley 30 yd area damage         t75
  -- 30179 Twilight Apostle  (uc2, type 7, mana 31952, addon aura 12550)
  (30179, 60015, 0,     0,  1, 1),  -- Shadow Bolt       40 yd single, 0 mana       t0 FILLER
  (30179, 61570, 1, 10000, 50, 1),  -- Lightning Shield  self damage proc      own  t50
  (30179, 47960, 1, 12000, 75, 1),  -- Shadowflame       100 yd single DoT          t75
  -- 30319 Twilight Darkcaster  (uc2, type 7, mana 15976) - free, on-theme, all its own
  (30319, 61562, 0,     0,  1, 1),  -- Shadow Bolt       40 yd single, 0 mana  own  t0 FILLER
  (30319, 61563, 1, 10000, 50, 1),  -- Corruption        30 yd DoT, 12 s       own  t50
  (30319, 13338, 1, 12000, 75, 1),  -- Curse of Tongues  30 yd cast speed -50% own  t75
  -- ==========================================================================
  -- PACK 8 "Ahn'kahet Deep" - BOSS (role 2)
  -- ==========================================================================
  -- 29309 Elder Nadox  boss_elder_nadox  (uc2, rank 1, 214200 hp)
  (29309, 42746, 1,  7000,  1, 1),  -- Cleave            weapon damage, 5 yd        t0
  -- Round C / C6: a second base ability per boss (operator, 2026-09-08: "2 Basis, dann je eine auf 50 und 75")
  (29309, 56130, 1,  9000,  1, 1),  -- Brood Plague      5 yd DoT, 30 s        own  t0 #2
  (29309, 26662, 1, 10000, 50, 1),  -- Berserk           self dmg + haste buff own  t50
  (29309, 54889, 1, 12000, 75, 1);  -- Shadow Shock      25 yd area damage          t75
