// imgui_command_bar.cpp
// Command bar docked along the bottom edge. Replaces the Skia command box when the
// ImGui overlay is running, and keeps its look: menu-coloured strip, plain text, a
// list of commands above it with a dot on the selected one and a tick on switches
// that are on.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "menu.h"
#include "plot_manager.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace Menu {

namespace {

struct CommandBarState {
    char buf[1024] = {};
    std::string lastText;             // text after the previous frame, to spot user edits
    std::vector<const char*> matches; // commands offered for the current text
    int selected = -1;                // row in matches, -1 for none
    int historyPos = -1;              // index into commandHistory, -1 when not browsing
    bool focusInput = false;          // give the text box the keyboard
    bool cursorToEnd = false;
    bool scrollToSelected = false;
    bool wasActive = false;
    bool wasCapturing = false;
    bool popupHeld = false;           // pointer is down on the list, so the text box lost the keyboard
    bool popupShown = false;
    ImRect popupRect;
    std::string replace;              // text to put in the active text box
    bool hasReplace = false;
    Manager::GwPlot* plot = nullptr;
};

CommandBarState& barState() {
    static CommandBarState s;
    return s;
}

ImVec4 paintColor(const SkPaint& paint, float alpha = 1.0f) {
    const SkColor c = paint.getColor();
    return ImVec4(SkColorGetR(c) / 255.f, SkColorGetG(c) / 255.f, SkColorGetB(c) / 255.f, alpha);
}

void setBarText(CommandBarState& s, const std::string& text) {
    std::snprintf(s.buf, sizeof(s.buf), "%s", text.c_str());
    s.lastText = s.buf;
    if (s.plot) s.plot->setCommandText(s.buf);
}

void updateMatches(CommandBarState& s) {
    s.matches.clear();
    const size_t len = std::strlen(s.buf);
    for (const char* cmd : commandList) {
        if (std::strncmp(cmd, s.buf, len) == 0 && cmd[len] != '\0') s.matches.push_back(cmd);
    }
    if (s.selected >= (int)s.matches.size()) s.selected = -1;
}

// Commands that run as soon as they are picked from the list
bool runsWithoutArguments(const Manager::GwPlot* plot, const char* cmd) {
    for (const char* e : exec) {
        if (std::strcmp(e, cmd) == 0) return true;
    }
    return std::strcmp(cmd, "online") == 0 && !plot->opts.genome_tag.empty();
}

int commandBarCallback(ImGuiInputTextCallbackData* data) {
    auto& s = *static_cast<CommandBarState*>(data->UserData);
    auto replaceText = [&](const std::string& text) {
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, text.c_str());
        data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
        s.lastText = text;
    };
    if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        if (s.hasReplace) {
            replaceText(s.replace);
            s.hasReplace = false;
            s.cursorToEnd = false;
        } else if (s.cursorToEnd) {
            data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
            s.cursorToEnd = false;
        }
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        std::string text(data->Buf, data->BufTextLen);
        if (s.plot->tabCompleteCommand(text)) {
            replaceText(text);
            s.selected = -1;
        } else if (s.matches.size() == 1) {
            replaceText(s.matches.front());
            s.selected = -1;
        } else if (!s.matches.empty()) {
            s.selected = (s.selected + 1) % (int)s.matches.size();
            s.scrollToSelected = true;
        }
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        const auto& history = s.plot->commandHistory;
        const bool up = data->EventKey == ImGuiKey_UpArrow;
        const int n = (int)s.matches.size();
        if (s.selected >= 0) {  // moving through the list
            s.selected += up ? -1 : 1;
            if (s.selected >= n) s.selected = n - 1;
            s.scrollToSelected = true;
        } else if (up && !history.empty()) {
            s.historyPos = s.historyPos < 0 ? (int)history.size() - 1 : std::max(0, s.historyPos - 1);
            replaceText(history[s.historyPos]);
        } else if (!up && s.historyPos >= 0) {
            ++s.historyPos;
            if (s.historyPos >= (int)history.size()) {
                s.historyPos = -1;
                replaceText("");
            } else {
                replaceText(history[s.historyPos]);
            }
        } else if (!up && n > 0) {
            s.selected = 0;
            s.scrollToSelected = true;
        }
    }
    return 0;
}

