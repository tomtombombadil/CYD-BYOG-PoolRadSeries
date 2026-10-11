// What the engine knows about each game's program (START.EXE / GAME.EXE):
// which release it is and where in the unpacked program the few tables the
// engine reads from it sit. Only sizes and addresses live here - the
// tables themselves are always read from the player's own copy (bring your
// own game).
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// Addresses are offsets in the program's data segment (DS); data_base is
// where DS:0 lies in the unpacked program. A program is recognised by its
// file size and unpacked size together; other releases need their own
// entry (the engine says "a version this engine doesn't know yet").
#pragma once

#include <cstdint>

#include "classes.h"
#include "rules.h"
#include "spells.h"
#include "combat.h"
#include "ecl.h"
#include "games.h"
#include "treasure.h"
#include "sound.h"

namespace profile {

// The screen frame's tile layout tables (see layout.h). Each is one byte a
// cell: the frame tile to draw there.
struct FrameTables {
    uint16_t top;            // row 0, 40 cells
    uint16_t bar;            // a full-width bar between areas, 40 cells
    uint16_t bottom;         // row 23, 40 cells
    uint16_t left;           // column 0, rows 0-23
    uint16_t right;          // column 39, rows 0-23
    uint16_t view_split;     // column 16, rows 0-16 (exploring)
    uint16_t view_top;       // the 3D view's own frame: row 2, cols 2-14 (indexed by column)
    uint16_t view_bottom;    //   row 14, cols 2-14
    uint16_t view_left;      //   col 2, rows 2-14 (indexed by row)
    uint16_t view_right;     //   col 14, rows 2-14
    uint16_t combat_left;    // combat: column 0, rows 0-22
    uint16_t combat_split;   //   column 22, rows 0-22
    uint16_t combat_right;   //   column 39, rows 0-22
};

// One step of the title sequence: a TITLE.DAX picture at a cell position,
// or (block 0) the credits screen; then a wait (a tap skips it).
struct TitleStep {
    uint8_t  block;          // 0 = credits
    uint8_t  row, col;       // where the picture's top left goes, in cells
    bool     clear;          // clear the screen first
    uint16_t wait_ms;        // 0 = go straight on
    uint8_t  sound;          // a sound effect as it shows (0 = none)
};

// A table of names in the program: Pascal strings, each in a fixed-size
// slot (class, race, alignment ... names)
struct NameTable {
    uint32_t at;                   // program image offset
    uint8_t  stride, count;
};

// A character's items menu and what its choices say (GAME.OVR offsets of
// Pascal strings), in this order
enum ItemWord {
    kUse, kTrade, kDrop, kHalve, kJoin, kSell, kId,          // " Use" ... " Id" (after "Ready")
    kMustUnready, kMustReady, kCantHalve,                    // "Must be unreadied" ...
    kYour, kGoneForever, kDropIt,                            // "Your " + item + " " + "will be gone forever", "Drop It? "
    kTradeWhom,                                              // "Trade with Whom?"
    kGiveYou, kGoldFor, kDeal, kSold, kOverloadPool,         // the shop's Sell
    kIdentify, kNothingNew, kSortOf, kNoMoney,               // the shop's Id
    kSelect,                                                 // "Select" (picking a character)
    kItemWords
};

// The shop's and temple's other words (GAME.OVR offsets of Pascal strings),
// in this order
enum ShopWord {
    kHealMenuMoney, kHealMenu, kHelpYou, kHealExit,             // the temple's menus, ", how can we help you?"
    kCureName,                                                  // 10 names in rules::Cure order
    kCastAnyway = kCureName + 10, kOnlyCost, kGoldPieces, kPayFor, kNotEnough, kCured,
    kNotBlind, kNotDiseased, kNotDead, kNotPoisoned, kNotCursed, kNotStoned,
    kPriestSays, kPriestRetrieve, kShopSays, kShopRetrieve,     // leaving coins on the counter
    kCollection, kGemsKey, kJewelryKey, kExitKey, kAppraisePrompt, kGemValued, kGp, kSellKey, kSellKeep, kYouCan,
    kJewelValued, kNoGems, kGemWord, kGemsWord, kJewelWord, kJewelsWord,   // Appraise
    kCoinType, kSelectWord, kHowMuch, kWillTake, kOverPool,     // Take
    kTrainConscious, kTrainCost, kTrainClass, kTrainExp,        // a training hall (Train Character)
    kWillBecome, kALevel, kAndALevel, kWishTrain, kCongrats,
    kShopWords
};

// Spells and resting: GAME.OVR offsets of Pascal strings, in this order
enum MagicWord {
    kSpellsWord, kInMemory, kInGrimoire, kToMemorize, kChooseSpell, kMemorizeKey,   // "Spells " ... "Memorize"
    kMemorizeThese, kMemorizeThese2, kCanMemorize,              // "Memorize These Spells? ", "can memorize:"
    kClericSpells, kDruidSpells, kMuSpells,                     // "    Cleric Spells:" ...
    kCannotMemorize, kNoCondition, kMemorizeSpells,             // "cannot memorize any spells", "is in no condition to ", ...
    kRestTime, kRestMenu, kStopResting, kHealedAll, kHasMemorized, kInterrupted,
    kMagicWords
};

// Casting in camp and the effects list (GAME.OVR offsets of Pascal strings),
// in this order
enum CastWord {
    kCastAny, kNoSpells, kCastKey, kCasts, kCantCastHere, kLoseIt, kCastOnWhom,   // "cast any spells" ... "Cast Spell on whom"
    kFullyHealed, kPartlyHealed, kIsCured, kIsUnaffected, kCanSee, kUnpoisoned,  // what a spell did
    kRaised, kUncursed, kItemUncursed, kNoEffects,                               // ... " <No Spell Effects>"
    kScribeKey, kOnScrolls, kToScribe, kNoCopyable, kAlreadyKnow, kAlreadyScribing,   // Scribe: "Scribe" ...
    kCannotScribe, kScribeThese, kScribeThese2, kScribeAny, kHasScribed,           // ... "has scribed"
    kToChoose, kLearnKey,                                                         // training's "to Choose", "Learn"
    kThatItem, kCombatOnly, kUseIt,                                               // "That Item", "is a combat-only item...", "Use it? "
    kUsesItem, kItemColon, kOops, kOnScroll,                                      // "uses an item", "Item:", "oops!", "on Scroll"
    kMiscasts,                                                                    // "miscasts" (the Robe of Vermin)
    kCastWords
};

// The camp's Alter (GAME.OVR offsets of Pascal strings), in this order
enum AlterWord {
    kAlterPrompt, kPartyOrder, kHasBeenSelected,                 // "Alter: ", "Party Order: ", "has been selected"
    kSpeedIs, kSpeedRange, kFaster, kSlower, kSpeedExit, kSpeedPrompt,   // "Game Speed = " ... "Game Speed:"
    kWillBeGone, kDropFromParty, kQuitToDos, kBidsFarewell, kDumped, kRelief,
    kPicsOn, kAnimOn, kAnimOff, kPicsOff, kPicsExit,             // Pics: "Pics on  " ... "Exit"
    kAlterWords
};

// The icon editor's words (GAME.OVR), in this order
enum IconWord { kIconOld, kIconReadyAction, kIconNew, kIconSmall, kIconLarge, kIconHair, kIconFace, kIconOk, kIconWords };

// Combat's words (GAME.OVR offsets of Pascal strings), in this order
enum FightWord {
    kBattleBegins, kFMove, kFViewAim, kFUse, kFCast, kFTurn, kFQuickDone,          // "A battle begins...", the menu
    kMoveLeft, kFleeAsk, kCantGo, kNotWithWeapon,                                  // "Move/Attack, Move Left = " ...
    kRangeIs, kFTarget, kNextPrevManual, kCenterExit, kAimPrompt,                  // "Range = " ... "Aim:"
    kFGuard, kDelayQuit, kFBandage, kSpeedExitW, kGuarding, kGameSpeed, kSpeedClose, kFSlower, kFFaster, kFExit,
    kBackstabs, kSlays, kAttacksW, kFromBehind, kCruelBlow, kHittingFor, kPointW, kPointsW, kOfDamage, kMisses,
    kLostSpell, kGoesDown, kIsDying, kIsKilled,
    kTeammateDying, kContinueBattle, kGotAway, kEscapeBlocked, kAttackAlly, kFleesPanic, kForcedFlee, kSurrenders,
    kBandaged, kSweeps, kHitpoints, kAcW, kHelpless,
    kHasFled, kLostFight, kHasWon, kFoundTreasure, kEachReceives, kExpPoints, kPressEnter, kRejoice, kPressAnyKey,
    kTakeColon, kMoneyItems, kViewPoolExit, kExitSp, kDetectExit, kViewTakePoolShare, kViewTakePool,
    kTreasureLeft, kClaimTreasure, kItemsColon, kTakeW,
    kTakes, kPointsOfDamage, kTakes1, kFromFire, kFromCold, kFromElec, kFromAcid, kFromMagic, kGoesDownS,
    kIsUnaffectedS, kCastsASpell, kSpellColon, kBeginsCasting, kCampOnly, kTurnsUndead, kIsTurned, kIsDestroyed,
    kNothingHappens, kAlreadyTargeted, kAbortSpell, kSpellAborted,               // ... "Abort Spell? ", "Spell Aborted"
    kNoxiousCloud, kAirClears, kPoisonCloud, kIsPoisoned, kIsCoughing,            // "Creates a noxious cloud" ...
    kIsSilenced, kFightingSnakes,                                                // a turn's start: "is silenced" ...
    // The monsters' specials: "engulfs ", "hugs ", "Avoids it", "is Paralyzed", "is paralyzed", "Suffocates",
    // the beholder's rays and their results, the gaze, spit, breath, thrown lightning, a troll getting up
    kEngulfs, kHugs, kAvoidsIt, kIsParalyzedC, kIsParalyzedL, kSuffocates,
    kRayDisint, kIsDisint, kRayStone, kIsStonedR, kRayDeath, kWoundsYou,
    kGazes, kReflects, kIsStonedG, kSpitsAcid, kSpitsMisses, kBreathesAcid, kBreathesFire, kBreathesFireH,
    kThrowsLightning, kStandsUp, kGetsBackUp,
    // Confusion's turns, Dispel Evil's blow, the fire shield ("flame type: ", "Hot Cold", "Abort spell? ", "Yes No")
    kIsConfusedT, kRunsAway, kGoesBerserk, kIsEnraged, kIsDispelled, kResistsDispel, kGetsZapped,
    kFlameType, kHotCold, kAbortSpellQ, kYesNoF,
    kGainsItem, kCollapses, kDiesFromPoison,                                     // "Gains an item" ...
    kIsAffectedW,                                                                // Detect's "is affected"
    kLostImage,                                                                  // Mirror Image: "lost an image"
    kFightWords
};

// An effect's name in the effects list (Display): a GAME.OVR string
struct EffectName {
    uint8_t  type;
    uint32_t at;
};

struct Profile {
    games::Game  game;
    const char*  release;        // shown to the player, e.g. "GOG"
    const char*  program;        // file name in the game folder
    uint32_t     program_size;   // bytes on disk
    uint32_t     image_size;     // unpacked bytes
    uint32_t     data_base;      // DS:0 in the unpacked program
    FrameTables  frame;
    const char*  tiles_file;     // the 8x8 tiles the frame is made of
    uint8_t      tiles_block;

