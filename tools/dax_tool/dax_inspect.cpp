// PC tool: inspects a folder of Gold Box DAX files with the engine's own
// decoders (src/engine). For checking formats against real game files.
//
//   dax_inspect <game folder>                 summary of every DAX file
//   dax_inspect <game folder> <out dir>       also writes each picture /
//                                             animation frame as a .ppm
//
// Build: g++ -std=c++17 -O1 -Isrc tools/dax_tool/dax_inspect.cpp src/engine/*.cpp -o dax_inspect
//
// The game files are the player's own and never go into the repo - neither
// do the pictures this writes.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

#include "engine/dax.h"
#include "engine/picture.h"

namespace {

std::vector<uint8_t> read_file(const std::string& path)
{
    std::vector<uint8_t> d;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return d;
    fseek(f, 0, SEEK_END);
    d.resize(static_cast<size_t>(ftell(f)));
    fseek(f, 0, SEEK_SET);
    if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
    fclose(f);
    return d;
}

bool ends_dax(const std::string& n)
{
    if (n.size() < 4) return false;
    std::string e = n.substr(n.size() - 4);
    for (auto& c : e) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return e == ".DAX";
}

pic::Rgb vga_pal[256];
bool use_vga = false;

void write_ppm(const std::string& path, const pic::Canvas& c, int w, int h)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint8_t i = c.px[y * c.w + x];
            const pic::Rgb rgb = use_vga ? vga_pal[i] : i < 16 ? pic::kEga[i] : pic::Rgb{0x30, 0x30, 0x30};
            fputc(rgb.r, f);
            fputc(rgb.g, f);
            fputc(rgb.b, f);
        }
    fclose(f);
}

uint8_t canvas_px[pic::kScreenW * pic::kScreenH];

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <game folder> [out dir]\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::string out = argc > 2 ? argv[2] : "";
    std::vector<std::string> names;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d))
            if (ends_dax(e->d_name)) names.push_back(e->d_name);
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    static dax::Index idx;
    static pic::Anim anim;
    pic::Canvas c{canvas_px, pic::kScreenW, pic::kScreenH};
    int total_pic = 0, total_anim = 0, total_vga = 0, total_other = 0;
    for (const auto& n : names) {
        const auto data = read_file(dir + "/" + n);
        dax::MemorySource src(data.data(), static_cast<uint32_t>(data.size()));
        const dax::Status st = dax::read_index(src, idx);
        printf("%-14s %7zu bytes  %-24s %3d blocks:", n.c_str(), data.size(), dax::status_text(st), idx.count);
        const bool xor_frames = strncasecmp(n.c_str(), "PIC", 3) == 0 || strncasecmp(n.c_str(), "FINAL", 5) == 0;
        for (int i = 0; i < idx.count; ++i) {
            const dax::Entry& e = idx.entries[i];
            std::vector<uint8_t> raw(e.raw_size);
            const uint32_t got = dax::load_block(src, idx, e, raw.data());
            pic::Header h;
            uint32_t extra = 0;
            const bool is_pic = got == e.raw_size && pic::parse_header(raw.data(), e.raw_size, h, &extra);
            if (is_pic && extra) printf(" [%u: %u extra bytes]", e.id, extra);
            pic::VgaHeader vh;
            const bool is_vga = !is_pic && got == e.raw_size && pic::parse_vga_header(raw.data(), e.raw_size, vh);
            bool is_anim = false;
            if (!is_pic && !is_vga) {
                dax::RleReader r(src, idx, e);
                is_anim = pic::parse_anim(r, e.raw_size, anim);
            }
            if (is_pic) {
                ++total_pic;
                printf(" %u:P%dx%dx%d", e.id, h.width_px(), h.height, h.frames);
            } else if (is_vga) {
                ++total_vga;
                printf(" %u:V%dx%dx%d", e.id, vh.width_px(), vh.height, vh.frames);
            } else if (is_anim) {
                ++total_anim;
                printf(" %u:A%dx%dx%d", e.id, anim.frame[0].width_px(), anim.frame[0].height, anim.frames);
            } else {
                ++total_other;
                printf(" %u:%u%s", e.id, e.raw_size, got == e.raw_size ? "" : "(short)");
            }
            if (out.empty()) continue;
            char base[256];
            snprintf(base, sizeof base, "%s/%s_%03u", out.c_str(), n.c_str(), e.id);
            use_vga = is_vga;
            if (is_vga) {
                for (int i = 0; i < 256; ++i) vga_pal[i] = pic::Rgb{0x30, 0x00, 0x30};
                {
                    dax::RleReader r(src, idx, e);
                    r.skip(pic::kVgaHeaderSize);
                    pic::read_vga_palette(r, vh, vga_pal);
                }
                for (int f = 0; f < vh.frames && f < 12; ++f) {
                    c.clear(0);
                    dax::RleReader r(src, idx, e);
                    pic::draw_vga(r, vh, f, c, 0, 0);
                    write_ppm(std::string(base) + "_f" + std::to_string(f) + ".ppm", c, vh.width_px(), vh.height);
                }
            } else if (is_pic) {
                for (int f = 0; f < h.frames && f < 8; ++f) {
                    c.clear(pic::kTransparent);
                    dax::RleReader r(src, idx, e);
                    r.skip(pic::kHeaderSize);
                    pic::draw(r, h, f, c, 0, 0);
                    write_ppm(std::string(base) + "_f" + std::to_string(f) + ".ppm", c, h.width_px(), h.height);
                }
            } else if (is_anim) {
                for (int f = 0; f < anim.frames && f < 8; ++f) {
                    c.clear(pic::kTransparent);
                    pic::draw_anim(src, idx, e, anim, f, xor_frames, c, 0, 0);
                    write_ppm(std::string(base) + "_f" + std::to_string(f) + ".ppm", c, anim.frame[f].width_px(),
                              anim.frame[f].height);
                }
            }
        }
        printf("\n");
    }
    printf("pictures %d, animations %d, VGA pictures %d, other %d\n", total_pic, total_anim, total_vga, total_other);
    return 0;
}
