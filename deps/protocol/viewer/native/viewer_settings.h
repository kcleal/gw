#pragma once
#include "themes.h"
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <iomanip>
namespace GwViewer {
// Only render parameters cross the wire; file paths, keymaps and host settings do not.
inline std::string renderSettings(const Themes::IniOptions& o) {
    std::ostringstream s;
#define GW_SETTING(field) s << #field << ' ' << o.field << '\n';
    GW_SETTING(font_size) GW_SETTING(ylim) GW_SETTING(max_coverage)
    GW_SETTING(indel_length) GW_SETTING(link_op) GW_SETTING(alignments)
    GW_SETTING(small_indel_threshold) GW_SETTING(snp_threshold) GW_SETTING(soft_clip_threshold)
    GW_SETTING(edge_highlights) GW_SETTING(mod_threshold) GW_SETTING(parse_mods)
    GW_SETTING(tlen_yscale) GW_SETTING(expand_tracks) GW_SETTING(log2_cov)
    GW_SETTING(scale_bar) GW_SETTING(show_translation) GW_SETTING(read_y_gap)
    GW_SETTING(splice_cluster_eps) GW_SETTING(min_junction_reads)
    GW_SETTING(tab_track_height) GW_SETTING(mods_qual_threshold)
#undef GW_SETTING
    return s.str();
}
inline void applyRenderSettings(Themes::IniOptions& o, const std::string& text) {
    if (text.size() > 4096) throw std::invalid_argument("Settings too large");
    auto next = o;
    std::istringstream s(text);
    std::string key;
    double value;
    while (s >> key) {
        if (!(s >> value) || !std::isfinite(value)) throw std::invalid_argument("Invalid display setting");
        bool found = false;
#define GW_FIELD(field, lo, hi) if (key == #field) { if(value < lo || value > hi) throw std::invalid_argument("Invalid " #field); next.field = value; found = true; }
        GW_FIELD(font_size, 6, 48) GW_FIELD(ylim, 1, 10000) GW_FIELD(max_coverage, 0, 2000000000)
        GW_FIELD(indel_length, 0, 1000000) GW_FIELD(link_op, 0, 2) GW_FIELD(alignments, 0, 1)
        GW_FIELD(small_indel_threshold, 0, 10000000) GW_FIELD(snp_threshold, 0, 10000000)
        GW_FIELD(soft_clip_threshold, 0, 10000000) GW_FIELD(edge_highlights, 0, 10000000)
        GW_FIELD(mod_threshold, 0, 10000000) GW_FIELD(parse_mods, 0, 1)
        GW_FIELD(tlen_yscale, 0, 1) GW_FIELD(expand_tracks, 0, 1) GW_FIELD(log2_cov, 0, 1)
        GW_FIELD(scale_bar, 0, 1) GW_FIELD(show_translation, 0, 1) GW_FIELD(read_y_gap, 0, 100)
        GW_FIELD(splice_cluster_eps, 0, 20) GW_FIELD(min_junction_reads, 1, 10000)
        GW_FIELD(tab_track_height, 0, 1) GW_FIELD(mods_qual_threshold, 0, 255)
#undef GW_FIELD
        if (value > 0) {
            if (key == "snp_threshold") next.myIni["view_thresholds"]["snp"] = std::to_string(int(value));
            if (key == "small_indel_threshold") next.myIni["view_thresholds"]["small_indel"] = std::to_string(int(value));
            if (key == "soft_clip_threshold") next.myIni["view_thresholds"]["soft_clip"] = std::to_string(int(value));
            if (key == "edge_highlights") next.myIni["view_thresholds"]["edge_highlights"] = std::to_string(int(value));
        }
        if (!found) throw std::invalid_argument("Unsupported display setting");
    }
    o = std::move(next);
}
}
