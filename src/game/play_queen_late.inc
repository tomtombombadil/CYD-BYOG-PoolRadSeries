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

// ---- Saving (queen_facts_3.md 7) ---------------------------------------------
// SAVGAMx.QSV in one go: written to _CYD/QSV.TMP first - its records laid
// over the loaded game's own (what the engine doesn't keep stays: spells,
// pointers) and that game's rest after them (the GOG folder's SAVGAMA.QSV
// for a new game) - then copied over the slot's file.
__attribute__((noinline)) bool q_save(char slot, const savegame::Header& h, const char* path)
{
    char src_path[200], tmp[160];
    snprintf(tmp, sizeof tmp, "%s/_CYD/QSV.TMP", games::kRootDir);
    if (d->q_src) {
        char nm[24];
        save_file_name(d->q_src, nm, sizeof nm);
        save_path(nm, src_path, sizeof src_path);
    }
    plat::File in;
    if (d->q_src) in = plat::open(src_path, "r");
    if (!in) {
        char rel[24];
        save_file_name('A', rel, sizeof rel);
        open_file(rel, in);
    }
    plat::File out = plat::open(tmp, "w");
    if (!out) {
        if (in) in.close();
        return false;
    }
    bool ok;
    {
        plat::FileSource s(in);
        FileSink sink(out);
        ok = savegame::write_queen(sink, d->gs, h, *pt, in ? &s : nullptr);
    }
    out.close();
    if (in) in.close();
    // (the slot's file from the copy)
    plat::File a = plat::open(tmp, "r");
    plat::File b = ok && a ? plat::open(path, "w") : plat::File{};
    if (!a || !b) ok = false;
    uint8_t buf[256];
    while (ok) {
        const int n = a.read(buf, sizeof buf);
        if (n <= 0) break;
        ok = b.write(buf, static_cast<size_t>(n)) == static_cast<size_t>(n);
    }
    if (a) a.close();
    if (b) b.close();
    plat::remove(tmp);
    if (ok) d->q_src = slot;
    return ok;
}

// ---- Fights (queen_facts_3.md 8) ---------------------------------------------
// MONCHA.GLB's monster id: a big-endian u16 (the whole size), then the rest
// run-length coded (as the DAX blocks: c < 0x80 c + 1 bytes as they are,
// else the next byte 257 - c times) - its 414-byte record (a character's,
// the first 0x52 bytes zero), a u8 effect count, a u8 item count, the
// effects (5 bytes: Curse's first five), the items (17 bytes), a pad byte.
__attribute__((noinline)) bool q_load_monster(int id, uint8_t* rec, uint8_t* sx, uint8_t* items, int* n_items,
                                              int items_cap, uint8_t* aff, int* n_aff, int aff_cap)
{
    *n_items = *n_aff = 0;
    plat::File f;
    if (!open_hl("MONCHA.GLB", f)) return false;
    uint8_t* raw = nullptr;
    uint8_t* q = nullptr;
    uint32_t total = 0;
    bool ok = false;
    {
        plat::FileSource src(f);
        uint32_t pos, len;
        if (hlib::find_entry(src, static_cast<uint16_t>(id), pos, len) && len >= 4 && len < 2048) {
            raw = static_cast<uint8_t*>(malloc(len));
            if (raw && src.read_at(pos, raw, len) == len) {
                total = static_cast<uint32_t>(raw[0] << 8 | raw[1]);
                if (total >= party::kQueenRecordSize + 2 && total < 4096) q = static_cast<uint8_t*>(calloc(1, total));
                uint32_t o = 0;
                for (uint32_t i = 2; q && i < len && o < total;) {
                    const uint8_t c = raw[i++];
                    if (c < 0x80) {
                        for (int k = 0; k <= c && i < len && o < total; ++k) q[o++] = raw[i++];
                    } else if (i < len) {
                        const uint8_t v = raw[i++];
                        for (int k = 0; k < 257 - c && o < total; ++k) q[o++] = v;
                    }
                }
                ok = q && o >= party::kQueenRecordSize + 2;
            }
        }
    }
    f.close();
    free(raw);
    if (ok) {
        party::Character* tmp = new (std::nothrow) party::Character;
        ok = tmp != nullptr;
        if (tmp) {
            party::from_queen(q, *tmp);
            memcpy(rec, tmp->rec, party::kRecordSize);
            sx[0] = 0;                                  // (its flags 2: not known)
            sx[1] = tmp->ext[party::kExtMagicRes];
            delete tmp;
        }
        const int nf = q[party::kQueenRecordSize], ni = q[party::kQueenRecordSize + 1];
        uint32_t at = party::kQueenRecordSize + 2;
        for (int k = 0; ok && k < nf && at + 5 <= total; ++k, at += 5)
            if (*n_aff < aff_cap) {
                uint8_t* e = aff + party::kAffectSize * (*n_aff)++;
                memset(e, 0, party::kAffectSize);
                memcpy(e, q + at, 5);
            }
        for (int k = 0; ok && k < ni && at + party::kQueenItemSize <= total; ++k, at += party::kQueenItemSize)
            if (*n_items < items_cap) party::queen_item_in(q + at, items + items::kRecordSize * (*n_items)++);
    }
    free(q);
    return ok;
}

