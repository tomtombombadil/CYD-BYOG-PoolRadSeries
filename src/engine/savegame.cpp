#include "savegame.h"

#include <new>

#include <cstdio>
#include <cstring>

namespace savegame {

bool read(dax::ByteSource& src, ecl::GameState& gs, Header& h, bool wall_sets, bool script)
{
    const uint32_t need = (wall_sets ? kSize : kPoolSize) - (script ? 0 : ecl::kCodeSize);
    if (src.size() < need) return false;
    h = Header{};
    uint32_t p = 0;
    auto take = [&](uint8_t* out, size_t n) {
        const bool ok = src.read_at(p, out, n) == n;
        p += static_cast<uint32_t>(n);
        return ok;
    };
    uint8_t b[12];
    if (!take(b, 1)) return false;
    h.game_area = b[0];
    if (!take(gs.area1, sizeof gs.area1) || !take(gs.area2, sizeof gs.area2) || !take(gs.table, sizeof gs.table) ||
        (script && !take(gs.code, sizeof gs.code)))
        return false;
    // (Secret saves no script: it's loaded again from the area word 0x4BF2)
    gs.code_len = script ? ecl::kCodeSize : 0;
    if (!take(b, 5)) return false;
    gs.x = b[0] & 15;
    gs.y = b[1] & 15;
    gs.dir = b[2] & 6;
    gs.wall_ahead = b[3];
    gs.roof = b[4];
    if (!take(b, 2)) return false;
    h.last_state = b[0];
    h.state = b[1];
    if (wall_sets) {
        if (!take(b, 12)) return false;
        for (int i = 0; i < 3; ++i) {
            h.wall_block[i] = static_cast<int16_t>(b[i * 4] | b[i * 4 + 1] << 8);
            h.wall_set[i] = static_cast<int16_t>(b[i * 4 + 2] | b[i * 4 + 3] << 8);
        }
    }
    if (!take(b, 1)) return false;
    int n = b[0];
    if (n > party::kMaxParty) n = party::kMaxParty;
    for (int i = 0; i < n; ++i) {
        uint8_t s[41];
        if (src.read_at(p + i * 41u, s, sizeof s) != sizeof s) return false;
        size_t len = s[0];
        if (len > 40) len = 40;
        memcpy(h.names[i], s + 1, len);
        h.names[i][len] = 0;
    }
    h.count = n;
    gs.game_area = h.game_area;
    gs.moved = false;
    return true;
}

bool write(Sink& out, const ecl::GameState& gs, const Header& h, bool wall_sets, bool script)
{
    uint8_t b[12];
    b[0] = h.game_area;
    if (!out.put(b, 1) || !out.put(gs.area1, sizeof gs.area1) || !out.put(gs.area2, sizeof gs.area2) ||
        !out.put(gs.table, sizeof gs.table) || (script && !out.put(gs.code, sizeof gs.code)))
        return false;
    b[0] = static_cast<uint8_t>(gs.x);
    b[1] = static_cast<uint8_t>(gs.y);
    b[2] = static_cast<uint8_t>(gs.dir);
    b[3] = gs.wall_ahead;
    b[4] = gs.roof;
    if (!out.put(b, 5)) return false;
    b[0] = h.last_state;
    b[1] = h.state;
    if (!out.put(b, 2)) return false;
    for (int i = 0; i < 3; ++i) {
        b[i * 4] = static_cast<uint8_t>(h.wall_block[i]);
        b[i * 4 + 1] = static_cast<uint8_t>(static_cast<uint16_t>(h.wall_block[i]) >> 8);
        b[i * 4 + 2] = static_cast<uint8_t>(h.wall_set[i]);
        b[i * 4 + 3] = static_cast<uint8_t>(static_cast<uint16_t>(h.wall_set[i]) >> 8);
    }
    if (wall_sets && !out.put(b, 12)) return false;
    b[0] = static_cast<uint8_t>(h.count);
    if (!out.put(b, 1)) return false;
    uint8_t names[8 * 41] = {};
    for (int i = 0; i < h.count && i < 8; ++i) {
        const size_t n = strnlen(h.names[i], 40);
        names[i * 41] = static_cast<uint8_t>(n);
        memcpy(names + i * 41 + 1, h.names[i], n);
    }
    return out.put(names, sizeof names);
}

namespace {
bool names_in(dax::ByteSource& src, uint32_t p, Header& h)
{
    uint8_t b;
    if (src.read_at(p, &b, 1) != 1) return false;
    int n = b;
    if (n > party::kMaxParty) n = party::kMaxParty;
    for (int i = 0; i < n; ++i) {
        uint8_t s[41];
        if (src.read_at(p + 1 + i * 41u, s, sizeof s) != sizeof s) return false;
        size_t len = s[0];
        if (len > 40) len = 40;
        memcpy(h.names[i], s + 1, len);
        h.names[i][len] = 0;
    }
    h.count = n;
    return true;
}
bool names_out(Sink& out, const Header& h)
{
    uint8_t b = static_cast<uint8_t>(h.count);
    if (!out.put(&b, 1)) return false;
    uint8_t names[8 * 41] = {};
    for (int i = 0; i < h.count && i < 8; ++i) {
        const size_t n = strnlen(h.names[i], 40);
        names[i * 41] = static_cast<uint8_t>(n);
        memcpy(names + i * 41 + 1, h.names[i], n);
    }
    return out.put(names, sizeof names);
}
} // namespace

bool read_darkness(dax::ByteSource& src, ecl::GameState& gs, Header& h)
{
    if (src.size() < kDarknessSize) return false;
    h = Header{};
    if (src.read_at(0, gs.area1, kDarknessBlock) != kDarknessBlock) return false;
    uint8_t b[11];
    if (src.read_at(kDarknessBlock, b, sizeof b) != sizeof b) return false;
    gs.x = b[0] & 15;
    gs.y = b[1] & 15;
    gs.dir = b[2] & 7;
    gs.wall_ahead = b[3];
    gs.roof = b[4];
    h.last_state = b[5];
    h.state = b[6];
    h.look = static_cast<uint16_t>(b[7] | b[8] << 8);
    h.look_slot = static_cast<uint16_t>(b[9] | b[10] << 8);
    gs.code_len = 0;
    gs.moved = false;
    gs.game_area = h.game_area = 1;         // (one file a kind: ECL1 ...)
    return names_in(src, kDarknessBlock + 11, h);
}

bool read_queen(dax::ByteSource& src, ecl::GameState& gs, Header& h, party::Party& p)
{
    if (src.size() < kQueenHead) return false;
    h = Header{};
    p.clear();
    if (src.read_at(0, gs.area1, kDarknessBlock) != kDarknessBlock) return false;
    uint8_t b[15];
    if (src.read_at(kDarknessBlock, b, sizeof b) != sizeof b) return false;
    gs.x = b[0];
    gs.y = b[1];
    gs.dir = b[2] & 7;
    gs.wall_ahead = b[3];
    gs.roof = b[4];
    h.last_state = b[5];
    h.state = b[6];
    h.look = static_cast<uint16_t>(b[7] | b[8] << 8);
    h.look_slot = static_cast<uint16_t>(b[9] | b[10] << 8);
    const int count = b[13] | b[14] << 8;
    if (count > party::kMaxParty) return false;
    gs.code_len = 0;
    gs.moved = false;
    gs.game_area = h.game_area = 1;
    uint32_t at = kQueenHead;
    uint8_t* rec = new (std::nothrow) uint8_t[party::kQueenRecordSize];
    if (!rec) return false;
    bool ok = true;
    for (int i = 0; ok && i < count; ++i) {
        ok = src.read_at(at, rec, party::kQueenRecordSize) == party::kQueenRecordSize;
        if (!ok) break;
        at += party::kQueenRecordSize;
        party::Character& c = p.m[p.count];
        party::from_queen(rec, c);
        c.name(h.names[i], sizeof h.names[i]);
        // its items (17 bytes each - party::queen_item_in)
        for (int k = 0; ok && k < rec[0xC9]; ++k) {
            uint8_t q[kQueenItem];
            ok = src.read_at(at, q, sizeof q) == sizeof q;
            at += kQueenItem;
            if (ok && c.n_items < party::kMaxItems) party::queen_item_in(q, c.items[c.n_items++]);
        }
        // its effects: a chain from the record's pointer (Curse's 9 bytes)
        bool more = rec[4] || rec[5] || rec[6] || rec[7];
        for (int k = 0; ok && more && k < 64; ++k) {
            uint8_t e[kQueenEffect];
            ok = src.read_at(at, e, sizeof e) == sizeof e;
            if (!ok) break;
            at += kQueenEffect;
            more = e[5] || e[6] || e[7] || e[8];
            if (c.n_affects < party::kMaxAffects) memcpy(c.affects[c.n_affects++], e, party::kAffectSize);
        }
        if (ok && at > src.size()) ok = false;
        if (ok) ++p.count;
    }
    delete[] rec;
    h.count = p.count;
    if (!ok) p.clear();
    return ok;
}

void file_name_queen(char slot, char* out, size_t cap) { snprintf(out, cap, "SAVGAM%c.QSV", slot); }

namespace {
// A QSV's records (where each starts) and where its rest starts; false: not one
bool queen_layout(dax::ByteSource& src, uint32_t* at, char (*names)[16], int* count, uint32_t* rest)
{
    uint8_t b[15];
    if (src.read_at(kDarknessBlock, b, sizeof b) != sizeof b) return false;
    const int n = b[13] | b[14] << 8;
    if (n > party::kMaxParty) return false;
    uint32_t p = kQueenHead;
    for (int i = 0; i < n; ++i) {
        uint8_t r[0xD0];
        if (src.read_at(p, r, sizeof r) != sizeof r) return false;
        at[i] = p;
        memcpy(names[i], r + 0x68, 15);
        names[i][15] = 0;
        p += party::kQueenRecordSize + kQueenItem * r[0xC9];
        bool more = r[4] || r[5] || r[6] || r[7];
        for (int k = 0; more && k < 64; ++k) {
            uint8_t e[kQueenEffect];
            if (src.read_at(p, e, sizeof e) != sizeof e) return false;
            p += kQueenEffect;
            more = e[5] || e[6] || e[7] || e[8];
        }
    }
    *count = n;
    *rest = p;
    return p <= src.size();
}
} // namespace

bool write_queen(Sink& out, const ecl::GameState& gs, const Header& h, const party::Party& p, dax::ByteSource* src)
{
    uint32_t at[party::kMaxParty] = {}, rest = 0;
    char names[party::kMaxParty][16] = {};
    int n_src = 0;
    if (src && !queen_layout(*src, at, names, &n_src, &rest)) src = nullptr;
    const uint8_t b[15] = {static_cast<uint8_t>(gs.x), static_cast<uint8_t>(gs.y), static_cast<uint8_t>(gs.dir),
                           gs.wall_ahead, gs.roof, h.last_state, h.state, static_cast<uint8_t>(h.look),
                           static_cast<uint8_t>(h.look >> 8), static_cast<uint8_t>(h.look_slot),
                           static_cast<uint8_t>(h.look_slot >> 8), 0, 0, static_cast<uint8_t>(p.count), 0};
    if (!out.put(gs.area1, kDarknessBlock) || !out.put(b, sizeof b)) return false;
    uint8_t* q = new (std::nothrow) uint8_t[party::kQueenRecordSize];
    if (!q) return false;
    bool ok = true;
    for (int i = 0; ok && i < p.count; ++i) {
        const party::Character& c = p.m[i];
        char nm[20];
        c.name(nm, sizeof nm);
        memset(q, 0, party::kQueenRecordSize);
        for (int k = 0; src && k < n_src; ++k)
            if (strcmp(names[k], nm) == 0) {
                if (src->read_at(at[k], q, party::kQueenRecordSize) != party::kQueenRecordSize)
                    memset(q, 0, party::kQueenRecordSize);
                break;
            }
        party::to_queen(c, q);
        ok = out.put(q, party::kQueenRecordSize);
        for (int k = 0; ok && k < c.n_items; ++k) {
            uint8_t it[kQueenItem];
            party::queen_item_out(c.items[k], it);
            ok = out.put(it, sizeof it);
        }
        for (int k = 0; ok && k < c.n_affects; ++k) {
            uint8_t e[kQueenEffect];
            memcpy(e, c.affects[k], kQueenEffect);
            // (the chain: a non-zero pointer but on the last)
            memset(e + 5, 0, 4);
            if (k + 1 < c.n_affects) e[8] = 0x5E;
            ok = out.put(e, sizeof e);
        }
    }
    delete[] q;
    // the rest, as it was
    for (uint32_t pos = rest; ok && src && pos < src->size();) {
        uint8_t buf[256];
        const uint32_t n = src->size() - pos < sizeof buf ? src->size() - pos : static_cast<uint32_t>(sizeof buf);
        ok = src->read_at(pos, buf, n) == n && out.put(buf, n);
        pos += n;
    }
    return ok;
}

bool write_darkness(Sink& out, const ecl::GameState& gs, const Header& h)
{
    const uint8_t b[11] = {static_cast<uint8_t>(gs.x), static_cast<uint8_t>(gs.y), static_cast<uint8_t>(gs.dir),
                           gs.wall_ahead, gs.roof, h.last_state, h.state, static_cast<uint8_t>(h.look),
                           static_cast<uint8_t>(h.look >> 8), static_cast<uint8_t>(h.look_slot),
                           static_cast<uint8_t>(h.look_slot >> 8)};
    return out.put(gs.area1, kDarknessBlock) && out.put(b, sizeof b) && names_out(out, h);
}

bool read_dkk(dax::ByteSource& src, ecl::GameState& gs, Header& h)
{
    if (src.size() < kDkkSize) return false;
    h = Header{};
    uint8_t b[24];
    uint32_t p = 0;
    auto take = [&](uint8_t* out, size_t n) {
        const bool ok = src.read_at(p, out, n) == n;
        p += static_cast<uint32_t>(n);
        return ok;
    };
    if (!take(b, 1)) return false;
    h.game_area = b[0];
    if (!take(gs.area1, 0x400) || !take(gs.area2, 0x400) || !take(gs.table, 0x200) || !take(b, 5 + 2 + 16))
        return false;
    gs.x = b[0] & 15;
    gs.y = b[1] & 15;
    gs.dir = b[2] & 7;
    gs.wall_ahead = b[3];
    gs.roof = b[4];
    h.last_state = b[5];
    h.state = b[6];
    int16_t blk[4], set[4];
    for (int i = 0; i < 4; ++i) {
        blk[i] = static_cast<int16_t>(b[7 + i * 4] | b[8 + i * 4] << 8);
        set[i] = static_cast<int16_t>(b[9 + i * 4] | b[10 + i * 4] << 8);
    }
    for (int i = 0; i < 3; ++i) {
        h.wall_block[i] = blk[i];
        h.wall_set[i] = set[i];
    }
    h.wall_block4 = blk[3];
    h.wall_set4 = set[3];
    h.look = static_cast<uint16_t>(blk[1] > 0 ? blk[1] : 0);    // (its scripts' looks go to slot 1)
    gs.code_len = 0;
    gs.moved = false;
    gs.game_area = h.game_area;
    return names_in(src, p, h);
}

bool write_dkk(Sink& out, const ecl::GameState& gs, const Header& h)
{
    uint8_t b[24];
    b[0] = h.game_area;
    if (!out.put(b, 1) || !out.put(gs.area1, 0x400) || !out.put(gs.area2, 0x400) || !out.put(gs.table, 0x200))
        return false;
    b[0] = static_cast<uint8_t>(gs.x);
    b[1] = static_cast<uint8_t>(gs.y);
    b[2] = static_cast<uint8_t>(gs.dir);
    b[3] = gs.wall_ahead;
    b[4] = gs.roof;
    b[5] = h.last_state;
    b[6] = h.state;
    for (int i = 0; i < 4; ++i) {
        const uint16_t blk = static_cast<uint16_t>(i < 3 ? h.wall_block[i] : h.wall_block4);
        const uint16_t set = static_cast<uint16_t>(i < 3 ? h.wall_set[i] : h.wall_set4);
        b[7 + i * 4] = static_cast<uint8_t>(blk);
        b[8 + i * 4] = static_cast<uint8_t>(blk >> 8);
        b[9 + i * 4] = static_cast<uint8_t>(set);
        b[10 + i * 4] = static_cast<uint8_t>(set >> 8);
    }
    return out.put(b, 5 + 2 + 16) && names_out(out, h);
}

void file_name_darkness(char slot, char* out, size_t cap)
{
    snprintf(out, cap, "SAVGAM%c.PTY", slot);
}

void char_file(char slot, int n, char* out, size_t cap)
{
    snprintf(out, cap, "CHRDAT%c%d", slot, n);
}

void file_name(char slot, char* out, size_t cap)
{
    snprintf(out, cap, "SAVGAM%c.DAT", slot);
}

void save_dir(const char* cfg, size_t len, char* out, size_t cap, int nth)
{
    if (!cap) return;
    out[0] = 0;
    int found = 0;
    size_t i = 0;
    while (i < len) {
        size_t e = i;
        while (e < len && cfg[e] != '\r' && cfg[e] != '\n') ++e;
        // A line naming a folder: has a backslash
        const char* line = cfg + i;
        const size_t n = e - i;
        if (memchr(line, '\\', n) && (nth < 0 || found++ == nth)) {
            size_t s = 0;
            if (n >= 2 && line[1] == ':') s = 2;
            while (s < n && (line[s] == '\\' || line[s] == '/')) ++s;
            size_t o = 0;
            for (size_t k = s; k < n && o + 1 < cap; ++k) out[o++] = line[k] == '\\' ? '/' : line[k];
            while (o > 0 && out[o - 1] == '/') --o;
            out[o] = 0;                     // (a later one wins: Pool names the game's folder first)
        }
        i = e;
        while (i < len && (cfg[i] == '\r' || cfg[i] == '\n')) ++i;
    }
}

bool drop_own_folder(const char* data_dir, char* sub)
{
    if (!data_dir || !sub) return false;
    size_t e = strlen(data_dir);
    while (e > 0 && (data_dir[e - 1] == '/' || data_dir[e - 1] == '\\')) --e;
    size_t b = e;
    while (b > 0 && data_dir[b - 1] != '/' && data_dir[b - 1] != '\\') --b;
    const size_t n = e - b;
    const size_t len = strlen(sub);
    if (!n || len < n || (len > n && (len == n + 1 || sub[n] != '/'))) return false;
    for (size_t i = 0; i < n; ++i) {
        const char a = sub[i], c = data_dir[b + i];
        if ((a >= 'a' && a <= 'z' ? a - 32 : a) != (c >= 'a' && c <= 'z' ? c - 32 : c)) return false;
    }
    if (len == n) sub[0] = 0;
    else memmove(sub, sub + n + 1, strlen(sub + n + 1) + 1);
    return true;
}

} // namespace savegame