// "chr1:1,234,567" for the pointer over a region of the plot, else empty
std::string positionUnderPointer(const Manager::GwPlot* plot) {
    const ImGuiIO& io = ImGui::GetIO();
    if (plot->mode != Manager::Show::SINGLE || plot->regions.empty() || !ImGui::IsMousePosValid(&io.MousePos) ||
        pointerOverOtherWindow()) return "";
    const float ms = std::max(1.f, plot->monitorScale);
    const float x = io.MousePos.x * ms, y = io.MousePos.y * ms;
    if (x < 0 || x >= plot->drawingWidth() || y < plot->topMenuSpace || y > plot->fb_height - plot->sliderSpace) return "";
    const float regionWidth = (float)plot->drawingWidth() / (float)plot->regions.size();
    const int idx = std::clamp((int)(x / regionWidth), 0, (int)plot->regions.size() - 1);
    const Utils::Region& rgn = plot->regions[idx];
    const double span = (double)rgn.end - (double)rgn.start;
    const double width = regionWidth - plot->gap * 2;
    if (span <= 0 || width <= 0) return "";
    const double offset = regionWidth * idx + plot->gap;
    if (x < offset || x > offset + width) return "";
    const int pos = (int)((x - offset) / (width / span)) + rgn.start;
    return rgn.chrom + ":" + Utils::formatNum(pos);
}

void runCommand(Manager::GwPlot* plot, CommandBarState& s, const std::string& text, bool& redraw) {
    s.buf[0] = '\0';
    s.lastText.clear();
    s.selected = s.historyPos = -1;
    s.popupHeld = false;
    try {
        plot->submitCommand(text);
    } catch (Manager::CloseException&) {
        if (plot->window) glfwSetWindowShouldClose(plot->window, GLFW_TRUE);
    }
    ImGui::SetWindowFocus(nullptr);  // keys go back to the plot
    redraw = true;
}

// A list row was chosen with Enter or the pointer
void chooseCommand(Manager::GwPlot* plot, CommandBarState& s, const char* cmd, bool& redraw) {
    if (runsWithoutArguments(plot, cmd)) {
        runCommand(plot, s, cmd, redraw);
        return;
    }
    setBarText(s, std::string(cmd) + " ");
    s.replace = s.buf;
    s.hasReplace = true;
    s.selected = s.historyPos = -1;
    s.focusInput = true;
}

// List of commands above the bar. Returns the row chosen with the pointer, or null.
const char* drawCommandList(Manager::GwPlot* plot, CommandBarState& s, float barTop) {
    const ImGuiIO& io = ImGui::GetIO();
    const Themes::BaseTheme& theme = plot->opts.theme;
    const float ms = std::max(1.f, plot->monitorScale);
    const float lineH = ImGui::GetTextLineHeight();
    const float charW = ImGui::CalcTextSize("0").x;
    const float padT = lineH * 0.3f;
    const float step = lineH + padT;
    const float textX = 25.f + charW * 3.f;  // the bar's text starts at 25 + 14 / scale
    const int n = (int)s.matches.size();

    float longest = 0;
    for (const char* cmd : commandList) longest = std::max(longest, ImGui::CalcTextSize(cmd).x);
    const float roomAbove = barTop - plot->topMenuSpace / ms - padT * 2;
    const int rows = std::max(1, std::min({n, 12, (int)((roomAbove - padT * 2) / step)}));
    const float scrollbar = n > rows ? ImGui::GetStyle().ScrollbarSize : 0;
    const float width = std::min(io.DisplaySize.x, textX + longest + charW * 2 + scrollbar);
    const float height = std::floor(rows * step + padT * 2);

    ImGui::SetNextWindowPos(ImVec2(0, barTop - height), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, padT));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f / ms);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, paintColor(theme.bgMenu));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##command_list", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    s.popupRect = ImGui::GetCurrentWindow()->Rect();

    const ImU32 textCol = ImGui::GetColorU32(paintColor(theme.tcDel));
    const ImU32 markCol = ImGui::GetColorU32(paintColor(theme.lcBright));
    const bool pointerMoved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
    const float rowW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* chosen = nullptr;
    for (int i = 0; i < n; ++i) {
        const char* cmd = s.matches[i];
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##row", ImVec2(rowW, step))) chosen = cmd;
        ImGui::PopID();
        if (ImGui::IsItemHovered() && pointerMoved) s.selected = i;  // the dot follows the pointer
        const float midY = p.y + step * 0.5f;
        if (i == s.selected) {
            dl->AddCircleFilled(ImVec2(25.f + charW * 0.5f, midY), std::max(2.f, 3.f / ms + 1.f), markCol);
            if (s.scrollToSelected) {
                ImGui::SetScrollHereY(0.5f);
                s.scrollToSelected = false;
            }
        }
        std::string name = cmd;
        if (getCommandSwitchValue(plot->opts, name, plot->drawLine) == 1) {
            const float x = 25.f;
            dl->PathLineTo(ImVec2(x + charW * 1.5f, midY));
            dl->PathLineTo(ImVec2(x + charW * 1.8f, midY + lineH * 0.25f));
            dl->PathLineTo(ImVec2(x + charW * 2.4f, midY - lineH * 0.35f));
            dl->PathStroke(markCol, 0, std::max(1.f, 1.5f / ms + 0.5f));
        }
        dl->AddText(ImVec2(textX, p.y + padT * 0.5f), textCol, cmd);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);
    return chosen;
}

} // namespace

