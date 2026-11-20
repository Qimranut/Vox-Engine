#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace vox::fontasset {
// VFNT v1: little-endian 32-bit fields, printable ASCII, 64px coverage atlas.
// No FreeType or GPU dependency: shared by the offline cooker and resource loader.
constexpr uint32_t first = 32, count = 95, version = 1;
struct Glyph {
    uint32_t x{}, y{}, width{}, height{};
    float bearingX{}, bearingY{}, advance{};
};
struct Font {
    uint32_t width{1024}, height{512}, pixelsPerEm{64};
    float ascender{}, lineHeight{};
    std::array<Glyph, count> glyphs{};
    std::array<float, count * count> kerning{};
    std::vector<unsigned char> pixels;
};
template<class T> inline void writeValue(std::ostream& out, const T& v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
template<class T> inline void readValue(std::istream& in, T& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    if (!in) throw std::runtime_error("Truncated cooked font.");
}
inline void write(std::ostream& out, const Font& f) {
    out.write("VFNT", 4); writeValue(out, version);
    writeValue(out, f.width); writeValue(out, f.height); writeValue(out, f.pixelsPerEm);
    writeValue(out, f.ascender); writeValue(out, f.lineHeight);
    for (const auto& g : f.glyphs) {
        writeValue(out, g.x); writeValue(out, g.y); writeValue(out, g.width); writeValue(out, g.height);
        writeValue(out, g.bearingX); writeValue(out, g.bearingY); writeValue(out, g.advance);
    }
    for (auto k : f.kerning) writeValue(out, k);
    out.write(reinterpret_cast<const char*>(f.pixels.data()), static_cast<std::streamsize>(f.pixels.size()));
    if (!out) throw std::runtime_error("Could not write cooked font.");
}
inline Font read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    char magic[4]{}; in.read(magic, 4); uint32_t v{}; readValue(in, v);
    if (std::string(magic, 4) != "VFNT" || v != version) throw std::runtime_error("Unsupported cooked font.");
    Font f;
    readValue(in, f.width); readValue(in, f.height); readValue(in, f.pixelsPerEm);
    readValue(in, f.ascender); readValue(in, f.lineHeight);
    if (f.width != 1024 || f.height != 512 || f.pixelsPerEm != 64 ||
        !std::isfinite(f.ascender) || !std::isfinite(f.lineHeight) || f.lineHeight <= 0 || f.lineHeight > 1024)
        throw std::runtime_error("Invalid font atlas metrics.");
    for (auto& g : f.glyphs) {
        readValue(in, g.x); readValue(in, g.y); readValue(in, g.width); readValue(in, g.height);
        readValue(in, g.bearingX); readValue(in, g.bearingY); readValue(in, g.advance);
        if (g.width > f.width || g.height > f.height || g.x > f.width - g.width || g.y > f.height - g.height ||
            !std::isfinite(g.bearingX) || !std::isfinite(g.bearingY) || !std::isfinite(g.advance) ||
            std::abs(g.bearingX) > 1024 || std::abs(g.bearingY) > 1024 || std::abs(g.advance) > 1024)
            throw std::runtime_error("Invalid font glyph.");
    }
    for (auto& k : f.kerning) {
        readValue(in, k);
        if (!std::isfinite(k) || std::abs(k) > 1024) throw std::runtime_error("Invalid font kerning.");
    }
    f.pixels.resize(static_cast<size_t>(f.width) * f.height);
    in.read(reinterpret_cast<char*>(f.pixels.data()), static_cast<std::streamsize>(f.pixels.size()));
    if (!in || in.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Invalid font atlas payload.");
    return f;
}
}