    // Strings in the program: "Press any key to continue" (text windows)
    uint32_t     press_any_key;  // image offset of the Pascal string

    // The title sequence
    const char*      title_file;
    const TitleStep* title;
    int              title_steps;
    // After it, the version line and its menu ("Play Demo"): Pascal
    // strings in the program image (0 = none)
    uint32_t         title_version, title_menu;

    // GAME.OVR (the overlay file): the credits screen's print calls
    const char*  overlay;
    uint32_t     overlay_size;
    uint32_t     credits_at;     // first print call
    uint32_t     credits_base;   // its code segment's start in the file
    uint8_t      credits_bars[2];   // rows of the frame's bars on that screen

    // 3D areas: files GEOn / WALLDEFn / 8X8Dn / ECLn.DAX for areas
    // first_area .. last_area; the common wall tiles; the horizon picture
    uint8_t      first_area, last_area;
    uint8_t      common_tiles_block;   // in 8X8D1.DAX
    const char*  sky_file;
    uint8_t      horizon_block;
    const ecl::OpSet* ecl_ops;         // the script's opcodes (nullptr: unknown)

    // Scripts and the 3D view while playing
    uint16_t     sky_colours;          // DS offset of the 16 sky colours (area words
                                       // 0x4BFD / 0x4BFE pick one)
    uint8_t      start_area, start_script;   // a new game: ECL<area> block <script>

