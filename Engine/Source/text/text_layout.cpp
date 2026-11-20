#include "text_layout.hpp"
#include <string_view>

namespace text {
std::vector<glm::vec4> layout(const vox::fontasset::Font& font, const cpn::Text& text) {
    std::vector<glm::vec4> vertices;
    if (!std::isfinite(text.size) || text.size <= 0 || text.content.size() > 4096) return vertices;
    std::string content;
    for (size_t i = 0; i < text.content.size(); ++i) {
        const auto c = static_cast<unsigned char>(text.content[i]);
        if (c >= 128) {
            content += '?';
            while (i + 1 < text.content.size() && (static_cast<unsigned char>(text.content[i + 1]) & 0xc0) == 0x80) ++i;
        } else if (c == '\t') content += "    ";
        else if (c != '\r') content += ((c < 32 && c != '\n') || c == 127) ? '?' : static_cast<char>(c);
    }
    const float scale = text.size / static_cast<float>(font.pixelsPerEm);
    float baseline = font.ascender * scale;
    size_t start = 0;
    while (start <= content.size()) {
        auto end = content.find('\n', start);
        if (end == std::string::npos) end = content.size();
        float lineWidth = 0;
        for (size_t i = start; i < end; ++i) {
            const auto index = static_cast<unsigned char>(content[i]) - vox::fontasset::first;
            lineWidth += font.glyphs[index].advance;
            if (i > start) lineWidth += font.kerning[(static_cast<unsigned char>(content[i - 1]) - vox::fontasset::first) * vox::fontasset::count + index];
        }
        float x = -lineWidth * scale * (text.alignment == 1 ? .5f : text.alignment == 2 ? 1.f : 0.f);
        for (size_t i = start; i < end; ++i) {
            const auto index = static_cast<unsigned char>(content[i]) - vox::fontasset::first;
            if (i > start) x += font.kerning[(static_cast<unsigned char>(content[i - 1]) - vox::fontasset::first) * vox::fontasset::count + index] * scale;
            const auto& g = font.glyphs[index];
            const float l = x + g.bearingX * scale, t = baseline - g.bearingY * scale;
            const float r = l + static_cast<float>(g.width) * scale, b = t + static_cast<float>(g.height) * scale;
            const float u0 = static_cast<float>(g.x) / font.width, u1 = static_cast<float>(g.x + g.width) / font.width;
            const float v0 = static_cast<float>(g.y) / font.height, v1 = static_cast<float>(g.y + g.height) / font.height;
            if (g.width && g.height) vertices.insert(vertices.end(), {{l,t,u0,v0}, {l,b,u0,v1}, {r,b,u1,v1}, {l,t,u0,v0}, {r,b,u1,v1}, {r,t,u1,v0}});
            x += g.advance * scale;
        }
        if (end == content.size()) break;
        start = end + 1; baseline += font.lineHeight * scale;
    }
    return vertices;
}
}
