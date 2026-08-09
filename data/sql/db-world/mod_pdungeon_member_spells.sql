-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: per-creature combat kits (world database)
--
-- One row = one spell a PDv2 mob may cast, keyed by creature_template entry.
--
--   slot 0  the RANGE mob's FILLER. Cast back to back while the mob holds at
--           V2.CastRangeYd; `cooldownMs` 0 means "no artificial gap", which is
--           the operator's design ("durchspammen"). The AI still honours a
--           non-zero value here, so a filler can be paced from data alone
--           without a rebuild.
--   slot 1  a COOLDOWN spell. Range mobs weave them between fillers; melee
--           mobs and bosses cast them from melee between auto-attacks.
--
-- `minDiff` gates a row on the run's 1..100 difficulty dial: a row is in the
-- mob's kit for that fight when minDiff <= difficulty. The filter happens once
-- at pull time, so a row costs nothing per tick.
--
-- THIS FILE SHIPS ZERO creature_template AND ZERO spell_dbc ROWS. Every spell
-- id below is a stock 3.3.5a Spell.dbc entry, and every creature entry is
-- shared with fl-underground-dungeon's live map-741 dungeon - which is why
-- behaviour is described HERE, in PDv2's own table, instead of in rows that
-- dungeon also reads.
--
-- ----------------------------------------------------------------------------
-- WHERE THE KITS COME FROM
--
-- fl-underground-dungeon/src/UndergroundData.cpp, TrashKits() and
-- BossDatasets(), READ-ONLY. Those tables are keyed by the creature's
-- ScriptName, so every entry below was resolved through the WORLD DB
-- (SELECT entry, name, ScriptName FROM creature_template WHERE entry BETWEEN
-- 84263 AND 84290, measured 2026-08-09) and never through that file's own
-- entry->name comments: GetMeleeEntries()/GetCasterEntries() there name 19 of
-- 25 entries wrongly (e.g. it calls 84264 "Twisted Abomination"; the DB says
-- Crypt Howler / npc_crypt_howler).
--
-- Every spell id was then checked against Data\dbc\Spell.dbc (+ SpellRange,
-- SpellRadius, SpellCastTimes, SpellDuration): it has to be a real spell, to
-- do what the kit label claims, to REACH from where the mob will cast it, and
-- to be affordable with the power the creature actually has.
--
-- ----------------------------------------------------------------------------
-- CADENCE (operator law, 2026-08-09)
--
--   MELEE + BOSS   position 1  cd 6000-8000 ms   minDiff 1
--                  position 2  cd 8000-10000 ms  minDiff 50
--                  position 3  cd 8000-12000 ms  minDiff 75
--   RANGE          position 1  the slot-0 filler, cd 0, minDiff 1
--                  position 2  cd 8000-10000 ms  minDiff 50
--                  position 3  cd 8000-12000 ms  minDiff 75
--   CC             ALWAYS cd 60000, and NEVER position 1
--
-- Kit DiffTier 0/50/75 maps onto positions 1/2/3; a kit with no slot at a tier
-- leaves that position empty. Inside a band the heavier ability takes the
-- longer end: 6000 a light weapon strike, 7000 a normal single-target ability,
-- 8000+ an AoE, a DoT, a channel or anything with a cast time.
--
-- ----------------------------------------------------------------------------
-- CC CLASSIFICATION (aura effects read out of Spell.dbc, not guessed)
--
--  spell  name                 why it is CC                       placed
--  6215   Fear                 aura 7 MOD_FEAR, Mechanic 5 FEAR    84263 @75
--  47860  Death Coil R6        aura 7 MOD_FEAR, EffMechanic 24     84287 @50
--                              HORROR
--  42917  Frost Nova R6        aura 26 MOD_ROOT, EffMechanic 7     84285 @50
--                              ROOT                                84268 @75
--  5918   Shadowstalker Stab   aura 12 MOD_STUN, Mechanic 12 STUN  84271 @50
--
-- All four carry cooldownMs 60000 and sit at minDiff 50 or 75. 42917 is the
-- one case where a CC is its kit's TIER-0 slot; the rule forbids a CC at
-- minDiff 1, so on 84268 it moves up to the first free position (75) and a
-- damage slot is promoted into position 1 instead.
--
-- Not CC, and deliberately not treated as such: 47106 Soul Flay's snare (aura
-- 33 MOD_DECREASE_SPEED - a snare is not in the operator's fear/stun/root/
-- silence/disorient list; it is excluded below for an unrelated reason), and
-- 62129 Wail of Souls' Interrupt/Knock Back effects (neither is an aura).
--
-- ----------------------------------------------------------------------------
-- POWER COST - THE unit_class 1 MOBS
--
-- 11 of these 28 templates are unit_class 1 (warrior): 84264 84267 84269 84271
-- 84273 84275 84277 84279 84284 84286 84287. creature_classlevelstats has
-- basemana 0 for class 1 at every level (measured on this box: level 80 -> 0),
-- so Creature::SelectLevel gives them CreateMana 0 and Power(MANA) 0.
--
-- What that means for a cast, from the engine and not from a rule of thumb:
--   * SpellInfo::CalcPowerCost adds ManaCostPercentage as a percentage OF
--     GetCreateMana() - which is 0 - so a percentage-only cost resolves to 0.
--   * a FLAT ManaCost is added unconditionally, and Spell::CheckPower then
--     compares it against Power(MANA) = 0 and fails with NO_POWER.
-- So on these mobs a flat cost is fatal and a percentage cost is free. Every
-- flat-cost spell in their kits is excluded below (606 Mind Rot 240, 1010
-- Curse of Idiocy 110); the percentage-cost ones are kept and listed here so
-- the assumption is auditable:
--
--  entry  spell  name                ManaCost  ManaCost%  effective on uc1
--  84267  47864  Curse of Agony R9      0         10          0
--  84267  47809  Shadow Bolt R13        0         17          0
--  84269  47809  Shadow Bolt R13        0         17          0
--  84273  47864  Curse of Agony R9      0         10          0
--  84275  50511  Curse of Weakness R9   0         10          0
--  84287  47809  Shadow Bolt R13        0         17          0
--  84287  47860  Death Coil R6          0         23          0
-- Every other uc1 row below is 0/0 in Spell.dbc and needs no argument at all.
--
-- 84287 Awakened Bones is the mob this matters most for: it is unit_class 1
-- AND a RANGE mob, and its kit holds exactly two ranged damage spells (47809,
-- 47860), both percentage-cost. There is no 0/0 ranged damage spell anywhere
-- in that kit, so a filler either costs percentage mana or does not exist.
--
-- ----------------------------------------------------------------------------
-- EXCLUDED KIT SPELLS, and why (every one of them, per creature)
--
--  spell  name                excluded from   reason
--  28863  Void Zone           84272 t0        SPELL_EFFECT_SUMMON - summons
--                                             are out of scope this slice
--  47106  Soul Flay           84268 t0        EffectImplicitTargetA 1
--                             84272 t75       TARGET_UNIT_CASTER on its only
--                             84283 t75       effect: the SNARE lands on the
--                                             CASTER, so the mob slows itself
--  37454  Bite                84273 t0        Karazhan chess spell: its
--                                             conditions rows (SourceType 13)
--                                             restrict the cone to chess-piece
--                                             creature entries, so it can
--                                             never touch a player
--  52473  Bite R10            84266 t0        costs 25 FOCUS; only hunter pets
--                             84278 t0        have focus, every creature here
--                                             has 0 -> NO_POWER
--  26476  Digestive Acid      84266 t50       SpellDuration index 21 = -1: a
--                             84278 t50       PERIODIC_DAMAGE aura that never
--                                             expires. Nothing removes an aura
--                                             a dead creature applied
--                                             (RemoveAllAurasOnDeath only
--                                             clears the DYING unit), so the
--                                             debuff would outlive the dungeon
--  67866  Trample             84286 t50       its only effect is SPELL_EFFECT_
--                                             DUMMY with no script anywhere -
--                                             the cast does nothing at all
--  67028  Slam                84282 t0        EffectImplicitTargetA 76
--                                             TARGET_DEST_CHANNEL_TARGET on
--                                             the damage effect: it needs a
--                                             channel that a standalone cast
--                                             never has
--  69055  Bone Slice          84280 t0        EffectRadiusIndex 0 on a
--                                             TARGET_UNIT_DEST_AREA_ENEMY
--                                             effect - radius 0 selects
--                                             nothing; it is Marrowgar's
--                                             scripted cleave
--  34111  Judgement of        84289 t75       EffectImplicitTargetA 1
--         Darkness                            TARGET_UNIT_CASTER on a
--                                             SCHOOL_DAMAGE effect: the boss
--                                             would nuke ITSELF
--  606    Mind Rot            84269 t50       flat ManaCost 240 on a
--                             84271 t75       unit_class 1 mob (0 mana)
--  1010   Curse of Idiocy     84267 t75       flat ManaCost 110 on a
--                             84271 t0        unit_class 1 mob (0 mana).
--                                             KEPT on 84274 and 84282, which
--                                             are unit_class 8
--  29477  Banshee Wail        84276 t50       cone, EffectRadius 10 yd, on a
--                                             mob that holds at 25 yd - it
--                                             would sweep empty air
--  64666  Savage Pounce       84264 t0        SPELL_EFFECT_JUMP hands the
--                             84277 t0        creature to the core's jump
--                                             movement generator, overriding
--                                             the module's grid waypoint run
--                                             on a map whose straight lines
--                                             cross the void
--  38162  Unyielding Knights  84288 p2        SPELL_EFFECT_SUMMON
--  0      (summon action)     84290 p2        the crypt lord's add-summon
--                                             sentinel, not a cast
--  31884  Avenging Wrath      84288 p2        phase-two TRANSITION casts.
--  71110  Aura of Darkness    84289 p2        PDv2 has no phase machinery, so
--  71586  Hardened Skin       84290 p2        these one-offs have no trigger
--  48806  Hammer of Wrath     84288 p2        phase-two ROTATION slot; the
--                                             boss's three positions are
--                                             already filled from its own
--                                             tier 0/50/75 rotation
--  20791  Shadow Bolt         84282           the generic FALLBACK nuke this
--                                             mob carried while it was a
--                                             caster. It never belonged to its
--                                             kit; now that 84282 is melee its
--                                             own kit reaches, so the stopgap
--                                             is dropped
--
-- SECOND TIER-0 SLOTS. Every trash kit has TWO DiffTier-0 slots but only ONE
-- position 1, so exactly one tier-0 spell per melee mob is left out. The pick
-- prefers a DAMAGE ability, and among damage abilities an INSTANT one, because
-- ScriptedAI::DoMeleeAttackIfReady returns early on UNIT_STATE_CASTING - a 3 s
-- cast on a 7 s cadence would cost a melee mob ~40% of its swings:
--  84265 drops 47809 Shadow Bolt (3.0 s cast)   -> keeps 47867 Curse of Doom
--  84267 drops 47857 Drain Life (5 s channel)   -> keeps 47864 Curse of Agony
--  84269 drops 47857 Drain Life (5 s channel)   -> keeps 47809 Shadow Bolt
--        (the whole kit is slow; the 3 s cast is the lesser of the two)
--  84270 drops 47864 Curse of Agony             -> keeps 47867 Curse of Doom
--        (both instant 30 yd DoTs; Curse of Doom is the heavier hit)
--  84274 drops 59965 Eye Beam (1.5 s cast)      -> keeps 54889 Shadow Shock
--  84275 drops 59269 Carnivorous Bite           -> keeps 42397 Rend Flesh
--  84279 drops 7484 Howling Rage (buff, 1.5 s)  -> keeps 59269 Carn. Bite
--  84283 drops 54052 Shadow Bite (5 yd single)  -> keeps 59126 Shadow Breath
--  84284 drops 43353 Infected Bite (DoT)        -> keeps 59992 Cleave
--  84286 drops 59270 Acid Spit (1.0 s cast)     -> keeps 59116 Poison Cloud
-- On 84264/84273/84277/84278/84280/84282 the second tier-0 slot is one of the
-- hard exclusions above, so nothing extra is dropped there.
--
-- ----------------------------------------------------------------------------
-- TWO THINGS THE OPERATOR SHOULD KNOW BEFORE THE FIRST RUN
--
-- 1) 62129 Wail of Souls is NOT 84281's filler, and must never become one.
--    Two independent facts agree on that. Its own kit cadence is 60000 ms
--    (UndergroundData.cpp, npc_ashen_wailer slot 1: the ability was authored
--    as a minute cooldown, not a rotation spell). And its second effect is
--    KNOCK_BACK on TARGET_SRC_CASTER - an area punt around the caster, plus
--    an interrupt - so spamming it on a map whose floor ends at the platform
--    edge is a mob launching every player near it into the void once per
--    cast, forever. It keeps its signature ability at the cadence its own kit
--    asked for, in the 60 s CC band its knock-back + interrupt earns.
--    The filler falls back to the generic Shadow Bolt because the Wailer's
--    only other ranged option, 69900 Spirit Burst, has range 0 - a
--    self-centred AoE that cannot land from V2.CastRangeYd at all. (Measured
--    in Spell.dbc 2026-08-09: 62129 range 100, effect2 98 KNOCK_BACK target
--    22; 69900 range 0 target 22; 32315 Soul Strike - the Banshee's other
--    tier-0 - range 5, which is why 84276 keeps Drain Life despite the
--    channel.)
-- 2) 42940 Blizzard (84285 @75) channels for 8 s. On a 12 s cadence an
--    Underground Spectrum spends two thirds of a difficulty-75 fight in that
--    channel and its filler stays silent meanwhile. Same one-line remedy.
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