    // The wilderness map (a big picture): a blinking square marks the
    // party's place while the game waits for the player
    struct {
        uint8_t  bigpic;               // its BIGPIC block (0: no wilderness map)
        uint16_t city_var;             // script word holding the place's number
        uint16_t xs, ys;               // DS offsets of the places' columns / rows (8-px cells)
        uint8_t  count;                // places in those tables
        uint8_t  hide_pic;             // no square after this event picture was shown
    } wild;

    // The party menu (the games' first screen: Create New Character ...
    // Load Saved Game ... BEGIN Adventuring) and the party list
    struct {
        uint32_t items;                // program image offset of the menu table: `count`
        uint8_t  count, stride;        //   entries of a Pascal string[40] + an "on" byte
        uint32_t choose;               // GAME.OVR offsets of Pascal strings: "Choose a function "
        uint32_t load_which;           //   "Load Which Game: "
        uint32_t name, ac_hp;          //   the party list's headings "Name", "AC  HP"
        const char* cfg;               // the configuration file naming the save folder
        uint32_t save_which, slots;    // GAME.OVR: "Save Which Game: ", "A B C D E F G H I J"
        uint32_t saving;               //   "Saving...Please Wait"
        uint32_t camp_menu;            // program image: "Save View Magic Rest Alter Fix Exit"
        uint32_t camp, makes_camp;     // GAME.OVR: "Camp:", "The party makes camp..."
        // Add / Remove / Drop (GAME.OVR): "Add from where? ", "Curse Pool Hillsfar Exit",
        // "Add a character: ", "Add ", "* ", the party rules' three messages,
        // "Overwrite ", "? ", "Drop ", " forever? ", "Are you sure? ", "You dump ",
        // " out back.", " bids you farewell.", " breathes a sigh of relief.", "Yes No"
        uint32_t add_from, add_sources, add_prompt, add, added, paladin_evil, rangers, no_evil,
                 overwrite, qmark, drop, forever, sure, dump, out_back, farewell, relief, yes_no;
        // Modify Character (GAME.OVR): " can't be modified.", "Modify: ", "Keep Exit"
        uint32_t cant_modify, modify, keep_exit;
        // Pool characters from a saved game: "from saved game " (GAME.OVR)
        uint32_t from_saved;
        // Remove -> "Overwrite NAME? " -> No: "New file name: " (GAME.OVR; curse_finish_facts.md 6)
        uint32_t new_file;
    } party;