// A fight picture: COMSPR / CPIC / CBODY.TLB's part `id` (their directory
// ids; + 128 the attack frames) - its library's colours first - into a
// Pic4 (rows of the screen's values, the see-through plane after them)
__attribute__((noinline)) uint8_t* q_load_pic4(const char* file, int id, int* w, int* h)
{
    const char* lib = !strncmp(file, "COMSPR", 6) ? "COMSPR.TLB" : !strncmp(file, "CPIC", 4) ? "CPIC.TLB"
                      : !strncmp(file, "CBODY", 5) ? "CBODY.TLB" : nullptr;
    if (!lib) return nullptr;
    plat::File f;
    if (!open_hl(lib, f)) return nullptr;
    uint8_t* out = nullptr;
    {
        plat::FileSource src(f);
        hlib::Library l;
        uint32_t pos, len;
        hlib::Part p;
        if (hlib::open(src, 0, src.size(), l) == hlib::Status::Ok) {
            // (the colours: the library's colour part - its first)
            for (int i = l.first_item(); i < l.count; ++i)
                if (hlib::entry(src, l, i, pos, len) && hlpic::load_colours(src, pos, len, q_col(), vga_load())) {
                    vga_take();
                    break;
                }
            const int i = hlib::find(src, l, static_cast<uint16_t>(id));
            if (i >= 0 && hlib::entry(src, l, i, pos, len) && hlib::read_part(src, pos, len, p) && p.drawable() &&
                p.width() <= hlib::kMaxWidth) {
                const int pw = p.width(), ph = p.height;
                const uint32_t nb = static_cast<uint32_t>(rowb(pw)) * ph;
                out = static_cast<uint8_t*>(calloc(1, nb + mask_bytes(pw, ph)));
                hlib::Pixels* px = out ? new (std::nothrow) hlib::Pixels(src, p) : nullptr;
                uint8_t* row = px ? static_cast<uint8_t*>(malloc(2 * hlib::kMaxWidth)) : nullptr;
                bool ok = px && row && px->ok();
                uint8_t* m = out ? out + nb : nullptr;
                for (int y = 0; ok && y < ph; ++y) {
                    ok = px->row(row, row + hlib::kMaxWidth);
                    for (int x = 0; ok && x < pw; ++x) {
                        if (!row[hlib::kMaxWidth + x]) {
                            const int k = y * pw + x;
                            m[k >> 3] = static_cast<uint8_t>(m[k >> 3] | 1 << (k & 7));
                        } else {
                            const uint8_t v = row[x];
                            set_nib(out, pw, x, y, p.ega() ? v & 15 : px8() ? v : q_col().ega[v]);
                        }
                    }
                }
                free(row);
                delete px;
                if (!ok) {
                    free(out);
                    out = nullptr;
                } else {
                    *w = pw;
                    *h = ph;
                }
            }
        }
    }
    f.close();
    return out;
}

