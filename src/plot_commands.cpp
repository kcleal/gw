//
// Created by kez on 10/05/24.
//

#include <array>
#include <iomanip>
#include <iterator>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include <htslib/sam.h>
#include <htslib/hts.h>
#include <GLFW/glfw3.h>
#include "hts_funcs.h"
#include "parser.h"
#include "plot_manager.h"
#include "themes.h"


namespace Commands {

    using Plot = Manager::GwPlot*;


    bool triggerClose(Plot p) {
        p->triggerClose = true;
        p->redraw = false;
        p->processed = true;
        return false;
    }

    // Command functions can access these parameters only
    #define PARAMS [](Commands::Plot p, std::string& command, std::vector<std::string>& parts) -> bool

    // Note the function map will be cached after first call. plt is bound, but parts are updated with each call
    bool run_command_map(Plot p, std::string& command) {

        std::vector<std::string> parts = Utils::split(command, ' ');

        static std::unordered_map<std::string, std::function<bool(Plot, std::string&, std::vector<std::string>&)>> functionMap = {

                {"q", PARAMS { return triggerClose(p); }},
                {"quit", PARAMS { return triggerClose(p); }}

        };

        auto it = functionMap.find(parts[0]);
        if (it != functionMap.end()) {
            std::cout << "Command success!\n";
            return it->second(p, command, parts);  // Execute the mapped function
        } else {
            std::cout << "Command not found!\n";
            return false;
        }
    }


}