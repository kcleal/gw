// menu_commands.cpp
// Command switch state helpers
// Extracted from menu.cpp with no functional changes.

#include <string>

#include "menu.h"
#include "themes.h"

namespace Menu {

int getCommandSwitchValue(Themes::IniOptions &opts,
                          std::string &cmd_s,
                          bool &drawLine)
{
    if (cmd_s == "tlen-y") {
        return (int)opts.tlen_yscale;
    } else if (cmd_s == "soft-clips") {
        return (int)(opts.soft_clip_threshold > 0);
    } else if (cmd_s == "mismatches") {
        return (int)(opts.snp_threshold > 0);
    } else if (cmd_s == "log2-cov") {
        return (int)(opts.log2_cov);
    } else if (cmd_s == "expand-tracks") {
        return (int)(opts.expand_tracks);
    } else if (cmd_s == "line") {
        return (int)drawLine;
    } else if (cmd_s == "insertions") {
        return (int)(opts.small_indel_threshold > 0);
    } else if (cmd_s == "edges") {
        return (int)(opts.edge_highlights > 0);
    } else if (cmd_s == "cov") {
        return (int)(opts.max_coverage > 0);
    } else if (cmd_s == "mods") {
        return (int)(opts.parse_mods);
    } else if (cmd_s == "alignments") {
        return (int)(opts.alignments);
    } else if (cmd_s == "data_labels") {
        return (int)(opts.data_labels);
    }
    return -1;
}

} // namespace Menu