// The field's pictures: DUNGCOM.TLB's picture of the look under a roof
// (the square's flags 0x80), else WILDCOM's - the other when that one has
// none; its parts 1.. the ground table's pictures 0..
__attribute__((noinline)) void q_load_tiles()
{
    const bool roof = geo::flags(d->map, d->gs.x, d->gs.y) >= 0x80;
    const int look = d->save.look & 0xFF;
    const char* files[2] = {roof ? "DUNGCOM.TLB" : "WILDCOM.TLB", roof ? "WILDCOM.TLB" : "DUNGCOM.TLB"};
    for (const char* name : files) {
        plat::File f;
        if (!open_hl(name, f)) continue;
        bool done = false;
        {
            plat::FileSource src(f);
            uint32_t pos, len;
            hlib::Library lib;
            if (hlib::find_entry(src, static_cast<uint16_t>(look), pos, len) &&
                hlib::open(src, pos, len, lib) == hlib::Status::Ok) {
                uint32_t cp, cl;
                if (hlib::entry(src, lib, 0, cp, cl)) {
                    hlpic::load_colours(src, cp, cl, q_col(), vga_load());
                    vga_take();
                }
                for (int k = 0; k < kTiles && k + 1 < lib.count; ++k) {
                    hlib::Part p;
                    if (!hlib::entry(src, lib, k + 1, cp, cl) || !hlib::read_part(src, cp, cl, p) || p.width() != kSq ||
                        p.height != kSq)
                        continue;
                    hlib::Pixels* px = new (std::nothrow) hlib::Pixels(src, p);
                    uint8_t row[kSq];
                    for (int y = 0; px && px->ok() && y < kSq && px->row(row); ++y)
                        for (int x = 0; x < kSq; ++x)
                            set_nib(fg->tile[k], kSq, x, y, p.ega() ? row[x] & 15 : px8() ? row[x] : q_col().ega[row[x]]);
                    delete px;
                }
                done = true;
                plat::log("[fight] field pictures: %s %d\n", name, look);
            }
        }
        f.close();
        if (done) return;
    }
}

// The fight's screen (DOSBox): the marble, the field's box 8-175 x 8-175,
// the panel's box 182-311 x 8-175; the bar below for the menu / prompts
__attribute__((noinline)) void q_draw_fight_frame(pic::Canvas& c)
{
    static const QBox kBoxes[] = {{8, 8, 175, 175}, {182, 8, 311, 175}};
    q_screen(c, kBoxes, 2, false);
    q_bar(c);
}

// A line over a one-row menu (Aim's "AIM: RANGE = 5"): white letters on
// the buttons' colour from x 8, y 177-185 (no edge - DOSBox); the marble
// under it comes back with the next menu
void q_line_above(pic::Canvas& c, const char* t)
{
    if (!t || !*t) return;
    q_strip_save(c);
    const int n = static_cast<int>(strlen(t));
    c.fill(8, 177, n * 8, 9, q_buttons().b_fill);
    for (int i = 0; i < n && i < 38; ++i)
        font::draw_glyph(c, d->font, font::glyph_of(d->font, t[i]), 8 + i * 8, 177, 15, q_buttons().b_fill);
    dirty_rows(text::kMenuRow - 2, text::kMenuRow - 1);
}

// The status line (row 23 in the other games): a prompt box on the bar
void q_status(pic::Canvas& c, const char* t)
{
    if (menu_on) return;                // (the menu has the bar)
    q_bar(c);
    if (t && *t) {
        text::Menu m;
        text::build(m, t, "");
        // (its fights' status: white letters in a black-edged box of the
        // buttons' colour - "GUARDING", DOSBox)
        text::MenuColors mc = q_buttons();
        mc.b_box = mc.b_fill;
        mc.b_box_ink = 15;
        text::draw(c, d->font, m, mc);
    }
    dirty_rows(text::kMenuRow - 1, text::kMenuRow);
}

// ---- Create New Character (queen_facts_4.md 5) -------------------------------
// One screen of four choices (DOSBox): the grey box 8-311 x 8-183 on the
// marble, red headings, grey names each beside a round button (7 x 7: a
// black ring - grey when it can't be picked - white inside, a red dot when
// picked): PICK RACE (12, 12; races from y 24 every 8), PICK ALIGNMENT
// (12, 88; LAWFUL NEUTRAL CHAOTIC from 100, GOOD NEUTRAL from 128), PICK
// GENDER (12, 152; from 164), PICK CLASS (148, 12; its 16 classes - no
// monk - from 24); the names 12 pixels right of their buttons; Done Exit
// on the bar. A race picks the classes it allows (the class falls back to
// the first one allowed); a knight or paladin is lawful only; a good /
// neutral the alignment allows with the law picked; an alignment the class
// can't have becomes the class's default (thieves' true neutral, else
// lawful good). Then a cleric's god (by the alignment: good Paladine ...
// Mishakal, neutral Sirrion, Reorx, Shinare; the dwarves' Reorx unasked),
// the roll and its screen with Reroll Stats Modify Done Exit.

