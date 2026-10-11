#include "savegame.h"

#include <cstdio>
#include <cstring>

namespace savegame {

bool read(dax::ByteSource& src, ecl::GameState& gs, Header& h)
{
    if (src.size() < kSize) return false;
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
        !take(gs.code, sizeof gs.code))
        return false;
    gs.code_len = ecl::kCodeSize;
    if (!take(b, 5)) return false;
    gs.x = b[0] & 15;
    gs.y = b[1] & 15;
    gs.dir = b[2] & 6;
    gs.wall_ahead = b[3];
    gs.roof = b[4];
    if (!take(b, 2)) return false;
    h.last_state = b[0];
    h.state = b[1];
    if (!take(b, 12)) return false;
    for (int i = 0; i < 3; ++i) {
        h.wall_block[i] = static_cast<int16_t>(b[i * 4] | b[i * 4 + 1] << 8);
        h.wall_set[i] = static_cast<int16_t>(b[i * 4 + 2] | b[i * 4 + 3] << 8);
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

bool write(Sink& out, const ecl::GameState& gs, const Header& h)
{
    uint8_t b[12];
    b[0] = h.game_area;
    if (!out.put(b, 1) || !out.put(gs.area1, sizeof gs.area1) || !out.put(gs.area2, sizeof gs.area2) ||
        !out.put(gs.table, sizeof gs.table) || !out.put(gs.code, sizeof gs.code))
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
    if (!out.put(b, 12)) return false;
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

void char_file(char slot, int n, char* out, size_t cap)
{
    snprintf(out, cap, "CHRDAT%c%d", slot, n);
}

void file_name(char slot, char* out, size_t cap)
{
    snprintf(out, cap, "SAVGAM%c.DAT", slot);
}

void save_dir(const char* cfg, size_t len, char* out, size_t cap)
{
    if (!cap) return;
    out[0] = 0;
    size_t i = 0;
    while (i < len) {
        size_t e = i;
        while (e < len && cfg[e] != '\r' && cfg[e] != '\n') ++e;
        // A line naming a folder: has a backslash
        const char* line = cfg + i;
        const size_t n = e - i;
        if (memchr(line, '\\', n)) {
            size_t s = 0;
            if (n >= 2 && line[1] == ':') s = 2;
            while (s < n && (line[s] == '\\' || line[s] == '/')) ++s;
            size_t o = 0;
            for (size_t k = s; k < n && o + 1 < cap; ++k) out[o++] = line[k] == '\\' ? '/' : line[k];
            while (o > 0 && out[o - 1] == '/') --o;
            out[o] = 0;
            return;
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
    if (!n || strlen(sub) <= n + 1 || sub[n] != '/') return false;
    for (size_t i = 0; i < n; ++i) {
        const char a = sub[i], c = data_dir[b + i];
        if ((a >= 'a' && a <= 'z' ? a - 32 : a) != (c >= 'a' && c <= 'z' ? c - 32 : c)) return false;
    }
    memmove(sub, sub + n + 1, strlen(sub + n + 1) + 1);
    return true;
}

} // namespace savegame
