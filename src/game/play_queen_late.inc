// The Dark Queen of Krynn in the Play Test: its screens that need the rest of
// play.cpp (View Character, the name tables) - included by play.cpp near the
// end of its anonymous namespace (play_queen.inc has the start of it).

// A line whose last letter is in column end_col
void q_right(pic::Canvas& c, const char* s, int end_col, int row, uint8_t fg)
{
    put(c, s, end_col + 1 - static_cast<int>(strlen(s)), row, fg);
}

// The far pointer table at `at` (a file offset): entry i's C string
bool q_far_word(dax::ByteSource& exe, const exepack::Info& info, uint32_t at, int i, char* out, size_t cap)
{
    uint8_t p[2];
    out[0] = 0;
    if (exepack::read(exe, info, at + 4u * static_cast<uint32_t>(i), p, 2) != exepack::Status::Ok) return false;
    uint8_t s[32];
    const size_t n = cap - 1 < sizeof s ? cap - 1 : sizeof s;
    if (exepack::read(exe, info, d->prof->data_base + (p[0] | p[1] << 8), s, n) != exepack::Status::Ok) return false;
    size_t k = 0;
    while (k < n && s[k]) ++k;
    memcpy(out, s, k);
    out[k] = 0;
    return true;
}

// View Character's name tables (profile view: far pointer tables) - the
// classes by the engine's numbers (its knight 1 into the engine's 7), the
// races by its own (ext), the three coins in places 4-6
__attribute__((noinline)) void q_name_tables(dax::ByteSource& exe, const exepack::Info& info)
{
    const auto& pv = d->prof->view;
    for (int i = 0; i < pv.cls.count && i < 18; ++i) {
        const int to = i == 1 ? 7 : i == 7 ? 1 : i;
        q_far_word(exe, info, pv.cls.at, i, d->cls[to], sizeof d->cls[0]);
    }
    for (int i = 0; i < pv.race.count && i < 8; ++i) q_far_word(exe, info, pv.race.at, i, d->race[i], sizeof d->race[0]);
    for (int i = 0; i < pv.alignment.count && i < 9; ++i)
        q_far_word(exe, info, pv.alignment.at, i, d->alignment[i], sizeof d->alignment[0]);
    for (int i = 0; i < pv.sex.count && i < 2; ++i) q_far_word(exe, info, pv.sex.at, i, d->sex[i], sizeof d->sex[0]);
    for (int i = 0; i < pv.money.count && i < 3; ++i) q_far_word(exe, info, pv.money.at, i, d->money[4 + i], sizeof d->money[0]);
    for (int i = 0; i < pv.health.count && i < 9; ++i) q_far_word(exe, info, pv.health.at, i, d->health[i], sizeof d->health[0]);
    // (the knights' orders: " of the Crown", " of the Sword", " of the Rose" - C strings one after another)
    uint32_t at = d->prof->variant.queen_orders;
    for (int k = 0; at && k < 3; ++k) {
        uint8_t s[16];
        if (exepack::read(exe, info, at, s, sizeof s) != exepack::Status::Ok) break;
        size_t n = 0;
        while (n < sizeof s - 1 && s[n]) ++n;
        memcpy(d->q_order[k], s, n);
        d->q_order[k][n] = 0;
        at += static_cast<uint32_t>(n) + 1;
    }
}

