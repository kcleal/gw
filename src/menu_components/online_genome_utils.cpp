#include "menu_components/online_genome_utils.h"

#if !defined(__EMSCRIPTEN__)

#include <regex>
#include <cctype>

namespace Menu {

std::vector<OnlineGenome> parseIgvGenomes(const std::string& jsonText) {
    std::vector<OnlineGenome> result;

    // Find array content between [ and ]
    size_t arrStart = jsonText.find('[');
    size_t arrEnd   = jsonText.rfind(']');
    if (arrStart == std::string::npos || arrEnd == std::string::npos || arrEnd <= arrStart)
        return result;

    // Split top-level objects by tracking brace depth, respecting strings.
    std::vector<std::string> objects;
    std::string current;
    int depth = 0;
    bool inString = false;
    bool escape = false;

    for (size_t i = arrStart + 1; i < arrEnd; ++i) {
        char ch = jsonText[i];
        if (escape) {
            current += ch;
            escape = false;
            continue;
        }
        if (ch == '\\') {
            current += ch;
            escape = true;
            continue;
        }
        if (ch == '"' && !inString) {
            inString = true;
            current += ch;
            continue;
        }
        if (ch == '"' && inString) {
            inString = false;
            current += ch;
            continue;
        }
        if (inString) {
            current += ch;
            continue;
        }
        if (ch == '{') {
            ++depth;
            current += ch;
        } else if (ch == '}') {
            --depth;
            current += ch;
            if (depth == 0) {
                // trim whitespace
                size_t a = 0, b = current.size();
                while (a < b && std::isspace(static_cast<unsigned char>(current[a]))) ++a;
                while (b > a && std::isspace(static_cast<unsigned char>(current[b - 1]))) --b;
                if (a < b)
                    objects.emplace_back(current.substr(a, b - a));
                current.clear();
            }
        } else if (ch == ',' && depth == 0) {
            // skip separator between top-level objects
        } else {
            current += ch;
        }
    }

    static const std::regex idRe(R"--("id"\s*:\s*"([^"]*)")--");
    static const std::regex nameRe(R"--("name"\s*:\s*"([^"]*)")--");
    static const std::regex fastaRe(R"--("fastaURL"\s*:\s*"([^"]*)")--");

    for (const std::string& body : objects) {
        std::smatch m;
        OnlineGenome g;
        if (std::regex_search(body, m, idRe))    g.id = m[1];
        if (std::regex_search(body, m, nameRe))  g.name = m[1];
        if (std::regex_search(body, m, fastaRe)) g.fastaURL = m[1];
        if (!g.id.empty() && !g.fastaURL.empty())
            result.push_back(std::move(g));
    }
    return result;
}

}

#endif // !defined(__EMSCRIPTEN__)
