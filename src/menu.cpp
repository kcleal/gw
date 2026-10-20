#include "menu.h"
#include "imgui_internal.h"

namespace Menu {
bool hasImGuiEscapeTarget() {
    auto* context = ImGui::GetCurrentContext();
    if (!context) return false;
    for (auto* window : context->Windows) {
        if (window->Active && !window->Hidden &&
            (window->HasCloseButton || (window->Flags & ImGuiWindowFlags_Popup)))
            return true;
    }
    return false;
}

// Resolve once per key press so closing a front window cannot also close one
// behind it. Include native ImGui popups, which handle their own Escape key.
static bool closeOnEscape(bool* open = nullptr) {
    auto& g = *ImGui::GetCurrentContext();
    if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false)) return false;
    static int frame = -1;
    static ImGuiContext* context = nullptr;
    static ImGuiWindow* target = nullptr;
    if (context != &g || frame != g.FrameCount) {
        context = &g;
        frame = g.FrameCount;
        target = nullptr;
        for (int i = g.Windows.Size - 1; i >= 0; --i) {
            auto* window = g.Windows[i];
            if (window->WasActive && !window->Hidden &&
                (window->HasCloseButton || (window->Flags & ImGuiWindowFlags_Popup))) {
                target = window;
                break;
            }
        }
    }
    if (ImGui::GetCurrentWindow() != target) return false;
    if (open) *open = false;
    return true;
}
}

// Skia menu
#include "menu_components/menu_draw.cpp"
#include "menu_components/menu_navigation.cpp"

// Options + command parsing
#include "menu_components/menu_options.cpp"
#include "menu_components/menu_commands.cpp"

// ImGui components
#include "menu_components/imgui/imgui_custom_theme.cpp"
#include "menu_components/imgui/imgui_settings.cpp"
#include "menu_components/imgui/imgui_command_dialog.cpp"
#include "menu_components/imgui/imgui_online_files.cpp"
#include "menu_components/imgui/imgui_top_menu.cpp"
#include "menu_components/imgui/imgui_open_dialog.cpp"
#include "menu_components/imgui/imgui_help.cpp"
#include "menu_components/imgui/imgui_info_popups.cpp"
#include "menu_components/imgui/imgui_data_labels.cpp"
#include "menu_components/imgui/imgui_alignment_scrollbars.cpp"
#include "menu_components/imgui/imgui_close_dialog.cpp"
#include "menu_components/imgui/imgui_label_table.cpp"
