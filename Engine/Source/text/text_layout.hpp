#pragma once
#include "resources/FontAsset.hpp"
#include "components/component_types/text.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace text {
// Positions use a top-left origin and +Y down; graphics maps this into screen or world space.
// Printable ASCII, newlines and tabs. Unsupported UTF-8 codepoints become one '?'.
std::vector<glm::vec4> layout(const vox::fontasset::Font& font, const cpn::Text& text);
}