// View Character (DOSBox): five boxes - the name, status, sex and age,
// hit points, alignment, race, class (a knight's order after it), level and
// experience (8-311 x 8-63); the stats (8-151 x 71-127) and the coins
// (159-311); AC / THAC0 / damage (8-151 x 135-159), encumbrance / movement
// (159-311); the readied weapon and armour (8-311 x 167-183). Labels cyan,
// values grey (the name, the stats and the coins all grey).
__attribute__((noinline)) void q_draw_character(pic::Canvas& c, const party::Character& ch)
{
    static const QBox kBoxes[] = {
        {8, 8, 311, 63}, {8, 71, 151, 127}, {159, 71, 311, 127}, {8, 135, 151, 159}, {159, 135, 311, 159}, {8, 167, 311, 183},
    };
    q_screen(c, kBoxes, 6, true);
    constexpr uint8_t kLabel = 11, kValue = 7;
    char t[48];
    ch.name(t, sizeof t);
    put(c, t, 1, 1, kValue);
    put(c, d->v_status, 20, 1, kLabel);
    put(c, name_of(d->health[0], 13, 9, ch.health()), 27, 1, kValue);
    put(c, name_of(d->sex[0], 7, 2, ch.sex()), 1, 3, kLabel);
    snprintf(t, sizeof t, d->v_age[0] ? d->v_age : "%d", ch.age());
    put(c, t, 8, 3, kLabel);
    put(c, d->v_hp, 20, 3, kLabel);
    snprintf(t, sizeof t, "%d/%d", ch.hp(), ch.hp_max());
    q_right(c, t, 37, 3, kValue);
    put(c, name_of(d->alignment[0], 17, 9, ch.alignment()), 1, 4, kLabel);
    put(c, name_of(d->race[0], 16, 8, ch.ext[party::kExtRace]), 20, 4, kLabel);
    snprintf(t, sizeof t, "%s", name_of(d->cls[0], 27, 18, ch.cls()));
    if (ch.cls() == 7 && ch.ext[party::kExtKnight] >= 1 && ch.ext[party::kExtKnight] <= 3)
        strncat(t, d->q_order[ch.ext[party::kExtKnight] - 1], sizeof t - strlen(t) - 1);
    put(c, t, 1, 5, kLabel);
    put(c, d->v_exp, 24, 6, kLabel);
    {
        int top = 0;
        for (int k = 0; k < 8; ++k)
            if (ch.level(k) > top) top = ch.level(k);
        size_t o = static_cast<size_t>(snprintf(t, sizeof t, "%s ", d->v_level));
        for (int k = 0, first = 1; k < 8 && o < sizeof t; ++k) {
            const int lv = ch.level(k), old = ch.old_level(k);
            if (lv > 0 || (old > 0 && old < top)) {
                o += static_cast<size_t>(snprintf(t + o, sizeof t - o, "%s%d", first ? "" : "/", lv + old));
                first = 0;
            }
        }
        put(c, t, 1, 7, kLabel);
    }
    snprintf(t, sizeof t, "%lu", static_cast<unsigned long>(ch.exp()));
    q_right(c, t, 32, 7, kValue);
    for (int i = 0; i < 6; ++i) {
        const int v = ch.stat(i);
        if (i == 0 && v == 18 && ch.str00() > 0)
            snprintf(t, sizeof t, "%s18(%02d)", d->v_stat[i], ch.str00() == 100 ? 0 : ch.str00());
        else
            snprintf(t, sizeof t, "%s%d", d->v_stat[i], v);
        put(c, t, 1, 9 + i, kValue);
    }
    for (int coin = 4, row = 9; coin < 7; ++coin) {
        const int n = ch.money(coin);
        if (n <= 0) continue;
        put(c, name_of(d->money[0], 11, 7, coin), 20, row, kValue);
        snprintf(t, sizeof t, "%d", n);
        q_right(c, t, 37, row++, kValue);
    }
    put(c, d->v_ac, 1, 17, kLabel);
    const int ac = ch.ac_raw();
    snprintf(t, sizeof t, "%s%d", ac > 60 ? "-" : "", ac > 60 ? ac - 60 : 60 - ac);
    q_right(c, t, 17, 17, kValue);
    // ("THAC0\t ": the word before the tab)
    char w[16];
    snprintf(w, sizeof w, "%s", d->v_thac0);
    if (char* tab = strchr(w, '\t')) *tab = 0;
    put(c, w, 1, 18, kLabel);
    snprintf(t, sizeof t, "%d", ch.thac0());
    q_right(c, t, 17, 18, kValue);
    put(c, d->v_damage, 1, 19, kLabel);
    const int b = ch.damage_bonus();
    if (b) snprintf(t, sizeof t, "%dD%d%s%d", ch.dice(), ch.dice_sides(), b > 0 ? "+" : "-", b > 0 ? b : -b);
    else snprintf(t, sizeof t, "%dD%d", ch.dice(), ch.dice_sides());
    q_right(c, t, 17, 19, kValue);
    put(c, d->v_enc, 20, 17, kLabel);
    snprintf(t, sizeof t, "%d", ch.encumbrance());
    q_right(c, t, 37, 17, kValue);
    int mv = ch.movement();
    if (ch.has_affect(0x2A)) mv *= 2;          // slow
    if (ch.has_affect(0x27)) mv /= 2;          // haste
    put(c, d->v_move, 20, 18, kLabel);
    snprintf(t, sizeof t, "%d", mv);
    q_right(c, t, 37, 18, kValue);
    const int wi = items::readied_in(ch.items, ch.n_items, *names, items::kSlotWeapon);
    const int ai = items::readied_in(ch.items, ch.n_items, *names, items::kSlotArmour);
    if (wi >= 0) {
        names->name(items::Item{ch.items[wi]}, t, sizeof t);
        put(c, t, 1, 21, kValue);
    }
    if (ai >= 0) {
        names->name(items::Item{ch.items[ai]}, t, sizeof t);
        put(c, t, 1, 22, kValue);
    }
    char keys[80];
    view_keys(ch, keys, sizeof keys);
    text::build(menu, "", keys);
    menu.selected = 0;
    show_menu_line(c);
}
