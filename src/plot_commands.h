//
// Created by kez on 10/05/24.
//

#pragma once

#include <string>
#include "plot_manager.h"

namespace Commands {

    // The var command's raw record output for an explicit Variant List row.
    bool var_record(Manager::GwPlot* p, int selectedRow, std::string& record);

    void run_command_map(Manager::GwPlot* p, std::string& command, std::ostream& out);

}
