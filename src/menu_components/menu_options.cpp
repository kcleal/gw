// menu_options.cpp
// Option parsing and application logic
// Extracted from menu.cpp with no functional changes.

#include <filesystem>
#include <iostream>
#include <unordered_map>

#include "menu.h"
#include "glfw_keys.h"
#include "termcolor.h"
#include "utils.h"
#include "parser.h"

namespace Menu {

// -----------------------------------------------------------------------------
// Option types
// -----------------------------------------------------------------------------

enum OptionKind {
    String,
    ThemeOption,
    LinkOption,
    Bool,
    Int,
    Float,
    IntByInt,
    Path,
    KeyboardKey
};

class Option {
public:
    std::string name;
    std::string value;
    std::string table;
    OptionKind kind;
    std::vector<std::string> choices;

    Option(std::string n, OptionKind k, std::string v, Themes::MenuTable mt)
        : name(std::move(n)), value(std::move(v)), kind(k)
    {
        table = getMenuKey(mt);
        if (kind == ThemeOption)
            choices = {"dark", "igv", "slate"};
        else if (kind == LinkOption)
            choices = {"none", "sv", "all"};
    }
};

// -----------------------------------------------------------------------------
// Factory
// -----------------------------------------------------------------------------

Option optionFromStr(std::string &name,
                     Themes::MenuTable mt,
                     std::string &value)
{
    static std::unordered_map<std::string, OptionKind> map = {
        {"indel_length", Int}, {"ylim", Int},
        {"split_view_size", Int}, {"threads", Int},
        {"pad", Int}, {"soft_clip", Int},
        {"small_indel", Int}, {"snp", Int},
        {"edge_highlights", Int}, {"font_size", Int},
        {"variant_distance", Int},
        {"mods_qual_threshold", Int},

        {"scroll_speed", Float},
        {"tabix_track_height", Float},
        {"read_y_gap", Float},

        {"coverage", Bool}, {"log2_cov", Bool},
        {"expand_tracks", Bool}, {"scale_bar", Bool},
        {"vcf_as_tracks", Bool}, {"bed_as_tracks", Bool},
        {"sv_arcs", Bool}, {"mods", Bool},
        {"data_labels", Bool},

        {"scroll_right", KeyboardKey}, {"scroll_left", KeyboardKey},
        {"zoom_out", KeyboardKey}, {"zoom_in", KeyboardKey},
        {"scroll_down", KeyboardKey}, {"scroll_up", KeyboardKey},
        {"cycle_link_mode", KeyboardKey},
        {"find_alignments", KeyboardKey},
    };

    if (mt == Themes::MenuTable::GENOMES)
        return Option(name, Path, value, mt);

    if (map.count(name))
        return Option(name, map[name], value, mt);

    if (name == "theme")
        return Option(name, ThemeOption, value, mt);

    if (name == "link")
        return Option(name, LinkOption, value, mt);

    if (name == "dimensions" || name == "number")
        return Option(name, IntByInt, value, mt);

    return Option(name, String, value, mt);
}

// -----------------------------------------------------------------------------
// Apply helpers
// -----------------------------------------------------------------------------


void applyIntOption(Option &new_opt, Themes::IniOptions &opts) {
    std::string::const_iterator it = new_opt.value.begin();
    while (it != new_opt.value.end() && std::isdigit(*it)) ++it;
    if (it != new_opt.value.end()) {
        std::cerr << termcolor::red << "Error:" << termcolor::reset << " expected a positive integer number, instead of " << new_opt.value << std::endl;
    } else {
        int v = std::stoi(new_opt.value);
        if (new_opt.name == "indel_length") { opts.indel_length = std::max(1, v); }
        else if (new_opt.name == "ylim") { opts.ylim = std::max(1, v); }
        else if (new_opt.name == "split_view_size") { opts.split_view_size = std::max(1, v); }
        else if (new_opt.name == "threads") { opts.threads = std::max(1, v); }
        else if (new_opt.name == "pad") { opts.pad = std::max(1, v); }
        else if (new_opt.name == "soft_clip") { opts.soft_clip_threshold = std::max(0, v); }
        else if (new_opt.name == "small_indel") { opts.small_indel_threshold = std::max(1, v); }
        else if (new_opt.name == "snp") { opts.snp_threshold = std::max(1, v); }
        else if (new_opt.name == "mod") { opts.mod_threshold = std::max(1, v); }
        else if (new_opt.name == "edge_highlights") { opts.edge_highlights = std::max(1, v); }
        else if (new_opt.name == "font_size") { opts.font_size = std::max(1, v); }
        else if (new_opt.name == "variant_distance") { opts.variant_distance = std::max(1, v); }
        else if (new_opt.name == "mods_qual_threshold") { opts.mods_qual_threshold = std::min(std::max(0, v), 255); new_opt.value = std::to_string(opts.mods_qual_threshold); }
        else {
            std::cerr << "Error: not implemented: " << new_opt.name << std::endl;
            return;
        }
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
    }
}

void applyFloatOption(Option &new_opt, Themes::IniOptions &opts) {
    float v;
    try {
        v = std::stof(new_opt.value);
    } catch (...) {
        std::cerr << termcolor::red << "Error:" << termcolor::reset << " expected float number, instead of " << new_opt.value << std::endl;
        return;
    }
    if (new_opt.name == "scroll_speed") { opts.scroll_speed = v; }
    else if (new_opt.name == "tabix_track_height") { opts.tab_track_height = v; }
    else if (new_opt.name == "read_y_gap") { opts.read_y_gap = v; }
    else {
        std::cerr << "Error: not implemented: " << new_opt.name << std::endl;
        return;
    }
    opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
}

void applyBoolOption(Option &new_opt, Themes::IniOptions &opts) {
    std::unordered_map<std::string, bool> bool_keys;
    bool_keys = { {"1", true}, {"true", true}, {"t", true}, {"True", true}, {"on", true}, {"0", false}, {"false", false}, {"f", false}, {"False", false}, {"off", false} };
    if (bool_keys.find(new_opt.value) == bool_keys.end()) {
        std::cerr << termcolor::red << "Error:" << termcolor::reset << " expected boolean (true/1 etc), instead of " << new_opt.value << std::endl;
        return;
    }
    bool v = bool_keys[new_opt.value];
    if (new_opt.name == "scroll_spped") { opts.scroll_speed = v; }
    else if (new_opt.name == "tabix_track_height") { opts.tab_track_height = v; }
    else if (new_opt.name == "log2_cov") { opts.log2_cov = v; }
    else if (new_opt.name == "expand_tracks") { opts.expand_tracks = v; }
    else if (new_opt.name == "scale_bar") { opts.scale_bar = v; }
    else if (new_opt.name == "vcf_as_tracks") { opts.vcf_as_tracks = v; }
    else if (new_opt.name == "bed_as_tracks") { opts.bed_as_tracks = v; }
    else if (new_opt.name == "coverage") { opts.max_coverage = (v) ? 1410065408 : 0; }
    else if (new_opt.name == "sv_arcs") { opts.sv_arcs = v; }
    else if (new_opt.name == "mods") { opts.parse_mods = v; }
    else if (new_opt.name == "data_labels") { opts.data_labels = v; std::cout << " YO\n";}
    else {
        std::cerr << "Error: not implemented: " << new_opt.name << std::endl;
        return;
    }
    opts.myIni[new_opt.table][new_opt.name] = (v) ? "true" : "false";
}

void applyKeyboardKeyOption(Option &new_opt, Themes::IniOptions &opts) {
    std::unordered_map<std::string, int> keys;
    Keys::getKeyTable(keys);
    std::string k = new_opt.value;
    for(auto &c : k) { c = toupper(c); }
    if (keys.find(k) == keys.end()) {
        std::cerr << termcolor::red << "Error:" << termcolor::reset << " key " << k << " not available." << std::endl;
        return;
    }
    int v = keys[k];
    if (new_opt.name == "scroll_right") { opts.scroll_right = v; }
    else if (new_opt.name == "scroll_left") { opts.scroll_left = v; }
    else if (new_opt.name == "zoom_out") { opts.zoom_out = v; }
    else if (new_opt.name == "zoom_in") { opts.zoom_in = v; }
    else if (new_opt.name == "scroll_down") { opts.scroll_down = v; }
    else if (new_opt.name == "scroll_up") { opts.scroll_up = v; }
    else if (new_opt.name == "cycle_link_mode") { opts.cycle_link_mode = v; }
    else if (new_opt.name == "find_alignments") { opts.find_alignments = v; }
    else {
        std::cerr << "Error: not implemented: " << new_opt.name << std::endl;
        return;
    }
    opts.myIni[new_opt.table][new_opt.name] = k;
}

void applyThemeOption(Option &new_opt, Themes::IniOptions &opts) {
    if (new_opt.value == "igv") {
        opts.theme = Themes::IgvTheme();
        opts.theme_str = "igv";
    } else if (new_opt.value == "dark") {
        opts.theme = Themes::DarkTheme();
        opts.theme_str = "dark";
    } else if (new_opt.value == "slate") {
        opts.theme = Themes::SlateTheme();
        opts.theme_str = "slate";
    } else {
        std::cerr << termcolor::red << "Error:" << termcolor::reset << " theme must be 'igv', 'dark' or 'slate'" << std::endl;
        return;
    }
    opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
    opts.theme.setAlphas();
    Themes::applyImGuiTheme(opts.theme_str);
}

void applyLinkOption(Option &new_opt, Themes::IniOptions &opts) {
    if (new_opt.value == "none") {
        opts.link_op = 0;
    } else if (new_opt.value == "sv") {
        opts.link_op = 1;

    } else if (new_opt.value == "all") {
        opts.link_op = 2;
    } else {
        std::cerr << termcolor::red << "Error:" << termcolor::reset << " theme must be one of (none, sv, all)" << std::endl;
        return;
    }
    opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
    opts.link = new_opt.value;
}

void applyIntByIntOption(Option &new_opt, Themes::IniOptions &opts) {
    try {
        Utils::Dims dims = Utils::parseDimensions(new_opt.value);
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
        if (new_opt.name == "number") {
            opts.number_str = new_opt.value;
            opts.number = dims;
        } else {
            opts.dimensions_str = new_opt.value;
            opts.dimensions = dims;
            warnRestart();
        }
    } catch (...) {
        std::cerr << termcolor::red << "Error:" << termcolor::reset << " dimensions not understood" << std::endl;
    }
}

void applyPathOption(Option& new_opt, Themes::IniOptions& opts) {
    if (Utils::startsWith(new_opt.value, "http") || Utils::startsWith(new_opt.value, "ftp")) {
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
    } else {
        std::string new_opt_path = Parse::tilde_to_home(new_opt.value);
        std::filesystem::path path = new_opt_path;
        if (std::filesystem::exists(path)) {
            opts.myIni[new_opt.table][new_opt.name] = new_opt_path;
        } else {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " local path does not exist: " << path << std::endl;
        }
    }
}

void applyStringOption(Option &new_opt, Themes::IniOptions &opts) {
    if (new_opt.name == "font") { opts.font_str = new_opt.value; }
    else if (new_opt.name == "track_label_parser_rules") { opts.track_label_parser_rules = new_opt.value; }
    opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
    if (new_opt.name == "labels" || new_opt.name == "parse_label" || new_opt.table == "shift_keymap") {
        warnRestart();
    }
}


// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------

void processTextEntry(Themes::IniOptions &opts,
                      std::string &inputText)
{
    if (opts.menu_level == "controls")
        return;

    Option opt =
        optionFromStr(opts.menu_level,
                      opts.menu_table,
                      inputText);

    switch (opt.kind) {
        case Int:        /* applyIntOption */ break;
        case Float:      /* applyFloatOption */ break;
        case Bool:       /* applyBoolOption */ break;
        case KeyboardKey:/* applyKeyboardKeyOption */ break;
        case ThemeOption:/* applyThemeOption */ break;
        case LinkOption: /* applyLinkOption */ break;
        case IntByInt:   /* applyIntByIntOption */ break;
        case Path:       applyPathOption(opt, opts); break;
        case String:     applyStringOption(opt, opts); break;
    }
}

} // namespace Menu