constexpr uint16_t kQWords = 0x72C7;          // DS offset of the words' block (Making::q_words)
const char* q_word(uint16_t ds)
{
    if (!mk || ds < kQWords || ds >= kQWords + sizeof mk->q_words) return "";
    return mk->q_words + (ds - kQWords);
}
// (the classes' names in its order, 17 one after another from 0x738A)
const char* q_class_name(int q)
{
    uint16_t at = 0x738A;
    for (int i = 0; i < q && at < kQWords + sizeof mk->q_words; ++i) at = static_cast<uint16_t>(at + strlen(q_word(at)) + 1);
    return q_word(at);
}
const char* q_race_name(int r)
{
    uint16_t at = 0x7330;
    for (int i = 0; i < r; ++i) at = static_cast<uint16_t>(at + strlen(q_word(at)) + 1);
    return q_word(at);
}

int q_ecls(int q) { return q == 1 ? 7 : q == 7 ? 1 : q; }
bool q_dwarf() { return mk->q_race == 3 || mk->q_race == 4; }
bool q_class_ok(int q)
{
    int list[17];
    const int n = create::classes_for(mk->tables, classes::kKrynnRace + mk->q_race, list, 17);
    for (int i = 0; i < n; ++i)
        if (list[i] == q_ecls(q)) return true;
    return false;
}
bool q_align_ok(int law, int good)
{
    if (good < 1 || good > 2 || law < 1 || law > 3) return false;
    static const uint8_t kBit[9] = {0, 1, 0xFF, 2, 3, 0xFF, 4, 5, 0xFF};
    const int a = (law - 1) * 3 + good - 1, bit = kBit[a];
    if (bit == 0xFF) return false;
    const uint8_t m = mk->tables.u8(static_cast<uint16_t>(mk->tables.lay.class_alignments + q_ecls(mk->q_cls) + (q_dwarf() ? 17 : 0)));
    return (m >> bit) & 1;
}
bool q_law_ok(int law) { return mk->q_cls == 1 || mk->q_cls == 3 ? law == 1 : true; }

// The alignment kept to what the class allows (its +0x2323 / +0x2532)
void q_fix_alignment()
{
    if (q_align_ok(mk->q_law, mk->q_good)) return;
    for (int g = 1; g <= 2; ++g)
        if (q_align_ok(mk->q_law, g)) {
            mk->q_good = g;
            return;
        }
    const int q = mk->q_cls;
    const bool tn = q == 6 || q == 12 || q == 14 || q == 15 || q == 16;
    mk->q_law = mk->q_good = tn ? 2 : 1;
}
void q_fix_class()
{
    if (!q_class_ok(mk->q_cls))
        for (int q = 0; q <= 16; ++q)
            if (q != 7 && q_class_ok(q)) {
                mk->q_cls = q;
                break;
            }
    q_fix_alignment();
}

// The form's choices as a list: 0-6 races, 7-9 law, 10-11 good, 12-13
// gender, 14-29 classes; each one's button place and whether it can be picked
constexpr int kQChoices = 30;
void q_choice(int i, int* x, int* y)
{
    if (i < 7) *x = 12, *y = 24 + 8 * i;
    else if (i < 10) *x = 12, *y = 100 + 8 * (i - 7);
    else if (i < 12) *x = 12, *y = 128 + 8 * (i - 10);
    else if (i < 14) *x = 12, *y = 164 + 8 * (i - 12);
    else *x = 148, *y = 24 + 8 * (i - 14);
}
int q_choice_class(int i)
{
    const int j = i - 14;
    return j < 7 ? j : j + 1;
}
bool q_choice_on(int i)
{
    if (i < 7 || (i >= 12 && i < 14)) return true;
    if (i < 10) return q_law_ok(i - 6);
    if (i < 12) return q_align_ok(mk->q_law, i - 9);
    return q_class_ok(q_choice_class(i));
}
bool q_choice_picked(int i)
{
    if (i < 7) return mk->q_race == i;
    if (i < 10) return mk->q_law == i - 6;
    if (i < 12) return mk->q_good == i - 9;
    if (i < 14) return mk->q_sex == i - 12;
    return mk->q_cls == q_choice_class(i);
}
const char* q_choice_name(int i)
{
    if (i < 7) return q_race_name(i);
    if (i < 10) return q_word(i == 7 ? 0x7458 : i == 8 ? 0x745F : 0x7467);
    if (i < 12) return q_word(i == 10 ? 0x746F : 0x745F);
    if (i < 14) return q_word(i == 12 ? 0x737E : 0x7383);
    return q_class_name(q_choice_class(i));
}

void q_text(pic::Canvas& c, const char* s, int x, int y, uint8_t fg, uint8_t bg = 0)
{
    for (int k = 0; s[k] && x + k * 8 < pic::kScreenW; ++k)
        font::draw_glyph(c, d->font, font::glyph_of(d->font, s[k]), x + k * 8, y, fg, bg);
}