void drawImGuiCommandBar(Manager::GwPlot* plot, bool& redraw) {
    auto& s = barState();
    s.plot = plot;
    const bool capturing = plot->commandBoxOpen();
    if (plot->mode == Manager::Show::SETTINGS || (!plot->commandBarVisible && !capturing && !s.wasActive)) {
        s.wasCapturing = capturing;
        s.popupShown = false;
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const Themes::BaseTheme& theme = plot->opts.theme;
    const float ms = std::max(1.f, plot->monitorScale);

    // '/' or ':' was pressed, or the legacy key path changed the text
    if (capturing && !s.wasCapturing && !s.wasActive) {
        s.selected = s.historyPos = -1;
        s.focusInput = true;
        s.cursorToEnd = true;
    }
    if (!s.wasActive && !s.hasReplace) {
        if (capturing && plot->inputText != s.buf) {
            std::snprintf(s.buf, sizeof(s.buf), "%s", plot->inputText.c_str());
            s.lastText = s.buf;
        } else if (!capturing && s.wasCapturing) {
            s.buf[0] = '\0';
            s.lastText.clear();
        }
    }

    // Same height as the top menu bar. When hidden it only appears while typing, over the slider.
    float barH = plot->topMenuSpace / ms;
    if (barH <= 0) barH = ImGui::GetFrameHeight() + 8.f;
    const float barTop = io.DisplaySize.y - barH;
    const float barW = plot->drawingWidth() / ms;

    ImGui::SetNextWindowPos(ImVec2(0, barTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(barW, barH), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, paintColor(theme.bgMenu));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, paintColor(theme.tcDel));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, paintColor(theme.lcBright));
    ImGui::Begin("##command_bar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollWithMouse);
    ImGuiWindow* barWindow = ImGui::GetCurrentWindow();

    const float textX = 25.f + 14.f / ms - ImGui::GetStyle().FramePadding.x;
    ImGui::SetCursorPos(ImVec2(textX, (barH - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::SetNextItemWidth(std::max(1.f, barW - textX - 14.f / ms));
    if (s.focusInput) {
        ImGui::SetKeyboardFocusHere();
        s.focusInput = false;
        s.cursorToEnd = true;
    }
    std::string hint = positionUnderPointer(plot);
    if (hint.empty() && plot->startupHints) hint = "Enter command  /";
    commandBarInputId = ImGui::GetID("##command_input");
    const bool entered = ImGui::InputTextWithHint(
        "##command_input", hint.c_str(), s.buf, sizeof(s.buf),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways |
        ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory,
        commandBarCallback, &s);
    const bool active = ImGui::IsItemActive();
    const bool focused = active || ImGui::IsWindowFocused();
    ImGui::End();
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(5);

    // Sits beneath other windows until it is selected
    if (!focused) ImGui::BringWindowToDisplayBack(barWindow);

    if (active) {
        if (!plot->commandBoxOpen()) plot->setCommandCapture(true);
        if (s.lastText != s.buf) {  // typed, pasted or deleted
            s.lastText = s.buf;
            s.selected = s.historyPos = -1;
        }
        if (plot->inputText != s.buf) plot->setCommandText(s.buf);
    }
    updateMatches(s);

    if (entered) {
        if (s.selected >= 0 && s.selected < (int)s.matches.size()) {
            chooseCommand(plot, s, s.matches[s.selected], redraw);
        } else {
            runCommand(plot, s, s.buf, redraw);
        }
    } else if (!active && s.wasActive) {
        if (s.popupShown && s.popupRect.Contains(io.MousePos) && ImGui::IsMouseDown(0)) {
            s.popupHeld = true;  // scrolling or picking from the list
        } else {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                s.buf[0] = '\0';
                s.lastText.clear();
                ImGui::SetWindowFocus(nullptr);
            }
            s.selected = s.historyPos = -1;
            plot->setCommandCapture(false);
            plot->setCommandText(s.buf);
            redraw = true;
        }
    }

    const bool listWanted = plot->commandBoxOpen() && !s.matches.empty() &&
                            (active || s.popupHeld || s.focusInput || s.hasReplace);
    s.popupShown = false;
    if (listWanted) {
        const char* chosen = drawCommandList(plot, s, barTop);
        s.popupShown = true;
        if (chosen) {
            s.popupHeld = false;
            chooseCommand(plot, s, chosen, redraw);
        } else if (s.popupHeld && !ImGui::IsMouseDown(0)) {
            s.popupHeld = false;
            s.focusInput = true;
        }
    } else {
        s.popupHeld = false;
    }

    // The loop sleeps between events; a pending focus change needs one more frame
    if ((s.focusInput || s.hasReplace || s.popupHeld) && plot->window) glfwPostEmptyEvent();

    s.wasActive = active;
    s.wasCapturing = plot->commandBoxOpen();
}

} // namespace Menu