-- Idempotent re-apply: delete this file's own entry range, then insert. Never
-- DROP - an operator who gave a creature of their own a kit keeps it.
DELETE FROM `pdungeon_member_spells` WHERE `entry` BETWEEN 84263 AND 84290;

INSERT INTO `pdungeon_member_spells`
  (`entry`, `spellId`, `slot`, `cooldownMs`, `minDiff`, `enabled`) VALUES
  -- ==========================================================================
  -- RANGE (role 1) - filler + up to two cooldown spells
  -- ==========================================================================
  -- 84263 Underground Occultist  npc_underground_occultist  (uc8)
  (84263, 47809, 0,     0,  1, 1),  -- Shadow Bolt R13   30 yd single, 3.0s cast   t0 FILLER
  (84263, 47960, 1, 10000, 50, 1),  -- Shadowflame R1   100 yd single DoT           t50
  (84263,  6215, 1, 60000, 75, 1),  -- Fear R3           20 yd  CC (MOD_FEAR)       t75
  -- 84276 Soul-Leech Banshee  npc_soul_leech_banshee  (uc8)
  (84276, 47857, 0,     0,  1, 1),  -- Drain Life R9     30 yd single leech         t0 FILLER
  (84276, 71264, 1, 12000, 75, 1),  -- Swarming Shadows  any range, single DoT      t75
  -- 84281 Ashen Wailer  npc_ashen_wailer  (uc8)
  (84281, 47809, 0,     0,  1, 1),  -- Shadow Bolt R13   30 yd single (see note 1)  FILLER
  (84281, 62129, 1, 60000, 50, 1),  -- Wail of Souls    100 yd + area knock-back    t0 -> CC band
  (84281, 71264, 1, 12000, 75, 1),  -- Swarming Shadows  any range, single DoT      t75
  -- 84285 Underground Spectrum  npc_underground_spectrum  (uc8)
  (84285, 42842, 0,     0,  1, 1),  -- Frostbolt R16     30 yd single, 3.0s cast    t0 FILLER
  (84285, 42917, 1, 60000, 50, 1),  -- Frost Nova R6     10 yd  CC (MOD_ROOT)       t50
  (84285, 42940, 1, 12000, 75, 1),  -- Blizzard R9       30 yd AoE, 8s channel      t75
  -- 84287 Awakened Bones  npc_awakened_bones  (uc1 - see the power-cost block)
  (84287, 47809, 0,     0,  1, 1),  -- Shadow Bolt R13   30 yd single   PROMOTED from t75
  (84287, 47860, 1, 60000, 50, 1),  -- Death Coil R6     30 yd  CC (MOD_FEAR)       t50
  -- ==========================================================================
  -- MELEE (role 0) - no filler; cooldown spells cast from melee
  -- ==========================================================================
  -- 84264 Crypt Howler  npc_crypt_howler  (uc1)
  (84264, 59992, 1,  7000,  1, 1),  -- Cleave            weapon damage              t0
  (84264,  8599, 1, 10000, 50, 1),  -- Enrage            self damage buff           t50
  (84264, 22644, 1, 12000, 75, 1),  -- Blood Leech       10 yd area leech           t75
  -- 84265 Shadowbone Stalker  npc_shadowbone_stalker  (uc8)
  (84265, 47867, 1,  8000,  1, 1),  -- Curse of Doom     30 yd DoT, instant         t0
  (84265, 47857, 1, 10000, 50, 1),  -- Drain Life R9     30 yd leech, 5s channel    t50
  (84265, 47864, 1, 12000, 75, 1),  -- Curse of Agony R9 30 yd DoT                  t75
  -- 84266 Dread Maggot  npc_dread_maggot  (uc8)
  (84266, 59363, 1,  8000,  1, 1),  -- Acid Splash       5 yd ground DoT            t0
  (84266, 59018, 1, 12000, 75, 1),  -- Bile Vomit        15 yd cone                 t75
  -- 84267 Abyss Hound  npc_abyss_hound  (uc1)
  (84267, 47864, 1,  8000,  1, 1),  -- Curse of Agony R9 30 yd DoT, instant         t0
  (84267, 47809, 1, 10000, 50, 1),  -- Shadow Bolt R13   30 yd single, 3.0s cast    t50
  -- 84268 Tormented Soul  npc_tormented_soul  (uc8)
  (84268, 48125, 1,  8000,  1, 1),  -- Shadow Word: Pain 30 yd DoT   PROMOTED from t75
  (84268, 50511, 1, 10000, 50, 1),  -- Curse of Weakness 30 yd debuff               t50
  (84268, 42917, 1, 60000, 75, 1),  -- Frost Nova R6     10 yd  CC (MOD_ROOT)  t0, moved up
  -- 84269 Putrid Fleshbeast  npc_putrid_fleshbeast  (uc1)
  (84269, 47809, 1,  8000,  1, 1),  -- Shadow Bolt R13   30 yd single, 3.0s cast    t0
  (84269, 71264, 1, 12000, 75, 1),  -- Swarming Shadows  any range, single DoT      t75
  -- 84270 Netherclaw Demon  npc_netherclaw_demon  (uc8)
  (84270, 47867, 1,  8000,  1, 1),  -- Curse of Doom     30 yd DoT, instant         t0
  (84270, 47857, 1, 10000, 50, 1),  -- Drain Life R9     30 yd leech, 5s channel    t50
  (84270, 50511, 1, 12000, 75, 1),  -- Curse of Weakness 30 yd debuff               t75
  -- 84271 Fleshbound Horror  npc_fleshbound_horror  (uc1)
  (84271, 67879, 1,  7000,  1, 1),  -- Claw              weapon damage  PROMOTED from t50
  (84271,  5918, 1, 60000, 50, 1),  -- Shadowst. Stab    CC (MOD_STUN)    t0, moved up
  -- 84272 Voidbound Revenant  npc_voidbound_revenant  (uc8, demoted from range)
  (84272, 47857, 1,  8000,  1, 1),  -- Drain Life R9     its former nuke, kept      t0
  (84272, 48125, 1, 10000, 50, 1),  -- Shadow Word: Pain 30 yd DoT                  t50
  -- 84273 Blightfang  npc_blightfang  (uc1)
  (84273, 67879, 1,  6000,  1, 1),  -- Claw              weapon damage              t0
  (84273, 22644, 1, 10000, 50, 1),  -- Blood Leech       10 yd area leech           t50
  (84273, 47864, 1, 12000, 75, 1),  -- Curse of Agony R9 30 yd DoT                  t75
  -- 84274 Carrion Watcher  npc_carrion_watcher  (uc8)
  (84274, 54889, 1,  8000,  1, 1),  -- Shadow Shock      25 yd area damage          t0
  (84274, 47857, 1, 10000, 50, 1),  -- Drain Life R9     30 yd leech, 5s channel    t50
  (84274,  1010, 1, 12000, 75, 1),  -- Curse of Idiocy   30 yd stat debuff          t75
  -- 84275 Grotesque Brute  npc_grotesque_brute  (uc1)
  (84275, 42397, 1,  7000,  1, 1),  -- Rend Flesh        damage + bleed             t0
  (84275,  8599, 1, 10000, 50, 1),  -- Enrage            self damage buff           t50
  (84275, 50511, 1, 12000, 75, 1),  -- Curse of Weakness 30 yd debuff               t75
  -- 84277 Rotting Hound  npc_rotting_hound  (uc1)
  (84277, 50729, 1,  7000,  1, 1),  -- Carnivorous Bite  damage + bleed             t0
  (84277,  8599, 1, 10000, 50, 1),  -- Enrage            self damage buff           t50
  (84277, 22644, 1, 12000, 75, 1),  -- Blood Leech       10 yd area leech           t75
  -- 84278 Hellpit Crawler  npc_hellpit_crawler  (uc8, demoted from range)
  (84278, 59363, 1,  8000,  1, 1),  -- Acid Splash       5 yd ground DoT            t0
  (84278, 59018, 1, 12000, 75, 1),  -- Bile Vomit        15 yd cone                 t75
  -- 84279 Bloodspike Beast  npc_bloodspike_beast  (uc1)
  (84279, 59269, 1,  7000,  1, 1),  -- Carnivorous Bite  damage + bleed             t0
  (84279,  8599, 1, 10000, 50, 1),  -- Enrage            self damage buff           t50
  (84279, 70654, 1, 12000, 75, 1),  -- Blood Armor       self damage-taken buff     t75
  -- 84280 Warped Bonefiend  npc_warped_bonefiend  (uc8, demoted from range)
  (84280, 69900, 1,  8000,  1, 1),  -- Spirit Burst      15 yd area damage          t0
  (84280, 48125, 1, 10000, 50, 1),  -- Shadow Word: Pain its former nuke, kept      t50
  (84280, 47867, 1, 12000, 75, 1),  -- Curse of Doom     30 yd DoT                  t75
  -- 84282 Twisted Abomination  npc_twisted_abomination  (uc8, demoted from range)
  (84282, 70191, 1,  7000,  1, 1),  -- Cleave            weapon damage              t0
  (84282,  8599, 1, 10000, 50, 1),  -- Enrage            self damage buff           t50
  (84282,  1010, 1, 12000, 75, 1),  -- Curse of Idiocy   30 yd stat debuff          t75
  -- 84283 Gloomfang  npc_gloomfang  (uc8, demoted from range)
  (84283, 59126, 1,  8000,  1, 1),  -- Shadow Breath     15 yd cone damage          t0
  (84283, 47857, 1, 10000, 50, 1),  -- Drain Life R9     its former nuke, kept      t50
  -- 84284 Underground Ghoul  npc_underground_ghoul  (uc1)
  (84284, 59992, 1,  7000,  1, 1),  -- Cleave            weapon damage              t0
  (84284,  8599, 1, 10000, 50, 1),  -- Enrage            self damage buff           t50
  (84284, 48640, 1,  8000, 75, 1),  -- Strike            weapon damage              t75
  -- 84286 Disgusting Larva  npc_disgusting_larva  (uc1)
  (84286, 59116, 1,  8000,  1, 1),  -- Poison Cloud      6 yd ground DoT            t0
  (84286, 48130, 1, 10000, 75, 1),  -- Gore              damage + bleed             t75
  -- ==========================================================================
  -- BOSSES (role 2) - melee model, same cadence, three positions
  -- ==========================================================================
  -- 84288 Dralak  boss_dralak  (uc8, level 82)
  (84288, 66536, 1,  7000,  1, 1),  -- Holy Smite        50 yd single, 1.25s cast   t0
  (84288, 57798, 1, 10000, 50, 1),  -- Consecration      8 yd ground DoT            t50
  (84288, 58944, 1, 12000, 75, 1),  -- Devotion Aura     20 yd resistance aura      t75
  -- 84289 Lord Maltrion  boss_vampir_lord  (uc8, level 82)
  (84289, 34240, 1,  8000,  1, 1),  -- Carrion Swarm     40 yd cone damage          t0
  (84289, 51016, 1, 10000, 50, 1),  -- Vampiric Bolt     40 yd damage + leech       t50
  (84289, 64160, 1, 12000, 75, 1),  -- Blood Tap         45 yd leech   phase-2 slot, see
                                    --                   the 34111 exclusion above
  -- 84290 Mor'Kar  boss_crypt_lord  (uc8, level 82)
  (84290, 70965, 1,  8000,  1, 1),  -- Crypt Scarabs     40 yd single, 2.0s cast    t0
  (84290, 67860, 1, 10000, 50, 1),  -- Impale            6 yd cone damage           t50
  (84290, 28615, 1, 12000, 75, 1);  -- Spike Volley      30 yd area damage          t75