// A round button (7 x 7): its ring (black, grey when it can't be picked),
// white inside, the red dot when picked (light red corners, dark red cross)
void q_radio(pic::Canvas& c, int x, int y, bool on, bool picked)
{
    const uint8_t ring = on ? 0 : 7;
    for (int k = 2; k <= 4; ++k) {
        c.set(x + k, y, ring);
        c.set(x + k, y + 6, ring);
        c.set(x, y + k, ring);
        c.set(x + 6, y + k, ring);
    }
    c.set(x + 1, y + 1, ring);
    c.set(x + 5, y + 1, ring);
    c.set(x + 1, y + 5, ring);
    c.set(x + 5, y + 5, ring);
    c.fill(x + 2, y + 1, 3, 1, 15);
    c.fill(x + 1, y + 2, 5, 3, 15);
    c.fill(x + 2, y + 5, 3, 1, 15);
    if (picked) {
        for (int dx : {2, 4})
            for (int dy : {2, 4}) c.set(x + dx, y + dy, 12);
        c.set(x + 3, y + 2, 4);
        c.fill(x + 2, y + 3, 3, 1, 4);
        c.set(x + 3, y + 4, 4);
    }
}

void q_form_draw(pic::Canvas& c)
{
    q_draw_frame(c, false);
    q_pm_box(c, 183);
    q_bar(c);
    q_text(c, q_word(0x7300), 12, 12, 12);
    q_text(c, q_word(0x730A), 12, 88, 12);
    q_text(c, q_word(0x7319), 12, 152, 12);
    q_text(c, q_word(0x7325), 148, 12, 12);
    for (int i = 0; i < kQChoices; ++i) {
        int x, y;
        q_choice(i, &x, &y);
        q_radio(c, x, y, q_choice_on(i), q_choice_picked(i));
        const bool cur = i == mk->q_cur && keys_used;
        q_text(c, q_choice_name(i), x + 12, y, cur ? 0 : 7, cur ? 15 : 0);
    }
    char words[16];
    snprintf(words, sizeof words, "%s %s", q_word(0x72CE), q_word(0x72D3));
    text::build(menu, "", words);
    menu.selected = 0;
    hl_first();
    show_menu_line(c);
    dirty(0, pic::kScreenH);
}

// The words (Done ... the names) and the gods' names from the program
__attribute__((noinline)) bool q_create_words()
{
    plat::File f;
    if (!open_file(d->prof->program, f)) return false;
    bool ok = false;
    {
        plat::FileSource src(f);
        exepack::Info info;
        ok = exepack::parse(src, info) == exepack::Status::Ok &&
             exepack::read(src, info, d->prof->data_base + kQWords, reinterpret_cast<uint8_t*>(mk->q_words),
                           sizeof mk->q_words - 1) == exepack::Status::Ok;
        // (its gods: far pointers DS 0x3C0A, 0 None; "Pick Diety" in the
        // words' far table DS 0x3CE6; "Select Exit" DS 0x4EF1)
        for (int g = 1; ok && g < 8; ++g)
            q_far_word(src, info, d->prof->data_base + 0x3C0A, g, mk->q_gods[g], sizeof mk->q_gods[0]);
        q_far_word(src, info, d->prof->data_base + 0x3CE6, 0, mk->pick_god, sizeof mk->pick_god);
        uint8_t raw[16] = {};
        if (exepack::read(src, info, d->prof->data_base + 0x4EF1, raw, sizeof raw - 1) == exepack::Status::Ok)
            snprintf(mk->words[4], sizeof mk->words[4], "%s", reinterpret_cast<const char*>(raw));
    }
    f.close();
    mk->q_words[sizeof mk->q_words - 1] = 0;
    return ok;
}

void q_form_start(pic::Canvas& c)
{
    if (!load_making() || !q_create_words()) {
        delete mk;
        mk = nullptr;
        pm_prompt(c, "Not in the engine yet.");
        return;
    }
    // (its start: human, fighter, lawful good, male - DOSBox)
    mk->q_race = 6;
    mk->q_cls = 2;
    mk->q_law = mk->q_good = 1;
    mk->q_sex = 0;
    mk->q_cur = 6;
    screen = Screen::QForm;
    q_form_draw(c);
}

