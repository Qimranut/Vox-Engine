#pragma once
#include <glm/glm.hpp>
#include <string>

namespace cpn {
struct Text {
    std::string font{"fonts/Barlow-Regular.ttf"};
    std::string content{"New text"};
    bool screenSpace{true};
    float size{32.f}; // Pixels in screen space; world units in 3D.
    glm::vec2 screenPosition{24.f, 24.f}; // Top-left, in viewport pixels.
    glm::vec4 color{1.f}; // Display RGB and opacity.
    int alignment{}; // 0 left, 1 centre, 2 right, per line.
};
}
