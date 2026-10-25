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

// The status line (row 23 in the other games): a prompt box on the bar
void q_status(pic::Canvas& c, const char* t)
{
    if (menu_on) return;                // (the menu has the bar)
    q_bar(c);
    if (t && *t) {
        text::Menu m;
        text::build(m, t, "");
        text::draw(c, d->font, m, q_buttons());
    }
    dirty_rows(text::kMenuRow - 1, text::kMenuRow);
}