void q_pick(int i)
{
    if (i < 0 || i >= kQChoices || !q_choice_on(i)) return;
    if (i < 7) {
        mk->q_race = i;
        q_fix_class();
    } else if (i < 10) {
        mk->q_law = i - 6;
        q_fix_alignment();
    } else if (i < 12) {
        mk->q_good = i - 9;
    } else if (i < 14) {
        mk->q_sex = i - 12;
    } else {
        mk->q_cls = q_choice_class(i);
        q_fix_alignment();
    }
}

void q_rolled(pic::Canvas& c);
void q_god_draw(pic::Canvas& c);

// Done: a cleric's god first (not a dwarf's), then the character
void q_form_done(pic::Canvas& c)
{
    const int q = mk->q_cls;
    const bool cleric = q == 0 || (q >= 8 && q <= 12);
    mk->god = cleric && q_dwarf() ? 6 : 0;
    if (cleric && !q_dwarf()) {
        const bool good = mk->q_good == 1;
        mk->q_n_gods = good ? 4 : 3;
        for (int k = 0; k < mk->q_n_gods; ++k) mk->q_god_opt[k] = good ? 1 + k : 5 + k;
        mk->q_god_sel = 0;
        screen = Screen::QGod;
        q_god_draw(c);
        return;
    }
    q_rolled(c);
}

void q_form_tap(int x, int y, pic::Canvas& c)
{
    if (y >= text::kMenuTapTop) {
        const int k = text::hit(menu, x / 8);
        if (k < 0) return;
        menu.selected = k;
        if (k == 0) q_form_done(c);
        else end_create(c);
        return;
    }
    for (int i = 0; i < kQChoices; ++i) {
        int bx, by;
        q_choice(i, &bx, &by);
        const int w = 12 + 8 * static_cast<int>(strlen(q_choice_name(i)));
        if (x >= bx - 2 && x < bx + w && y >= by - 1 && y < by + 8) {
            mk->q_cur = i;
            q_pick(i);
            q_form_draw(c);
            return;
        }
    }
}

// The cursor keys: up / down through the choices (those that can be picked)
bool q_form_nav(int step, pic::Canvas& c)
{
    int i = mk->q_cur < 0 ? 0 : mk->q_cur;
    for (int k = 0; k < kQChoices; ++k) {
        i = (i + step + kQChoices) % kQChoices;
        if (q_choice_on(i)) break;
    }
    mk->q_cur = i;
    q_form_draw(c);
    return true;
}

bool q_form_point(int* x, int* y)
{
    if (mk->q_cur < 0) return false;
    q_choice(mk->q_cur, x, y);
    *x += 14;
    *y += 3;
    return true;
}

// ---- Pick Diety (DOSBox): its heading red at (8, 8), the gods from y 16
// grey, the one picked cyan; Select Exit on the bar
void q_god_draw(pic::Canvas& c)
{
    q_draw_frame(c, false);
    q_pm_box(c, 183);
    q_bar(c);
    q_text(c, mk->pick_god, 8, 8, 12);
    for (int k = 0; k < mk->q_n_gods; ++k)
        q_text(c, mk->q_gods[mk->q_god_opt[k]], 16, 16 + 8 * k, k == mk->q_god_sel ? 11 : 7);
    text::build(menu, "", mk->words[4]);
    menu.selected = 0;
    hl_first();
    show_menu_line(c);
    dirty(0, pic::kScreenH);
}

void q_god_tap(int x, int y, pic::Canvas& c)
{
    if (y >= text::kMenuTapTop) {
        const int k = text::hit(menu, x / 8);
        if (k < 0) return;
        if (text::key(menu, k) == 'S') {
            mk->god = mk->q_god_opt[mk->q_god_sel];
            q_rolled(c);
        } else {
            screen = Screen::QForm;
            q_form_draw(c);
        }
        return;
    }
    const int k = (y - 16) / 8;
    if (y >= 16 && k < mk->q_n_gods) {
        mk->q_god_sel = k;
        q_god_draw(c);
    }
}

bool q_god_nav(int step, pic::Canvas& c)
{
    mk->q_god_sel = (mk->q_god_sel + step + mk->q_n_gods) % mk->q_n_gods;
    q_god_draw(c);
    return true;
}

// The new character rolled (its +0x1F25 and +0x1A90) and shown: View
// Character's screen with "Reroll Stats Modify Done Exit"
void q_rolled_menu(pic::Canvas& c)
{
    // (one button a choice: "Reroll Stats" has a space - text::build's marks)
    char words[48];
    snprintf(words, sizeof words, "~%s ~%s ~%s ~%s", q_word(0x72E8), q_word(0x72F5), q_word(0x72CE), q_word(0x72D3));
    text::build(menu, "", words);
    menu.selected = 0;
    hl_first();
    show_menu_line(c);
}