    // View Character: name tables in the program and the screen's words
    // in GAME.OVR
    struct {
        NameTable cls, race, alignment, sex, money, health;
        // GAME.OVR offsets of Pascal strings
        uint32_t npc, age, stats, level, exp, status, ac, hp, thac0, damage,
                 encumbrance, movement, exit;
        uint8_t  stats_stride;         // "STR ", "INT " ... one after another
    } view;

    // Items: the name words (word 1 first) in the program, the item type
    // table file, and item types the rules treat specially
    struct {
        NameTable   words;
        const char* types_file;
        uint8_t     arrow, quarrel, dart, flask;   // missiles, flask of oil (names)
        uint8_t     keep1, keep2;                  // name words that keep a missile's name singular
        uint8_t     elf_bonus[6];                  // +1 to hit for elves (bows, short / long sword)
        uint32_t    buy_items, buy;                // GAME.OVR: "Items: " (the shop's list), "Buy"
        uint32_t    list_next, list_prev, list_exit;   //   a list's " Next", " Prev", " Exit"
        uint32_t    shop_menu, shop_menu_money;    //   "Buy View Pool Appraise Exit" / with Take, Share
        uint32_t    no_money;                      //   "Not enough Money."
        uint32_t    overloaded;                    //   "Overloaded"
        // A character's items: "Items", "Ready Item", "Ready", " Yes  ", " No   ",
        // "It's Cursed", "Wrong Class", "already using ", "Your hands are full!",
        // "'s"; View Character's "Weapon", "Armor"
        uint32_t    title, heading, ready, yes, no, cursed, wrong_class, already, hands_full, plural_s,
                    weapon, armour;
        // The effects the stats are worked out with: Strength, the giant
        // strength potion's, Enlarge, Friends, Feeblemind, Constitution 20+'s healing
        uint8_t     stat_fx[6];
        uint8_t     hammer[4];                     // Spiritual Hammer: its effect, the item type, its two words
        uint8_t     detect[4];                     // Detect Magic: its effect, the spells that give it (treasure's Detect)
    } items;

    // Character rules (making and training characters): the rule tables in
    // the program's data segment (DS:0 at image offset ds_image), a few
    // facts that are code in the program, and the creation screens' words
    struct {
        uint32_t        ds_image;
        classes::Layout tables;
        uint16_t        hp_count, hp_dice;    // DS: dice at level 1 / sides, 8 classes
        uint16_t        icon_colours;         // DS: the 6 default combat icon colours
        // effect (affect) types given at creation
        uint8_t         con_save, dwarf_orc, giants, gnome_giant, gnome_extra, elf_sleep, halfelf, prot_evil,
                        ranger_giant;
        // spells: a new magic-user's four, silent training's at levels 2-5
        uint8_t         mu_first[4], mu_level2, mu_level3[2], mu_level4, mu_level5;
        // GAME.OVR: "Pick Race", "Pick Gender", "Pick Class", "Pick Alignment", "Select",
        // "Reroll stats? ", "Character name: ", "Save ", "? "
        uint32_t        pick_race, pick_gender, pick_class, pick_alignment, select, reroll, char_name, save_q,
                        qmark;
        // Human Change: a new magic-user's spells; GAME.OVR "Pick New Class", " doesn't qualify.",
        // "Select", " is now a 1st level ", "."
        uint8_t         mu_change[4];
        uint32_t        pick_new, no_qualify, change_select, now_first, dot;
    } create;

