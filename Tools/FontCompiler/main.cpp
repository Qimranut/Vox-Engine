#include "resources/FontAsset.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace fs = std::filesystem;
void cook(FT_Library library, const fs::path& source, const fs::path& output) {
    // Read with filesystem APIs so Unicode paths work on Windows too.
    std::ifstream input(source, std::ios::binary);
    std::vector<unsigned char> bytes(std::istreambuf_iterator<char>{input}, {});
    if (input.bad() || bytes.empty()) throw std::runtime_error("Cannot read font.");
    FT_Face face{};
    if (FT_New_Memory_Face(library, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &face))
        throw std::runtime_error("FreeType cannot open font.");
    struct FaceGuard { FT_Face f; ~FaceGuard() { FT_Done_Face(f); } } guard{face};
    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) || FT_Set_Pixel_Sizes(face, 0, 64))
        throw std::runtime_error("Font needs a Unicode character map and a scalable 64px face.");
    vox::fontasset::Font font;
    font.ascender = static_cast<float>(face->size->metrics.ascender) / 64.f;
    font.lineHeight = static_cast<float>(face->size->metrics.height) / 64.f;
    font.pixels.resize(static_cast<size_t>(font.width) * font.height);
    uint32_t x = 2, y = 2, rowHeight = 0;
    for (uint32_t i = 0; i < vox::fontasset::count; ++i) {
        if (FT_Load_Char(face, i + vox::fontasset::first, FT_LOAD_RENDER)) throw std::runtime_error("Cannot rasterize glyph.");
        const auto slot = face->glyph;
        const auto& b = slot->bitmap;
        if (b.width && b.pixel_mode != FT_PIXEL_MODE_GRAY) throw std::runtime_error("Only grayscale outline fonts are supported.");
        if (x + b.width + 2 > font.width) { x = 2; y += rowHeight + 4; rowHeight = 0; }
        if (b.width + 4 > font.width || y + b.rows + 2 > font.height) throw std::runtime_error("Font exceeds atlas size.");
        font.glyphs[i] = {x, y, b.width, b.rows, static_cast<float>(slot->bitmap_left),
            static_cast<float>(slot->bitmap_top), static_cast<float>(slot->advance.x) / 64.f};
        for (uint32_t row = 0; row < b.rows; ++row) {
            const auto* src = b.buffer + (b.pitch >= 0 ? row : b.rows - 1 - row) * static_cast<size_t>(std::abs(b.pitch));
            std::copy_n(src, b.width, font.pixels.data() + static_cast<size_t>(y + row) * font.width + x);
        }
        x += b.width + 4; rowHeight = std::max(rowHeight, b.rows);
    }
    for (uint32_t a = 0; a < vox::fontasset::count; ++a)
        for (uint32_t b = 0; b < vox::fontasset::count; ++b) {
            FT_Vector delta{};
            FT_Get_Kerning(face, FT_Get_Char_Index(face, a + 32), FT_Get_Char_Index(face, b + 32), FT_KERNING_DEFAULT, &delta);
            font.kerning[a * vox::fontasset::count + b] = static_cast<float>(delta.x) / 64.f;
        }
    fs::create_directories(output.parent_path());
    auto temp = output; temp += ".tmp";
    try {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        vox::fontasset::write(out, font); out.close();
        if (!out) throw std::runtime_error("Cannot close cooked font.");
        (void)vox::fontasset::read(temp);
        if (!MoveFileExW(temp.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace cooked font.");
    } catch (...) { std::error_code ec; fs::remove(temp, ec); throw; }
}
int main(int argc, char** argv) {
    FT_Library library{};
    if (FT_Init_FreeType(&library)) { std::cerr << "Cannot initialize FreeType.\n"; return 1; }
    struct LibraryGuard { FT_Library l; ~LibraryGuard() { FT_Done_FreeType(l); } } guard{library};
    try {
        const auto root = fs::absolute(argc > 1 ? argv[1] : "Assets");
        const auto toolTime = fs::last_write_time(fs::absolute(argv[0]));
        size_t cooked = 0;
        for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator{}; ++it) {
            if (it->is_directory() && it->path().filename() == ".cooked") { it.disable_recursion_pending(); continue; }
            if (!it->is_regular_file()) continue;
            auto ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".ttf" && ext != ".otf") continue;
            if (it->file_size() > 32 * 1024 * 1024) throw std::runtime_error("Font exceeds 32 MiB.");
            auto output = root / ".cooked" / it->path().lexically_relative(root); output += ".vfont";
            if (fs::exists(output) && fs::last_write_time(output) >= it->last_write_time() && fs::last_write_time(output) >= toolTime) continue;
            cook(library, it->path(), output); ++cooked;
            std::cout << "[FontCompiler] " << it->path().filename().string() << '\n';
        }
        std::cout << "[FontCompiler] Cooked " << cooked << " font(s).\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "[FontCompiler] " << e.what() << '\n'; return 1; }
}
