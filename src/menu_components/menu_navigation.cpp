// menu_navigation.cpp
// Menu navigation and selection logic
// Extracted from menu.cpp with no functional changes.

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "ini.h"
#include "menu.h"
#include "termcolor.h"

namespace Menu {

// -----------------------------------------------------------------------------
// Menu selection (ENTER / click)
// -----------------------------------------------------------------------------

bool menuSelect(Themes::IniOptions &opts) {
    if (opts.menu_level.empty()) {
        opts.menu_table = Themes::MenuTable::MAIN;
        opts.previous_level = opts.menu_level;
        opts.control_level = "close";
    }
    else if (opts.menu_level == "genomes") {
        opts.menu_table = Themes::MenuTable::GENOMES;
        opts.previous_level = opts.menu_level;
        if (opts.myIni["genomes"].size() == 0) {
            opts.menu_level.clear();
        } else {
            opts.menu_level = opts.myIni["genomes"].begin()->first;
        }
    }
    else if (opts.menu_level == "tracks") {
        opts.menu_table = Themes::MenuTable::TRACKS;
        opts.previous_level = opts.menu_level;
        if (opts.myIni["tracks"].size() == 0) {
            opts.menu_level.clear();
        } else {
            opts.menu_level = opts.myIni["tracks"].begin()->first;
        }
    }
    else if (opts.menu_level == "general") {
        opts.menu_table = Themes::MenuTable::GENERAL;
        opts.previous_level = opts.menu_level;
        opts.menu_level = opts.myIni["general"].begin()->first;
    }
    else if (opts.menu_level == "view_thresholds") {
        opts.menu_table = Themes::MenuTable::VIEW_THRESHOLDS;
        opts.previous_level = opts.menu_level;
        opts.menu_level = opts.myIni["view_thresholds"].begin()->first;
    }
    else if (opts.menu_level == "navigation") {
        opts.menu_table = Themes::MenuTable::NAVIGATION;
        opts.previous_level = opts.menu_level;
        opts.menu_level = opts.myIni["navigation"].begin()->first;
    }
    else if (opts.menu_level == "interaction") {
        opts.menu_table = Themes::MenuTable::INTERACTION;
        opts.previous_level = opts.menu_level;
        opts.menu_level = opts.myIni["interaction"].begin()->first;
    }
    else if (opts.menu_level == "labelling") {
        opts.menu_table = Themes::MenuTable::LABELLING;
        opts.previous_level = opts.menu_level;
        opts.menu_level = opts.myIni["labelling"].begin()->first;
    }
    else if (opts.menu_level == "shift_keymap") {
        opts.menu_table = Themes::MenuTable::SHIFT_KEYMAP;
        opts.previous_level = opts.menu_level;
        opts.menu_level = opts.myIni["shift_keymap"].begin()->first;
    }
    else if (opts.menu_level == "controls") {
        if (opts.control_level == "close") {
            opts.menu_level.clear();
            opts.menu_table = Themes::MenuTable::MAIN;
            return false;
        }
        else if (opts.control_level == "back") {
            opts.menu_table = Themes::MenuTable::MAIN;
            opts.menu_level.clear();
            opts.control_level = "close";
        }
        else if (opts.control_level == "save") {
            opts.menu_level.clear();
            opts.control_level = "close";
            opts.menu_table = Themes::MenuTable::MAIN;
            opts.saveIniChanges();
            std::cout << "Saved .gw.ini to " << opts.ini_path << std::endl;
            return false;
        }
        else if (opts.control_level == "delete") {
            std::string key =
                (opts.menu_table == Themes::MenuTable::GENOMES)
                    ? "genomes"
                    : "tracks";
            opts.myIni[key].remove(opts.genome_tag);
            opts.menu_level.clear();
            opts.control_level = "close";
        }
        else if (opts.control_level == "add") {
            opts.editing_underway = !opts.editing_underway;
        }
    }
    else {
        if (!opts.editing_underway) {
            opts.editing_underway = true;
        }
        else {
            opts.editing_underway = false;
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Keyboard navigation
// -----------------------------------------------------------------------------

bool navigateMenu(
    Themes::IniOptions &opts,
    int key,
    int action,
    std::string &inputText,
    int *charIndex,
    bool *captureText,
    bool *textFromSettings,
    bool *processText,
    std::string &reference_path
) {
    if (opts.ini_path.empty()) {
        std::cerr << termcolor::red << "Error:"
                  << termcolor::reset
                  << " .gw.ini file could not be read\n";
        return false;
    }

    if (*processText && !*textFromSettings) {
        *processText = false;
        return true;
    }

    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return true;

    // Up / down navigation
    if (key == GLFW_KEY_DOWN || key == GLFW_KEY_UP) {
        std::vector<std::string> keys;
        int index = -1;
        int i = 0;

        if (opts.menu_table == Themes::MenuTable::MAIN) {
            for (const auto &h : mainHeadings()) {
                keys.push_back(h);
                if (opts.menu_level == h) index = i;
                i++;
            }
        }
        else {
            std::string table = getMenuKey(opts.menu_table);
            for (auto &kv : opts.myIni[table]) {
                if (kv.first == "fmt" || kv.first == "miny") continue;
                keys.push_back(kv.first);
                if (opts.menu_level == kv.first) index = i;
                i++;
            }
        }

        if (key == GLFW_KEY_DOWN)
            index = std::min((int)keys.size() - 1, index + 1);
        else
            index = std::max(-1, index - 1);

        if (index < 0) {
            opts.menu_level = "controls";
            opts.control_level =
                availableButtonsStr(opts.menu_table).front();
        } else {
            opts.control_level.clear();
            opts.menu_level = keys[index];
        }
        return true;
    }

    // ENTER / RIGHT / TAB
    if (key == GLFW_KEY_ENTER ||
        key == GLFW_KEY_KP_ENTER ||
        key == GLFW_KEY_RIGHT ||
        key == GLFW_KEY_TAB)
    {
        bool keep = menuSelect(opts);

        if (opts.editing_underway) {
            inputText =
                opts.myIni[getMenuKey(opts.menu_table)][opts.menu_level];
            *charIndex = (int)inputText.size();
            *captureText = true;
            *textFromSettings = true;
        }
        return keep;
    }

    // LEFT / ESC
    if (key == GLFW_KEY_LEFT || key == GLFW_KEY_ESCAPE) {
        opts.menu_table = Themes::MenuTable::MAIN;
        opts.menu_level = opts.previous_level;
        opts.editing_underway = false;
        *captureText = false;
        *textFromSettings = false;
        inputText.clear();
        return true;
    }

    return true;
}

} // namespace Menu