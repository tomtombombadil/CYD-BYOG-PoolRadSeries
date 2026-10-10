// Behaviour checks: items (included by sweep.cpp with -DBEHAVE_ITEMS).
// What each item does when readied and used, against the original's rules
// (claude/behaviour_facts.md: items). The item records come from the
// player's own files (ITEMS_BIN: 63-byte records made in scratch space).

static std::vector<uint8_t> b_items;           // all the records
static int b_n_items = 0;

static bool b_load_items()
{
    const char* file = getenv("ITEMS_BIN");
    FILE* in = fopen(file ? file : "/tmp/claude-0/sw/items.bin", "rb");
    if (!in) return false;
    uint8_t r[63];
    b_items.clear();
    while (fread(r, 1, 63, in) == 63) b_items.insert(b_items.end(), r, r + 63);
    fclose(in);
    b_n_items = static_cast<int>(b_items.size() / 63);
    return b_n_items > 0;
}

// The first record of a type with no plus and no effects (a plain weapon)
static const uint8_t* b_plain_of_type(int type)
{
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = &b_items[k * 63];
        if (r[0x2E] == type && r[0x32] == 0 && r[0x3C] == 0 && r[0x3D] == 0 && r[0x3E] == 0) return r;
    }
    return nullptr;
}

// A character's only readied item: `rec` (any other items gone)
static void b_hold_only(party::Character& ch, const uint8_t* rec)
{
    using namespace play;
    ch.n_items = 0;
    if (rec) {
        rules::add_item(ch, rec);
        ch.items[0][0x34] = 1;
        ch.items[0][0x36] = 0;
    }
    rules::recalc(ch, *names, d->facts);
}

static void behave_items()
{
    using namespace play;
    if (!b_load_items()) {
        expect("items", "item records", false, "no ITEMS_BIN");
        return;
    }
    if (!boot()) {
        expect("items", "boot", false);
        return;
    }
    // ---- A magic weapon's plus: +1 to hit AND +1 damage (the long sword, type 36)
    {
        const uint8_t* plain = b_plain_of_type(36);
        if (!expect("items", "a plain long sword in the game's items", plain != nullptr)) return;
        party::Character& ch = pt->m[1];             // a fighter of the sample party
        b_hold_only(ch, plain);
        const int hit0 = ch.rec[0x199], dmg0 = ch.damage_bonus(), dice0 = ch.dice(), sides0 = ch.dice_sides();
        uint8_t plus1[63];
        memcpy(plus1, plain, 63);
        plus1[0x32] = 1;
        b_hold_only(ch, plus1);
        expect("items", "+1 long sword: to hit +1", ch.rec[0x199] == hit0 + 1, "to-hit value %d -> %d", hit0, ch.rec[0x199]);
        expect("items", "+1 long sword: damage +1", ch.damage_bonus() == dmg0 + 1, "damage bonus %d -> %d", dmg0,
               ch.damage_bonus());
        expect("items", "+1 long sword: the long sword's dice", ch.dice() == dice0 && ch.dice_sides() == sides0, "%dd%d",
               ch.dice(), ch.dice_sides());
        const items::TypeInfo& ti = names->type(36);
        expect("items", "long sword: 1d8 against man-sized (the ITEMS table)", dice0 == ti.dice && sides0 == ti.sides,
               "%dd%d, table %dd%d", dice0, sides0, ti.dice, ti.sides);
    }
}
