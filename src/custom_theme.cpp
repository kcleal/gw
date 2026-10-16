#include "themes.h"
#include <fstream>
#include <stdexcept>

namespace Themes {
    const std::vector<ThemeColor>& themeColors() {
        static const std::vector<ThemeColor> colors = {
            {"BACKGROUND", "Background", "Background and text", GwPaint::bgPaint},
            {"BACKGROUND_TILED", "Background tiled", "Background and text", GwPaint::bgPaintTiled},
            {"BACKGROUND_MENU", "Background menu", "Background and text", GwPaint::bgMenu},
            {"NORMAL_READ", "Normal read", "Reads", GwPaint::fcNormal},
            {"DELETION", "Deletion", "Reads", GwPaint::fcDel},
            {"DUPLICATION", "Duplication", "Reads", GwPaint::fcDup},
            {"INVERSION_FORWARD", "Inversion forward", "Reads", GwPaint::fcInvF},
            {"INVERSION_REVERSE", "Inversion reverse", "Reads", GwPaint::fcInvR},
            {"TRANSLOCATION", "Translocation", "Reads", GwPaint::fcTra},
            {"INSERTION", "Insertion", "Reads", GwPaint::fcIns},
            {"SOFT_CLIP", "Soft clip", "Reads", GwPaint::fcSoftClip},
            {"NUCLEOTIDE_A", "Nucleotide A", "Nucleotides", GwPaint::fcA},
            {"NUCLEOTIDE_T", "Nucleotide T", "Nucleotides", GwPaint::fcT},
            {"NUCLEOTIDE_C", "Nucleotide C", "Nucleotides", GwPaint::fcC},
            {"NUCLEOTIDE_G", "Nucleotide G", "Nucleotides", GwPaint::fcG},
            {"NUCLEOTIDE_N", "Nucleotide N", "Nucleotides", GwPaint::fcN},
            {"COVERAGE", "Coverage", "Coverage and tracks", GwPaint::fcCoverage},
            {"TRACK", "Track", "Coverage and tracks", GwPaint::fcTrack},
            {"NORMAL_READ_MQ0", "Normal read MQ0", "Mapping quality zero", GwPaint::fcNormal0},
            {"DELETION_MQ0", "Deletion MQ0", "Mapping quality zero", GwPaint::fcDel0},
            {"DUPLICATION_MQ0", "Duplication MQ0", "Mapping quality zero", GwPaint::fcDup0},
            {"INVERSION_FORWARD_MQ0", "Inversion forward MQ0", "Mapping quality zero", GwPaint::fcInvF0},
            {"INVERSION_REVERSE_MQ0", "Inversion reverse MQ0", "Mapping quality zero", GwPaint::fcInvR0},
            {"TRANSLOCATION_MQ0", "Translocation MQ0", "Mapping quality zero", GwPaint::fcTra0},
            {"SOFT_CLIP_MQ0", "Soft clip MQ0", "Mapping quality zero", GwPaint::fcSoftClip0},
            {"BIGWIG", "BigWig", "Coverage and tracks", GwPaint::fcBigWig},
            {"REGION_OF_INTEREST", "Region of interest", "Coverage and tracks", GwPaint::fcRoi},
            {"MATE_PRIMARY", "Mate primary", "Mate palette (uniform color)", GwPaint::mate_fc},
            {"MATE_SECONDARY", "Mate secondary", "Mate palette (uniform color)", GwPaint::mate_fc0},
            {"MATE_UNMAPPED", "Mate unmapped", "Lines and highlights", GwPaint::ecMateUnmapped},
            {"SPLIT_READ", "Split read", "Lines and highlights", GwPaint::ecSplit},
            {"SELECTED_ELEMENT", "Selected element", "Lines and highlights", GwPaint::ecSelected},
            {"LINE_JOINS", "Line joins", "Lines and highlights", GwPaint::lcJoins},
            {"LINE_COVERAGE", "Line coverage", "Lines and highlights", GwPaint::lcCoverage},
            {"LINE_LIGHT_JOINS", "Line light joins", "Lines and highlights", GwPaint::lcLightJoins},
            {"LINE_GTF_JOINS", "Line GTF joins", "Lines and highlights", GwPaint::lcGTFJoins},
            {"LINE_LABEL", "Line label", "Lines and highlights", GwPaint::lcLabel},
            {"LINE_BRIGHT", "Line bright", "Lines and highlights", GwPaint::lcBright},
            {"TEXT_DELETION", "Text deletion", "Background and text", GwPaint::tcDel},
            {"TEXT_INSERTION", "Text insertion", "Background and text", GwPaint::tcIns},
            {"TEXT_LABELS", "Text labels", "Background and text", GwPaint::tcLabels},
            {"TEXT_BACKGROUND", "Text background", "Background and text", GwPaint::tcBackground},
            {"MARKERS", "Markers", "Lines and highlights", GwPaint::fcMarkers},
            {"METHYLATED_C", "Methylated c", "Base modifications", GwPaint::fc5mc},
            {"HYDROXYMETHYLATED_C", "Hydroxymethylated c", "Base modifications", GwPaint::fc5hmc},
            {"OTHER_MODIFICATION", "Other modification", "Base modifications", GwPaint::fcOther},
            {"LINE_GAP", "Line gap", "Lines and highlights", GwPaint::lcGap},
            {"CODON_START", "Codon start", "Translation", GwPaint::fcCodonStart},
            {"CODON_STOP", "Codon stop", "Translation", GwPaint::fcCodonStop},
            {"CODON_OTHER", "Codon other", "Translation", GwPaint::fcCodonOther},
            {"CODON_SELECTED", "Codon selected", "Translation", GwPaint::bgCodonSelected},
        };
        return colors;
    }