void q_rolled(pic::Canvas& c)
{
    const int align = (mk->q_law - 1) * 3 + mk->q_good - 1;
    create::begin(mk->ch, mk->tables, mk->facts, mk->dice, classes::kKrynnRace + mk->q_race, mk->q_sex,
                  q_ecls(mk->q_cls), align, mk->god);
    create::roll(mk->ch, mk->tables, mk->facts, mk->dice);
    mk->ch.rec[0x142] = static_cast<uint8_t>(create::queen_icon(mk->q_sex, mk->q_race, mk->q_cls, align));
    rules::recalc(mk->ch, *names, d->facts);
    screen = Screen::QRolled;
    show_new_character(c);
    q_rolled_menu(c);
}

void ask_name(pic::Canvas& c);
void q_rolled_tap(int x, int y, pic::Canvas& c)
{
    if (y < text::kMenuTapTop) return;
    const int k = text::hit(menu, x / 8);
    if (k < 0) return;
    menu.selected = k;
    switch (text::key(menu, k)) {
    case 'R':
        create::roll(mk->ch, mk->tables, mk->facts, mk->dice);
        rules::recalc(mk->ch, *names, d->facts);
        show_new_character(c);
        q_rolled_menu(c);
        break;
    case 'M': error(c, "Not in the engine yet."); break;
    case 'D': ask_name(c); break;
    case 'E': end_create(c); break;
    default: break;
    }
}

// ---- A new character's kit and its file (queen_facts_4.md 7) ---------------
// The kit (its save, ovl 5 +0x34E4): five item numbers by race and class
// (DS 0x180D + race x 170 + class x 10, u16 each; 0 none) - item n is
// ITEM.DAT's row n - 1 (17 bytes) -, each put at the head of the items;
// numbers 0x50, 0x56 and 0x5B (arrows, quarrels, darts) 40 of them; the
// first weapon (the type's slot 0) readied, the rest but numbers 0x52-0x54,
// 0x56 and 0x5D readied too
__attribute__((noinline)) void q_kit(party::Character& ch)
{
    const int qrace = ch.ext[party::kExtRace] % 7, qcls = ch.cls() == 7 ? 1 : ch.cls() == 1 ? 7 : ch.cls();
    if (qcls < 0 || qcls > 16) return;
    uint8_t ids[10] = {};
    {
        plat::File f;
        if (!open_file(d->prof->program, f)) return;
        plat::FileSource src(f);
        exepack::Info info;
        const bool ok = exepack::parse(src, info) == exepack::Status::Ok &&
                        exepack::read(src, info, d->prof->data_base + 0x180D + qrace * 170 + qcls * 10, ids, 10) ==
                            exepack::Status::Ok;
        f.close();
        if (!ok) return;
    }
    plat::File f;
    if (!open_hl("ITEM.DAT", f)) return;
    {
        plat::FileSource src(f);
        bool weapon = false;
        for (int k = 0; k < 5; ++k) {
            const int n = ids[2 * k];
            uint8_t row[17];
            if (n == 0 || src.read_at(static_cast<uint32_t>(n - 1) * 17, row, sizeof row) != sizeof row || row[0] == 0xFF ||
                ch.n_items >= party::kMaxItems)
                continue;
            for (int j = ch.n_items; j > 0; --j) memcpy(ch.items[j], ch.items[j - 1], party::kItemSize);
            ++ch.n_items;
            uint8_t* it = ch.items[0];
            party::queen_item_in(row, it);
            it[0x35] &= 0xF8;                    // (its byte 11, the hidden words: the 63-byte record's 0x35)
            if (n == 0x50 || n == 0x56 || n == 0x5B) it[0x39] = 40;
            it[0x34] = 0;
            if (n == 0x52 || n == 0x53 || n == 0x54 || n == 0x56 || n == 0x5D) continue;
            if (names && names->type(it[0x2E]).slot != items::kSlotWeapon) {
                it[0x34] = 1;
            } else if (!weapon) {
                weapon = true;
                it[0x34] = 1;
            }
        }
    }
    f.close();
    for (int k = 0; k < ch.n_items; ++k)
        if (ch.items[k][0x34]) rules::worn(ch, k, true);
    rules::recalc(ch, *names, d->facts);
}

