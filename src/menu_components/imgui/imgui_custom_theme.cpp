#include <algorithm>
#include <cstring>
#include "menu.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include <memory>

namespace Menu {

static void drawCustomThemeControls(Themes::IniOptions& opts, bool& redraw) {
    opts.ensureCustomThemesLoaded();
    ImGui::TextWrapped("Start with a base theme, then override individual colors. Changes preview immediately and save automatically when you finish editing.");
    bool changed = false;
    ImGui::SetNextItemWidth(250);
    if (ImGui::BeginCombo("Theme", opts.custom_theme_name.c_str())) {
        // Copy names because selection stores pending edits in the collection.
        std::vector<std::string> names;
        for (const auto& saved : opts.custom_themes) names.push_back(saved.name);
        for (const auto& name : names) {
            if (ImGui::Selectable(name.c_str(), name == opts.custom_theme_name))
                changed |= opts.selectCustomTheme(name);
        }
        ImGui::EndCombo();
    }
    static char name[129] = {};
    static std::string displayedName;
    if (displayedName != opts.custom_theme_name) {
        std::strncpy(name, opts.custom_theme_name.c_str(), sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        displayedName = opts.custom_theme_name;
    }
    ImGui::SetNextItemWidth(250);
    bool rename = ImGui::InputText("Name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::BeginDisabled(!opts.custom_theme_enabled);
    rename |= ImGui::Button("Rename");
    if (rename && opts.custom_theme_enabled) changed |= opts.renameCustomTheme(name);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("New")) changed |= opts.createCustomTheme(name, false);
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) changed |= opts.createCustomTheme(name, true);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save a copy of the current colors with the entered name");
    const char* bases[] = {"dark", "igv", "slate"};
    int base = opts.theme_str == "igv" ? 1 : opts.theme_str == "slate" ? 2 : 0;
    ImGui::SetNextItemWidth(130);
    if (ImGui::Combo("Base theme", &base, bases, 3)) {
        opts.theme_str = bases[base];
        opts.custom_theme_save_pending = true;
        changed = true;
    }
    bool enabled = opts.custom_theme_enabled;
    std::error_code pathError;
    if (ImGui::Checkbox("Use custom colors", &enabled)) {
        if (enabled) {
            if (opts.custom_themes.empty() && std::filesystem::exists(opts.customThemePath(), pathError))
                enabled = opts.loadCustomTheme(opts.customThemePath());
            else if (!opts.custom_themes.empty())
                opts.selectCustomTheme(opts.custom_theme_name);
        }
        opts.custom_theme_enabled = enabled;
        changed = true;
    }
    ImGui::TextWrapped("Theme file: %s", opts.customThemePath().string().c_str());
    if (ImGui::Button("Reload JSON")) {
        if (opts.loadCustomTheme(opts.customThemePath())) {
            opts.custom_theme_enabled = true;
            changed = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset all colors")) {
        opts.custom_theme_colors.clear();
        opts.custom_theme_save_pending = true;
        changed = true;
    }
    if (!opts.custom_theme_error.empty())
        ImGui::TextWrapped("Theme file: %s", opts.custom_theme_error.c_str());

    if (changed) opts.rebuildTheme();
    ImGui::BeginDisabled(!opts.custom_theme_enabled);
    const char* groups[] = {"Background and text", "Reads", "Nucleotides",
        "Coverage and tracks", "Lines and highlights", "Mapping quality zero",
        "Base modifications", "Translation", "Mate palette (uniform color)"};
    for (const char* group : groups) {
        if (!ImGui::CollapsingHeader(group)) continue;
        for (const auto& entry : Themes::themeColors()) {
            if (std::strcmp(entry.group, group) != 0) continue;
            ImGui::PushID(entry.name);
            int a, r, g, b;
            opts.theme.getPaintARGB(entry.paint, a, r, g, b);
            float rgba[] = {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
            ImGui::SetNextItemWidth(std::min(240.f, ImGui::GetContentRegionAvail().x * 0.6f));
            if (ImGui::ColorEdit4(entry.label, rgba, ImGuiColorEditFlags_AlphaBar |
                    ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_Uint8)) {
                auto byte = [](float v) { return (int)(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
                opts.custom_theme_colors[entry.paint] = SkColorSetARGB(
                    byte(rgba[3]), byte(rgba[0]), byte(rgba[1]), byte(rgba[2]));
                opts.custom_theme_save_pending = true;
                opts.rebuildTheme();
                changed = true;
            }
            if (opts.custom_theme_colors.count(entry.paint)) {
                // A separate line keeps Reset reachable in narrow windows.
                if (ImGui::SmallButton("Reset to base")) {
                    opts.custom_theme_colors.erase(entry.paint);
                    opts.custom_theme_save_pending = true;
                    opts.rebuildTheme();
                    changed = true;
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndDisabled();
    if (changed) {
        Themes::applyImGuiTheme(opts.theme_str);
        opts.settings_save_pending = true;
        redraw = true;
    }
}

namespace {
struct ThemePreviewCache {
    ImTextureData texture;
    sk_sp<SkImage> image;
    unsigned revision{~0u};
    std::string font;
    void releaseTexture() {
        if (texture.GetTexID() != ImTextureID_Invalid) {
            texture.WantDestroyNextFrame = true;
            texture.UnusedFrames = 1;
            texture.SetStatus(ImTextureStatus_WantDestroy);
            ImGui_ImplOpenGL3_UpdateTexture(&texture);
        }
        texture.DestroyPixels();
    }
};
std::unique_ptr<ThemePreviewCache> themePreviewCache;
}

void clearThemePreview() {
    if (!themePreviewCache) return;
    themePreviewCache->releaseTexture();
    themePreviewCache.reset();
}

static void drawThemePreview(const Themes::IniOptions& opts, bool fitHeight) {
    ImGui::TextUnformatted("Live preview");
    ImGui::TextWrapped("Example alignments rendered by GW");
    auto available = ImGui::GetContentRegionAvail();
    // On narrow windows keep a legible full-width image and let this child
    // scroll vertically. Reserve scrollbar width independently of its visibility
    // so appearing/disappearing scrollbars cannot cause layout feedback.
    const float fullWidth = ImGui::GetWindowWidth() - 2 * ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ScrollbarSize;
    const float width = std::max(1.f, fitHeight ? std::min(available.x, available.y * (900.f / 560.f)) : fullWidth);
    const ImVec2 size(width, width * (560.f / 900.f));
    const float density = std::max(1.f, ImGui::GetIO().DisplayFramebufferScale.x);
    const int renderWidth = std::clamp((int)(width * density), 300, 1600);
    const int renderHeight = renderWidth * 560 / 900;
    if (!themePreviewCache) themePreviewCache = std::make_unique<ThemePreviewCache>();
    auto& cache = *themePreviewCache;
    if (!cache.image || cache.revision != opts.theme_revision || cache.font != opts.font_str ||
            cache.image->width() != renderWidth || cache.image->height() != renderHeight) {
        cache.image = renderThemePreview(opts, renderWidth, renderHeight);
        if (!cache.image) { ImGui::TextUnformatted("Preview unavailable"); return; }
        cache.revision = opts.theme_revision;
        cache.font = opts.font_str;
        auto& tex = cache.texture;
        if (tex.Width != renderWidth || tex.Height != renderHeight || !tex.Pixels) {
            cache.releaseTexture();
            tex.WantDestroyNextFrame = false;
            // Create requires Destroyed, including for headless tests where the
            // pending GPU texture was never uploaded.
            tex.SetStatus(ImTextureStatus_Destroyed);
            tex.Create(ImTextureFormat_RGBA32, renderWidth, renderHeight);
        } else if (tex.GetTexID() != ImTextureID_Invalid) {
            tex.Updates.clear();
            tex.Updates.push_back(ImTextureRect{0, 0, (unsigned short)renderWidth, (unsigned short)renderHeight});
            tex.SetStatus(ImTextureStatus_WantUpdates);
        }
        // Skia stores native-channel premultiplied pixels; ImGui expects RGBA
        // with straight alpha. Convert once per edit, not once per UI frame.
        const auto info = SkImageInfo::Make(renderWidth, renderHeight, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
        cache.image->readPixels(info, tex.Pixels, tex.GetPitch(), 0, 0);
    }
    if (ImGui::GetIO().BackendRendererUserData) {
        ImGui_ImplOpenGL3_UpdateTexture(&cache.texture);
        ImGui::Image(cache.texture.GetTexRef(), size);
    } else {
        ImGui::Dummy(size); // Pixel rendering and layout remain testable without OpenGL.
    }
}

static void drawCustomTheme(Themes::IniOptions& opts, bool& redraw) {
    const auto available = ImGui::GetContentRegionAvail();
    const bool sideBySide = available.x >= 900.f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float editorWidth = sideBySide ? std::clamp(available.x * .36f, 340.f, 440.f) : available.x;
    const float editorHeight = sideBySide ? available.y : std::max(160.f, available.y * .5f);
    if (ImGui::BeginChild("##theme_editor", ImVec2(editorWidth, editorHeight)))
        drawCustomThemeControls(opts, redraw);
    ImGui::EndChild();
    if (sideBySide) ImGui::SameLine(0, spacing);
    const float previewWidth = sideBySide ? available.x - editorWidth - spacing : available.x;
    const float previewHeight = sideBySide ? available.y : std::max(1.f, available.y - editorHeight - ImGui::GetStyle().ItemSpacing.y);
    if (ImGui::BeginChild("##theme_preview", ImVec2(previewWidth, previewHeight)))
        drawThemePreview(opts, sideBySide);
    ImGui::EndChild();
}

} // namespace Menu
