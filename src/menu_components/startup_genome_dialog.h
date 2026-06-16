#pragma once

#include <string>
#include "themes.h"

namespace Menu {

enum class StartupResult { Genome, Session, Quit, Error };

struct StartupChoice {
    StartupResult result;
    std::string genomePath;   // valid when result == Genome
    std::string genomeTag;    // valid when result == Genome
};

// Present an interactive terminal dialog for choosing a reference genome when
// GW is started without one. Returns the selected genome, a request to load the
// previous session, or a quit/error signal. On non-interactive builds
// (Emscripten) this falls back to the non-interactive path and returns Error.
StartupChoice runStartupGenomeDialog(Themes::IniOptions& iopts);

}