    std::filesystem::path IniOptions::customThemePath() const {
        return std::filesystem::path(ini_path).parent_path() / ".gw.theme.json";
    }

    void IniOptions::rebuildTheme() {
        if (theme_str == "igv") theme = IgvTheme();
        else if (theme_str == "slate") theme = SlateTheme();
        else { theme_str = "dark"; theme = DarkTheme(); }
        if (custom_theme_enabled) theme.colorOverrides = custom_theme_colors;
        theme.setAlphas();
        ++theme_revision;
    }

    namespace {
        struct JsonValue {
            enum Type { Object, Array, String, Number } type{Object};
            std::vector<std::pair<std::string, JsonValue>> members;
            std::vector<JsonValue> items;
            std::string text;
            int number{0};
            const JsonValue& at(const std::string& key) const {
                for (const auto& m : members) if (m.first == key) return m.second;
                throw std::runtime_error("Missing theme field: " + key);
            }
        };
        // A bounded reader for the theme schema; objects, arrays, strings and
        // non-negative integers are its only value types. No external dependency.
        class ThemeJsonReader {
            std::istream& in;
            [[noreturn]] void invalid() { throw std::runtime_error("Invalid theme JSON"); }
            int peek() { in >> std::ws; return in.peek(); }
            void expect(char c) { if (peek() != c) invalid(); in.get(); }
            unsigned hex4() {
                unsigned value = 0;
                for (int i = 0; i < 4; ++i) {
                    int c = in.get(), digit = c >= '0' && c <= '9' ? c - '0' :
                        c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                    if (digit < 0) invalid();
                    value = value * 16 + digit;
                }
                return value;
            }
            void utf8(std::string& out, unsigned c) {
                if (c < 0x80) out += char(c);
                else if (c < 0x800) { out += char(0xc0 | (c >> 6)); out += char(0x80 | (c & 63)); }
                else if (c < 0x10000) {
                    out += char(0xe0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63));
                } else {
                    out += char(0xf0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 63));
                    out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63));
                }
            }
            std::string string() {
                expect('"'); std::string result;
                for (;;) {
                    int c = in.get();
                    if (c == '"') return result;
                    if (c < 32) invalid();
                    if (c != '\\') { result += char(c); continue; }
                    switch (in.get()) {
                        case '"': result += '"'; break;
                        case '\\': result += '\\'; break;
                        case '/': result += '/'; break;
                        case 'b': result += '\b'; break;
                        case 'f': result += '\f'; break;
                        case 'n': result += '\n'; break;
                        case 'r': result += '\r'; break;
                        case 't': result += '\t'; break;
                        case 'u': {
                            unsigned code = hex4();
                            if (code >= 0xd800 && code <= 0xdbff) {
                                if (in.get() != '\\' || in.get() != 'u') invalid();
                                unsigned low = hex4();
                                if (low < 0xdc00 || low > 0xdfff) invalid();
                                code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                            } else if (code >= 0xdc00 && code <= 0xdfff) invalid();
                            utf8(result, code); break;
                        }
                        default: invalid();
                    }
                }
            }
            JsonValue value(int depth) {
                if (depth > 8) invalid();
                JsonValue result;
                int ch = peek();
                if (ch == '{') {
                    in.get();
                    if (peek() != '}') for (;;) {
                        auto key = string(); expect(':');
                        for (const auto& m : result.members) if (m.first == key)
                            throw std::runtime_error("Duplicate theme field: " + key);
                        result.members.emplace_back(key, value(depth + 1));
                        if (peek() == '}') break;
                        expect(',');
                    }
                    expect('}');
                } else if (ch == '[') {
                    result.type = JsonValue::Array; in.get();
                    if (peek() != ']') for (;;) {
                        result.items.push_back(value(depth + 1));
                        if (peek() == ']') break;
                        expect(',');
                    }
                    expect(']');
                } else if (ch == '"') {
                    result.type = JsonValue::String; result.text = string();
                } else if (ch >= '0' && ch <= '9') {
                    result.type = JsonValue::Number;
                    do {
                        result.number = result.number * 10 + in.get() - '0';
                        if (result.number > 255) invalid();
                        if (ch == '0' && in.peek() >= '0' && in.peek() <= '9') invalid();
                    } while (in.peek() >= '0' && in.peek() <= '9');
                } else invalid();
                return result;
            }
        public:
            explicit ThemeJsonReader(std::istream& stream) : in(stream) {}
            JsonValue read() {
                auto result = value(0);
                if (peek() != std::char_traits<char>::eof()) invalid();
                return result;
            }
        };

        bool validBase(const std::string& base) { return base == "dark" || base == "igv" || base == "slate"; }
        std::string themeName(std::string name) {
            const auto start = name.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) throw std::runtime_error("Enter a theme name");
            name = name.substr(start, name.find_last_not_of(" \t\r\n") - start + 1);
            if (name.size() > 128) throw std::runtime_error("Theme names must be at most 128 bytes");
            for (unsigned char c : name) if (c < 32 || c == 127)
                throw std::runtime_error("Theme names cannot contain control characters");
            return name;
        }
        std::string quoteJson(const std::string& value) {
            std::string out = "\"";
            for (unsigned char c : value) {
                if (c == '"' || c == '\\') out += '\\';
                out += char(c);
            }
            return out + '"';
        }
        std::unordered_map<int, SkColor> readColors(const JsonValue& value) {
            if (value.type != JsonValue::Object) throw std::runtime_error("Expected a colors object");
            std::unordered_map<int, SkColor> colors;
            for (const auto& field : value.members) {
                const ThemeColor* entry = nullptr;
                for (const auto& c : themeColors()) if (field.first == c.name) { entry = &c; break; }
                if (!entry) throw std::runtime_error("Unknown theme color: " + field.first);
                const auto& rgba = field.second;
                if (rgba.type != JsonValue::Array || rgba.items.size() != 4)
                    throw std::runtime_error("Colors require four ARGB integers from 0 to 255");
                for (const auto& component : rgba.items) if (component.type != JsonValue::Number)
                    throw std::runtime_error("Colors require four ARGB integers from 0 to 255");
                colors[entry->paint] = SkColorSetARGB(rgba.items[0].number, rgba.items[1].number,
                                                     rgba.items[2].number, rgba.items[3].number);
            }
            return colors;
        }
        CustomTheme readNamedTheme(const JsonValue& value) {
            if (value.type != JsonValue::Object) throw std::runtime_error("Expected a named theme object");
            for (const auto& field : value.members)
                if (field.first != "name" && field.first != "base" && field.first != "colors")
                    throw std::runtime_error("Unknown theme field: " + field.first);
            const auto& name = value.at("name"); const auto& base = value.at("base");
            if (name.type != JsonValue::String || base.type != JsonValue::String || !validBase(base.text))
                throw std::runtime_error("Theme requires a name and a dark, igv, or slate base");
            return {themeName(name.text), base.text, readColors(value.at("colors"))};
        }
    }

    void IniOptions::ensureCustomThemesLoaded() {
        if (custom_theme_file_checked) return;
        custom_theme_file_checked = true;
        std::error_code error;
        if (!std::filesystem::exists(customThemePath(), error)) return;
        const auto base = theme_str;
        if (loadCustomTheme(customThemePath()) && custom_theme_enabled) rebuildTheme();
        if (!custom_theme_enabled) theme_str = base;
    }

    bool IniOptions::selectThemeByName(const std::string& name) {
        ensureCustomThemesLoaded();
        if (name == "dark" || name == "igv" || name == "slate") {
            if (custom_theme_enabled) storeCustomTheme();
            auto base = name;
            setTheme(base);
        } else {
            // A prefix makes custom names that match built-in names unambiguous.
            auto custom = name.rfind("custom:", 0) == 0 ? name.substr(7) : name;
            if (!selectCustomTheme(custom)) return false;
        }
        myIni["general"]["theme"] = theme_str;
        settings_save_pending = true;
        return true;
    }

    void IniOptions::storeCustomTheme() {
        for (auto& saved : custom_themes) if (saved.name == custom_theme_name) {
            saved.base = theme_str; saved.colors = custom_theme_colors;
            return;
        }
        custom_themes.push_back({custom_theme_name, theme_str, custom_theme_colors});
    }

    bool IniOptions::selectCustomTheme(const std::string& name) {
        if (custom_theme_enabled) storeCustomTheme();
        for (const auto& saved : custom_themes) if (saved.name == name) {
            custom_theme_name = saved.name; theme_str = saved.base; custom_theme_colors = saved.colors;
            custom_theme_enabled = true; rebuildTheme();
            settings_save_pending = true;
            return true;
        }
        return false;
    }

    bool IniOptions::createCustomTheme(const std::string& input, bool duplicate) {
        try {
            auto name = themeName(input);
            for (const auto& saved : custom_themes) if (saved.name == name)
                throw std::runtime_error("A theme with that name already exists");
            if (custom_theme_enabled || custom_themes.empty()) storeCustomTheme();
            if (duplicate && !custom_theme_enabled) {
                for (const auto& saved : custom_themes) if (saved.name == custom_theme_name) {
                    theme_str = saved.base; custom_theme_colors = saved.colors;
                    break;
                }
            }
            custom_theme_name = name;
            if (!duplicate) custom_theme_colors.clear();
            custom_theme_enabled = true; storeCustomTheme(); rebuildTheme();
            custom_theme_save_pending = settings_save_pending = true;
            custom_theme_error.clear(); return true;
        } catch (const std::runtime_error& e) { custom_theme_error = e.what(); return false; }
    }

    bool IniOptions::renameCustomTheme(const std::string& input) {
        try {
            auto name = themeName(input);
            if (name == custom_theme_name) return true;
            for (const auto& saved : custom_themes) if (saved.name == name)
                throw std::runtime_error("A theme with that name already exists");
            storeCustomTheme();
            for (auto& saved : custom_themes) if (saved.name == custom_theme_name) saved.name = name;
            custom_theme_name = name;
            custom_theme_save_pending = settings_save_pending = true;
            custom_theme_error.clear(); return true;
        } catch (const std::runtime_error& e) { custom_theme_error = e.what(); return false; }
    }

    bool IniOptions::loadCustomTheme(const std::filesystem::path& path) {
        custom_theme_file_checked = true;
        try {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in) throw std::runtime_error("Cannot open " + path.string());
            if (in.tellg() > 1024 * 1024) throw std::runtime_error("Theme JSON exceeds 1 MiB");
            in.seekg(0);
            auto root = ThemeJsonReader(in).read();
            if (root.type != JsonValue::Object) throw std::runtime_error("Expected a theme JSON object");
            std::vector<CustomTheme> themes;
            bool collection = false, single = false;
            for (const auto& f : root.members) {
                collection |= f.first == "themes" || f.first == "version";
                single |= f.first == "name" || f.first == "base" || f.first == "colors";
            }
            if (collection) {
                for (const auto& f : root.members) if (f.first != "version" && f.first != "themes")
                    throw std::runtime_error("Unknown theme collection field: " + f.first);
                const auto& version = root.at("version"); const auto& entries = root.at("themes");
                if (version.type != JsonValue::Number || version.number != 1)
                    throw std::runtime_error("Unsupported theme JSON version");
                if (entries.type != JsonValue::Array || entries.items.empty())
                    throw std::runtime_error("Theme collection must contain at least one theme");
                for (const auto& item : entries.items) themes.push_back(readNamedTheme(item));
            } else if (single) themes.push_back(readNamedTheme(root));
            else themes.push_back({"Custom", theme_str, readColors(root)}); // Legacy gwplot format.
            for (size_t i = 0; i < themes.size(); ++i)
                for (size_t j = 0; j < i; ++j) if (themes[i].name == themes[j].name)
                    throw std::runtime_error("Duplicate theme name: " + themes[i].name);
            size_t selected = 0;
            for (size_t i = 0; i < themes.size(); ++i) if (themes[i].name == custom_theme_name) selected = i;
            // Validate the entire collection before replacing any current state.
            custom_themes = std::move(themes);
            custom_theme_name = custom_themes[selected].name;
            theme_str = custom_themes[selected].base;
            custom_theme_colors = custom_themes[selected].colors;
            custom_theme_error.clear();
            return true;
        } catch (const std::runtime_error& e) { custom_theme_error = e.what(); return false;
        } catch (const std::exception& e) { custom_theme_error = e.what(); return false; }
    }

    bool IniOptions::saveCustomTheme() {
        try {
            // Disabled built-in themes must not change a saved custom theme's base.
            if (custom_theme_enabled || custom_theme_save_pending || custom_themes.empty()) storeCustomTheme();
            std::ofstream out(customThemePath());
            if (!out) throw std::runtime_error("Cannot write " + customThemePath().string());
            out << "{\n  \"version\": 1,\n  \"themes\": [";
            for (size_t i = 0; i < custom_themes.size(); ++i) {
                const auto& saved = custom_themes[i];
                if (i) out << ',';
                out << "\n    {\n      \"name\": " << quoteJson(saved.name)
                    << ",\n      \"base\": " << quoteJson(saved.base) << ",\n      \"colors\": {";
                bool first = true;
                for (const auto& entry : themeColors()) {
                    auto found = saved.colors.find(entry.paint);
                    if (found == saved.colors.end()) continue;
                    const auto c = found->second;
                    if (!first) out << ',';
                    first = false;
                    out << "\n        " << quoteJson(entry.name) << ": [" << SkColorGetA(c) << ", "
                        << SkColorGetR(c) << ", " << SkColorGetG(c) << ", " << SkColorGetB(c) << "]";
                }
                out << "\n      }\n    }";
            }
            out << "\n  ]\n}\n"; out.flush();
            if (!out) throw std::runtime_error("Cannot save theme JSON");
            custom_theme_error.clear(); return true;
        } catch (const std::runtime_error& e) { custom_theme_error = e.what(); return false;
        } catch (const std::exception& e) { custom_theme_error = e.what(); return false; }
    }

    bool IniOptions::saveSettings() {
        try {
            if (ini_path.empty()) throw std::runtime_error("No settings file configured");
            if (custom_theme_enabled || custom_theme_save_pending) {
                if (!saveCustomTheme()) throw std::runtime_error(custom_theme_error);
                custom_theme_save_pending = false;
            }
            myIni["general"]["theme"] = theme_str;
            myIni["custom_theme"]["enabled"] = custom_theme_enabled ? "true" : "false";
            myIni["custom_theme"]["name"] = custom_theme_name;
            mINI::INIFile file(ini_path);
            if (!file.write(myIni, true)) throw std::runtime_error("Cannot save " + ini_path);
#ifdef __EMSCRIPTEN__
            saveIniChanges(); // Also flush MemFS to IndexedDB.
#endif
            settings_save_pending = false;
            settings_save_error.clear();
            return true;
        } catch (const std::runtime_error& e) {
            settings_save_pending = false;
            settings_save_error = e.what();
            return false;
        } catch (const std::exception& e) {
            settings_save_pending = false; // Retry on the next edit or an explicit retry.
            settings_save_error = e.what();
            return false;
        }
    }
}
