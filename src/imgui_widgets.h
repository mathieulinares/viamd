#include <imgui.h>

namespace ImGui {

IMGUI_API bool RangeSliderFloat(const char* label, float* v1, float* v2, float v_min, float v_max, const char* display_format = "(%.3f, %.3f)", ImGuiSliderFlags flags = 0);

// custom ImGui procedures
bool DeleteButton(const char* label, const ImVec2& size = ImVec2(0, 0));
void CreateDockspace();
void BeginCanvas(const char* id, bool allow_inputs = false);
void EndCanvas();

inline bool InputQuery(const char* label, char* buf, size_t buf_size, bool is_valid, const char* err_text = 0, ImGuiInputTextFlags flags = 0) {
	if (!is_valid) ImGui::PushStyleColor(ImGuiCol_FrameBg, 0xAA222299);
	bool result = ImGui::InputText(label, buf, buf_size, flags);
	if (!is_valid) ImGui::PopStyleColor();
	if (ImGui::IsItemHovered() && !is_valid) {
		ImGui::SetTooltip("%s", err_text);
	}
	return result;
}


void init_theme();

void PushDisabled();
void PopDisabled();

bool ColorEdit3Minimal(const char* label, float color[3]);
bool ColorEdit4Minimal(const char* label, float color[4]);

}  // namespace ImGui