    // Items: Use Trade Drop Halve Join Sell Id and their words (ItemWord order)
    uint32_t item_words[kItemWords];

    // The shop's Take / Appraise, the temple (ShopWord order); the temple's
    // costs and the effects its cures take away; an appraised gem or jewel
    // kept: item type, name word
    uint32_t         shop_words[kShopWords];
    rules::CureFacts cures;
    uint8_t          gem_type, gem_word, jewel_word;

    // Spells: the camp's magic menu ("Cast Memorize Scribe Display Rest
    // Exit") and the level names ("1st Level") and spell names in the
    // program; the words above; script words for encounters while resting
    // (steps between checks, the chance in %)
    struct {
        uint32_t  menu;
        NameTable levels, names;
        uint32_t  words[kMagicWords];
        uint16_t  rest_period, rest_chance;
        // Casting: the words, what each spell does outside combat (spells
        // not listed and meant for fights "can't be cast here"), the
        // effect numbers the spells need
        uint32_t                  cast_words[kCastWords];
        const spells::CampSpell*  camp;
        uint8_t                   n_camp;
        spells::Facts             facts;
        // The effects list: effects named after the first spell (1-56)
        // that gives them, and those with names of their own
        const uint8_t*            spell_named;
        uint8_t                   n_spell_named;
        const EffectName*         named;
        uint8_t                   n_named;
        // Scrolls: the item word "With 1 Spell", Read Magic's effect
        uint8_t                   scroll_one_spell, read_magic;
    } magic;

    // The camp's Alter: its menu ("Order Drop Speed Icon Pics Exit"), the
    // party order's "Select Exit" / "Place Exit" (START.EXE image), the
    // words above
    struct {
        uint32_t menu, select, place;
        uint32_t words[kAlterWords];
        // The icon editor (alter_icon_facts.md 1): its five menus (program
        // image: Parts.., Head Weapon Exit, Weapon Body xxxx.., " Keep
        // Exit", Next Prev Keep Exit) and its words (GAME.OVR)
        uint32_t icon_menu[5];
        uint32_t icon_words[kIconWords];
    } alter;

    // Combat: the program's tables, the words, the effects that end with
    // a fight
    struct {
        combat::TableAt tables;
        uint32_t        words[kFightWords];
        uint8_t         ends[20];
        combat::Facts   facts;
        const combat::FightSpell* spells;
        uint8_t         n_spells;
    } fight;

    // The script machine's own words (GAME.OVR), in ecl::ScriptWord's order
    uint32_t script_words[ecl::kScriptWords];

    // Random treasure (TREASURE 0x80 + n): the rules' numbers, and where the
    // program keeps its rows of ready-made items (DS offset, 7 x 16 bytes)
    const treasure::Facts* random_items;
    uint16_t               random_rows;

    // The sound driver's tables and byte code (its segment in the program)
    sound::Layout sound;

    // Locked doors (door_facts.md): "Bash", " Pick", " Knock", " Exit",
    // "Locked. " (GAME.OVR) and the Knock spell's number
    struct {
        uint32_t bash, pick, knock, exit, locked;
        uint8_t  knock_spell;
    } door;
    // The game won (PROGRAM 8; curse_finish_facts.md 1): the end texts
    // (GAME.OVR, the pages' lines one after another), the pictures (PIC
    // animations played once with pages 2 and 3, the picture drawn faded
    // with page 4, the head and body for page 5, the big picture for the
    // last page and its fireworks), the fade's colour table (DS: the new
    // colour for each of 16), the area word that stops Begin and the
    // training mask word (everyone may train, free)
    struct {
        uint32_t text[24];
        uint8_t  page_lines[6];
        uint8_t  anim[2];
        uint8_t  fade_pic, head, body, bigpic;
        uint16_t fade_table;
        uint16_t begin_word, train_word;
    } won;
};

// The game's program file name (to look for it), or nullptr if no
// release of that game is known yet.
const char* program_name(games::Game g);

// The profile matching a program, or nullptr.
const Profile* find(games::Game g, uint32_t program_size, uint32_t image_size);

} // namespace profile
