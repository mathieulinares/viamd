#pragma once

#include <core/md_vec_math.h>
#include <imgui.h>

namespace immediate {

// Shared screen-space label style for native measurements and component annotations.
inline bool scene_label(ImDrawList* draw, const mat4_t& mvp, ImVec2 viewport, vec3_t position,
                        const char* begin, const char* end, ImU32 text, ImU32 background, ImVec2 offset = ImVec2(0, 0)) {
    const vec4_t p = mat4_mul_vec4(mvp, vec4_from_vec3(position, 1));
    if (p.w <= 0) return false;
    const vec4_t c = p / p.w;
    if (!(c.x > -1 && c.x < 1 && c.y > -1 && c.y < 1 && c.z > -1 && c.z < 1)) return false;
    const ImVec2 size = ImGui::CalcTextSize(begin, end);
    const ImVec2 center((c.x * 0.5f + 0.5f) * viewport.x + offset.x, (-c.y * 0.5f + 0.5f) * viewport.y + offset.y);
    const ImVec2 lo(center.x - size.x * 0.5f, center.y - size.y * 0.5f);
    const ImVec2 hi(lo.x + size.x, lo.y + size.y);
    draw->AddRectFilled(ImVec2(lo.x - 4, lo.y - 2), ImVec2(hi.x + 4, hi.y + 2), background, 5);
    draw->AddText(lo, text, begin, end);
    return true;
}

} // namespace immediate