// NAME.QCH in the save folder: the record (over the file's own when there
// is one: the fields the engine doesn't keep stay), its items, its effects
__attribute__((noinline)) bool q_write_char(const char* base, const party::Character& ch)
{
    char fn[24], path[200];
    snprintf(fn, sizeof fn, "%s.QCH", base);
    save_path(fn, path, sizeof path);
    uint8_t* old = nullptr;
    size_t n_old = 0;
    {
        plat::File in = plat::open(path, "r");
        if (in) {
            n_old = in.size();
            old = n_old >= party::kQueenRecordSize && n_old < 8192 ? new (std::nothrow) uint8_t[n_old] : nullptr;
            if (old && in.read(old, n_old) != static_cast<int>(n_old)) {
                delete[] old;
                old = nullptr;
            }
            in.close();
        }
    }
    plat::File out = plat::open(path, "w");
    bool ok = static_cast<bool>(out);
    if (ok) {
        dax::MemorySource src(old, old ? n_old : 0);
        FileSink sink(out);
        ok = savegame::write_queen_char(sink, ch, old ? &src : nullptr);
        out.close();
    }
    delete[] old;
    return ok;
}

// ---- Its shops' goods (queen_facts_4.md 9) ---------------------------------
// SHOP n: the u16 list entries first[n] .. last[n] (tables of 9 at DS
// goods_first / goods_last), each an ITEM.DAT row (1-based, 17 bytes); 255
// a random magic-user scroll (w6 +0x1A83): type 0x27, words 0x66 0x27 and
// 0x8A - d for a d2 (the robe: 1 White, 2 Red / Black), plus 1, weight 1,
// value 3000, byte 11 the robe's bit (1 << (6 - d)), three magic-user spells
// of that robe (DS spell_class + s: 3 either robe) - rolled d126 until one fits
__attribute__((noinline)) void q_shop_items(int n, items::Ground& g)
{
    const auto& pi = d->prof->items;
    if (n < 1 || n > 9) n = 1;
    uint8_t ab[4];
    if (!program_ds(static_cast<uint16_t>(pi.goods_first + 2 * (n - 1)), ab, 2) ||
        !program_ds(static_cast<uint16_t>(pi.goods_last + 2 * (n - 1)), ab + 2, 2)) {
        plat::log("[play] SHOP: the program's goods didn't read\n");
        return;
    }
    const int a = ab[0] | ab[1] << 8, b = ab[2] | ab[3] << 8;
    if (a < 1 || b < a || b - a > 64) return;
    uint8_t list[130];
    if (!program_ds(static_cast<uint16_t>(pi.goods_rows + 2 * (a - 1)), list, 2u * static_cast<size_t>(b - a + 1))) return;
    uint8_t robes[140] = {};
    uint8_t* cls = nullptr;     // (the spells' class bytes, only for a random scroll)
    plat::File f;
    if (!open_hl("ITEM.DAT", f)) return;
    {
        plat::FileSource src(f);
        for (int k = 0; k <= b - a; ++k) {
            const int r = list[2 * k] | list[2 * k + 1] << 8;
            uint8_t row[17] = {};
            if (r == 255) {
                if (!cls) {
                    cls = new (std::nothrow) uint8_t[127];
                    if (!cls || !program_ds(pi.spell_class, robes, sizeof robes)) break;
                    for (int s = 1; s < 127; ++s) {
                        uint8_t c2[1] = {0};
                        cls[s] = program_ds(static_cast<uint16_t>(d->prof->create.tables.spells + 22 * s), c2, 1) ? c2[0] : 0;
                    }
                }
                const int robe = rng.roll(2, 1);
                row[0] = 0x27, row[1] = 0x66, row[2] = 0x27, row[3] = static_cast<uint8_t>(0x8A - robe);
                row[4] = 1, row[6] = 3000 & 0xFF, row[7] = 3000 >> 8, row[8] = 1;
                row[11] = static_cast<uint8_t>(1 << (6 - robe));
                for (int j = 0; j < 3; ++j) {
                    int s;
                    do s = rng.roll(126, 1);
                    while (cls[s] != 2 || (robes[s] != 3 && robes[s] != robe));
                    row[14 + j] = static_cast<uint8_t>(s);
                }
            } else if (r < 1 || src.read_at(static_cast<uint32_t>(r - 1) * 17, row, sizeof row) != sizeof row ||
                       row[0] == 0xFF) {
                continue;
            }
            if (g.n >= items::kMaxGround) break;
            uint8_t* it = g.item[g.n++];
            party::queen_item_in(row, it);
            it[0x35] &= 0xF8;
        }
    }
    f.close();
    delete[] cls;
    plat::log("[play] SHOP %d: %d items\n", n, g.n);
}
