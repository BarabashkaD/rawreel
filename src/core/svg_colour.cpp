#include "core/svg_colour.h"

#include <cstddef>

namespace rawreel {

std::string replaceCurrentColor(std::string_view svg, std::string_view colour) {
    constexpr std::string_view CURRENT = "currentColor";
    std::string text;
    text.reserve(svg.size());
    std::size_t from = 0;
    for (std::size_t at = svg.find(CURRENT); at != std::string_view::npos;
         at = svg.find(CURRENT, from)) {
        text.append(svg.substr(from, at - from));
        text.append(colour);
        from = at + CURRENT.size();
    }
    text.append(svg.substr(from));
    return text;
}

}  // namespace rawreel
