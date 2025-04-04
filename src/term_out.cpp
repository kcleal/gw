//
// Created by Kez Cleal on 07/12/2022.
// Collection of terminal printing functions
//
#include <cassert>
#include <filesystem>
#include <htslib/sam.h>
#include <string>
#include <iterator>
#include <regex>
#include <vector>
#include <unordered_set>

#if !defined(__EMSCRIPTEN__)
    #include <curl/curl.h>
    #include <curl/easy.h>
#endif

#include "htslib/hts.h"
#include "drawing.h"
#include "hts_funcs.h"
#include "plot_manager.h"
#include "segments.h"
#include "ankerl_unordered_dense.h"
#include "termcolor.h"
#include "term_out.h"
#include "themes.h"
#include "gw_version.h"


namespace Term {

    void help(Themes::IniOptions &opts, std::ostream& out) {
        out << termcolor::italic << "\n* Select the GW window (not the terminal) and type '/' or ':' to enter a command\n" << termcolor::reset;
        out << termcolor::italic << "\n* Get more help using 'man [COMMAND]' or 'help [COMMAND]'\n" << termcolor::reset;
        out << termcolor::underline << "\nCommand          Modifier        Description                                            \n" << termcolor::reset;
        out << termcolor::green << "[locus]                          " << termcolor::reset << "e.g. 'chr1' or 'chr1:1-20000'\n";
        out << termcolor::green << "add              region(s)       " << termcolor::reset << "Add one or more regions e.g. 'add chr1:1-20000'\n";
        out << termcolor::green << "alignments                       " << termcolor::reset << "Turn display alignments on or off\n";
        out << termcolor::green << "colour           name + ARGB     " << termcolor::reset << "Set a colour for a plot component\n";
        out << termcolor::green << "count            expression?     " << termcolor::reset << "Count reads. See filter for example expressions'\n";
        out << termcolor::green << "cov              value?          " << termcolor::reset << "Change max coverage value. Use 'cov' to toggle coverage\n";
        out << termcolor::green << "edges                            " << termcolor::reset << "Toggle edges\n";
        out << termcolor::green << "expand-tracks                    " << termcolor::reset << "Toggle showing expanded tracks\n";
        out << termcolor::green << "filter           expression      " << termcolor::reset << "Examples 'filter mapq > 0', 'filter ~flag & secondary'\n                                 'filter mapq >= 30 or seq-len > 100'\n";
        out << termcolor::green << "find, f          qname?          " << termcolor::reset << "To find other alignments from selected read use 'find'\n                                 Or use 'find [QNAME]' to find target read'\n";
        out << termcolor::green << "goto             loci/feature    " << termcolor::reset << "e.g. 'goto chr1:1-20000'. 'goto hTERT' \n";
        out << termcolor::green << "grid             width x height  " << termcolor::reset << "Set the grid size for --variant images 'grid 8x8' \n";
        out << termcolor::green << "header           names?          " << termcolor::reset << "Prints the header of the current bam to terminal\n";
        out << termcolor::green << "indel-length     int             " << termcolor::reset << "Label indels >= length\n";
        out << termcolor::green << "insertions, ins                  " << termcolor::reset << "Toggle insertions\n";
        out << termcolor::green << "label                            " << termcolor::reset << "Toggle data labels\n";
        out << termcolor::green << "line                             " << termcolor::reset << "Toggle mouse position vertical line\n";
        out << termcolor::green << "link             none/sv/all     " << termcolor::reset << "Switch read-linking 'link all'\n";
        out << termcolor::green << "load             type? file      " << termcolor::reset << "Load bams, tracks, tiles, session file or ideogram\n";
        out << termcolor::green << "log2-cov                         " << termcolor::reset << "Toggle scale coverage by log2\n";
        out << termcolor::green << "mate             add?            " << termcolor::reset << "Use 'mate' to navigate to mate-pair, or 'mate add' \n                                 to add a new region with mate \n";
        out << termcolor::green << "mismatches, mm                   " << termcolor::reset << "Toggle mismatches\n";
        out << termcolor::green << "online                           " << termcolor::reset << "Show links to online genome browsers\n";
        out << termcolor::green << "quit, q          -               " << termcolor::reset << "Quit GW\n";
        out << termcolor::green << "refresh, r       -               " << termcolor::reset << "Refresh and re-draw the window\n";
        out << termcolor::green << "remove, rm       index           " << termcolor::reset << "Remove a region by index e.g. 'rm 1'. To remove a bam \n                                 use the bam index 'rm bam1', or track 'rm track1'\n";
        out << termcolor::green << "roi              region? name?   " << termcolor::reset << "Add a region of interest\n";
        out << termcolor::green << "sam                              " << termcolor::reset << "Print selected read to screen or save to file\n";
        out << termcolor::green << "save             filename        " << termcolor::reset << "Save reads (bam/cram), snapshot (png/pdf/svg), session (ini), or labels (tsv)\n";
        out << termcolor::green << "settings                         " << termcolor::reset << "Open the settings menu'\n";
		out << termcolor::green << "snapshot, s      path?           " << termcolor::reset << "Save current window as image e.g. 's', or 's view.png',\n                                 or vcf columns can be used 's {pos}_{info.SU}.png'\n";
        out << termcolor::green << "soft-clips, sc                   " << termcolor::reset << "Toggle soft-clips\n";
        out << termcolor::green << "sort             strand/hap/pos  " << termcolor::reset << "Sort reads by strand, haplotype, and/or pos\n";
        out << termcolor::green << "tags                             " << termcolor::reset << "Print selected sam tags\n";
        out << termcolor::green << "theme            igv/dark/slate  " << termcolor::reset << "Switch color theme e.g. 'theme dark'\n";
        out << termcolor::green << "tlen-y                           " << termcolor::reset << "Toggle --tlen-y option\n";
        out << termcolor::green << "var, v           vcf_column?     " << termcolor::reset << "Print variant information e.g. 'var', 'var info',\n                                 or a list of columns 'var pos qual format.SU'\n";
        out << termcolor::green << "ylim             number          " << termcolor::reset << "The maximum y-limit for the image e.g. 'ylim 100'\n";

        out << termcolor::underline << "\nHot keys                      \n" << termcolor::reset;
        out << "scroll left          " << termcolor::bright_yellow; Term::printKeyFromValue(opts.scroll_left, out); out << "\n" << termcolor::reset;
        out << "scroll right         " << termcolor::bright_yellow; Term::printKeyFromValue(opts.scroll_right, out); out << "\n" << termcolor::reset;
        out << "scroll down          " << termcolor::bright_yellow; Term::printKeyFromValue(opts.scroll_down, out); out << ", CTRL + [\n" << termcolor::reset;
        out << "scroll up            " << termcolor::bright_yellow; Term::printKeyFromValue(opts.scroll_up, out); out << ", CTRL + ]\n" << termcolor::reset;
        out << "zoom in              " << termcolor::bright_yellow; Term::printKeyFromValue(opts.zoom_in, out); out << "\n" << termcolor::reset;
        out << "zoom out             " << termcolor::bright_yellow; Term::printKeyFromValue(opts.zoom_out, out); out << "\n" << termcolor::reset;
        out << "zoom to cursor       " << termcolor::bright_yellow; out << "CTRL + LEFT_MOUSE" << "\n" << termcolor::reset;
        out << "sort at cursor       " << termcolor::bright_yellow; out << "S" << "\n" << termcolor::reset;
        out << "decrease ylim        " << termcolor::bright_yellow; out << "CTRL + KEY_MINUS" << "\n" << termcolor::reset;
        out << "increase ylim        " << termcolor::bright_yellow; out << "CTRL + KEY_EQUALS" << "\n" << termcolor::reset;
        out << "next region view     " << termcolor::bright_yellow; Term::printKeyFromValue(opts.next_region_view, out); out << "\n" << termcolor::reset;
        out << "previous region view " << termcolor::bright_yellow; Term::printKeyFromValue(opts.previous_region_view, out); out << "\n" << termcolor::reset;
        out << "cycle link mode      " << termcolor::bright_yellow; Term::printKeyFromValue(opts.cycle_link_mode, out); out << "\n" << termcolor::reset;
        out << "find all alignments  " << termcolor::bright_yellow; Term::printKeyFromValue(opts.find_alignments, out); out << "\n" << termcolor::reset;
        out << "repeat last command  " << termcolor::bright_yellow; out << "CTRL + "; Term::printKeyFromValue(opts.repeat_command, out); out << "\n" << termcolor::reset;
        out << "resize window        " << termcolor::bright_yellow; out << "SHIFT + ARROW_KEY" << "\n" << termcolor::reset;
        out << "switch viewing mode  " << termcolor::bright_yellow; out << "TAB" << "\n" << termcolor::reset;

        out << "\n";
    }

    void manuals(std::string &s, std::ostream& out) {
        out << "\nManual for command '" << s << "'\n";
        out << "--------------------"; for (int i=0; i<(int)s.size(); ++i) out << "-"; out << "-\n\n";
        if (s == "[locus]" || s == "locus") {
            out << "    Navigate to a genomic locus.\n        You can use chromosome names or chromosome coordinates.\n    Examples:\n        'chr1:1-20000', 'chr1', 'chr1:10000'\n\n";
        } else if (s == "add") {
            out << "    Add a genomic locus.\n        This will add a new locus to the right-hand-side of your view.\n    Examples:\n        'add chr1:1-20000', 'add chr2'\n\n";
        } else if (s == "alignments") {
            out << "    Toggle alignments.\n        Alignments will be shown or hidden.\n\n";
        } else if (s == "colour" || s == "color") {
            out << "    Set the (alpha, red, green, blue) colour for one of the plot elements.\n"
                   "        Elements are selected by name (see below) and values are in the range [0, 255].\n"
                   "        For example 'colour fcNormal 255 255 0 0' sets face-colour of normal reads to red.\n"
                   "        Also, tracks can be coloured using thier index e.g. 'colour track0 255 0 200 0' sets \n"
                   "        the face-colour of track 0 to green.\n\n"
                   "          track[index]   - Paint for track at index, e.g. track0 or track1 etc\n\n"
                   "          bgPaint        - background paint\n"
                   "          bgMenu         - background of menu\n"
                   "          fcNormal       - face-colour normal reads\n"
                   "          fcDel          - face-colour deletion pattern reads\n"
                   "          fcDup          - face-colour duplication pattern reads\n"
                   "          fcInvF         - face-colour inversion-forward pattern reads\n"
                   "          fcInvR         - face-colour inversion-reverse pattern reads\n"
                   "          fcTra          - face-colour translocation pattern reads\n"
                   "          fcIns          - face-colour insertion blocks\n"
                   "          fcSoftClips    - face-colour soft-clips when zoomed-out\n"
                   "          fcA            - face-colour A mismatch\n"
                   "          fcT            - face-colour T mismatch\n"
                   "          fcC            - face-colour C mismatch\n"
                   "          fcG            - face-colour G mismatch\n"
                   "          fcN            - face-colour N mismatch\n"
                   "          fcCoverage     - face-colour coverage track\n"
                   "          fcTrack        - face-colour tracks\n"
                   "          fcBigWig       - face-colour bigWig files\n"
                   "          fcNormal0      - face-colour normal reads with mapq=0\n"
                   "          fcDel0         - face-colour deletion pattern reads with mapq=0\n"
                   "          fcDup0         - face-colour duplication pattern reads with mapq=0\n"
                   "          fcInvF0        - face-colour inversion-forward pattern reads with mapq=0\n"
                   "          fcInvR0        - face-colour inversion-reverse pattern reads with mapq=0\n"
                   "          fcTra0         - face-colour translocation pattern reads with mapq=0\n"
                   "          fcIns0         - face-colour insertion blocks with mapq=0\n"
                   "          fcSoftClips0   - face-colour soft-clips when zoomed-out with mapq=0\n"
                   "          fcMarkers      - face-colour of markers\n"
                   "          fc5mc          - face-colour of 5-Methylcytosine\n"
                   "          fc5hmc         - face-colour of 5-Hydroxymethylcytosine\n"
                   "          ecMateUnmapped - edge-colour mate unmapped reads\n"
                   "          ecSplit        - edge-colour split reads\n"
                   "          ecSelected     - edge-colour selected reads\n"
                   "          lcJoins        - line-colour of joins\n"
                   "          lcCoverage     - line-colour of coverage profile\n"
                   "          lcLightJoins   - line-colour of lighter joins\n"
                   "          lcGTFJoins     - line-colour of joins for GTF/GFF3\n"
                   "          lcLabel        - line-colour of labels\n"
                   "          lcBright       - line-colour of bright edges\n"
                   "          tcDel          - text-colour of deletions\n"
                   "          tcIns          - text-colour of insertions\n"
                   "          tcLabels       - text-colour of labels\n"
                   "          tcBackground   - text-colour of background\n\n";
        } else if (s == "count") {
            out << "    Count the visible reads in each view.\n        A summary output will be displayed for each view on the screen.\n        Optionally a filter expression may be added to the command. See the man page for 'filter' for mote details\n    Examples:\n        'count', 'count flag & 2', 'count flag & proper-pair' \n\n";
        } else if (s == "cov") {
            out << "    Toggle coverage track.\n        This will turn on/off coverage tracks.\n\n";
        } else if (s == "edges" || s == "sc") {
            out << "    Toggle edge highlights.\n        Edge highlights are turned on or off.\n\n";
        } else if (s == "expand-tracks") {
                out << "    Toggle expand-tracks.\n        Features in the tracks panel are expanded so overlapping features can be seen.\n\n";
        } else if (s == "filter") {
            out << "    Filter visible reads.\n"
                         "        Reads can be filtered using an expression '{property} {operation} {value}' (the white-spaces are also needed).\n"
                         "        For example, here are some useful expressions:\n"
                         "            filter mapq >= 20\n"
                         "            filter flag & 2048\n"
                         "            filter seq contains TTAGGG\n\n"
                         "        Here are a list of '{property}' values you can use:\n"
                         "             maps, flag, ~flag, name, tlen, abs-tlen, rnext, pos, ref-end, pnext, seq, seq-len,\n"
                         "             RG, BC, BX, RX, LB, MD, MI, PU, SA, MC, NM, CM, FI, HO, MQ, SM, TC, UQ, AS\n\n"
                         "        These can be combined with '{operator}' values:\n"
                         "             &, ==, !=, >, <, >=, <=, eq, ne, gt, lt, ge, le, contains, omit\n\n"
                         "        Reads can be filtered on their mapping orientation/pattern e.g:\n"
                         "             filter pattern == del    # deletion-like pattern\n"
                         "             filter pattern == inv_f  # inversion-forward\n"
                         "             filter pattern == inv_f  # inversion-reverse\n"
                         "             filter pattern != tra    # translocation\n\n"
                         "        Bitwise flags can also be applied with named values:\n"
                         "             paired, proper-pair, unmapped, munmap, reverse, mreverse, read1, read2, secondary, dup, supplementary\n\n"
                         "             filter paired\n"
                         "             filter read1\n\n"
                         "        Expressions can be chained together providing all expressions are 'AND' or 'OR' blocks:\n"
                         "             filter mapq >= 20 and mapq < 30\n"
                         "             filter mapq >= 20 or flag & supplementary\n\n"
                         "        Finally, you can apply filters to specific panels using array indexing notation:\n"
                         "             filter mapq > 0 [:, 0]   # All rows, column 0 (all bams, first region only)\n"
                         "             filter mapq > 0 [0, :]   # Row 0, all columns (the first bam only, all regions)\n"
                         "             filter mapq > 0 [1, -1]  # Row 1, last column\n\n";
        } else if (s == "find" || s == "f") {
            out << "    Find a read with name.\n        All alignments with the same name will be highlighted with a black border\n    Examples:\n        'find D00360:18:H8VC6ADXX:1:1107:5538:24033'\n\n";
        } else if (s == "goto") {
            out << "    Navigate to a locus or track feature.\n        This moves the current region to a new view point. You can specify a genome locus, or a feature name from one of the loaded tracks\n    Examples:\n        'goto chr1'   \n        'goto hTERT'   # this will search all tracks for an entry called 'hTERT' \n\n";
        } else if (s == "grid") {
            out << "    Set the grid size.\n        Set the number of images displayed in a grid when using --variant option\n    Examples:\n        'grid 8x8'   # this will display 64 image tiles\n\n";
        } else if (s == "header") {
            out << "    Prints the header of the current selected bam to the terminal.\n"
                   "    Using 'header names' will only print the SQ lines of the header\n\n";
        } else if (s == "indel-length") {
            out << "    Set the minimum indel-length.\n        Indels (gaps in alignments) will be labelled with text if they have length >= 'indel-length'\n    Examples:\n        'indel-length 30'\n\n";
        } else if (s == "insertions" || s == "ins") {
            out << "    Toggle insertions.\n        Insertions smaller than 'indel-length' are turned on or off.\n\n";
        } else if (s == "labels") {
            out << "    Toggle data labels.\n        Text labels will be displayed next to data tracks.\n\n";
        } else if (s == "line") {
            out << "    Toggle line.\n        A vertical line will turn on/off.\n\n";
        } else if (s == "link" || s == "l") {
            out << "    Link alignments.\n        This will change how alignments are linked, options are 'none', 'sv', 'all'.\n    Examples:\n        'link sv', 'link all'\n\n";
        } else if (s == "load") {
            out << "    Load reads, tracks, tiles, session file or ideogram.\n"
                   "        The type identifier is optional, and if not supplied then the filepath extension will.\n"
                   "        will determine which type of file to load.\n\n"
                   "    Examples:\n"
                   "        'load reads.bam'        # Load reads.bam file.\n"
                   "        'load reads.cram'       # Load a cram file\n"
                   "        'load repeats.bed'      # Load a bed file\n"
                   "        'load variants.vcf'     # Load a vcf file\n"
                   "        'load session.xml'      # Load a previous session\n\n"
                   "        'load bam a.bam'        # Load alignments (bam, cram)\n"
                   "        'load track a.bed'      # Load a track (bed, vcf/bcf, gtf/gff3)\n"
                   "        'load tiled a.bed'      # Load a file to generate image-tiled from (vcf, bed)\n\n"
                   "        'load ideogram a.bed'   # Load an ideogram file (bed)\n\n"
                   "    Notes:\n"
                   "        Vcfs/bcfs/beds can be loaded as a track or image tiles. Control this behavior using the\n"
                   "        settings option Settings -> Interaction -> vcf_as_tracks and bed_as_tracks"
                   "\n\n";
        } else if (s == "log2-cov") {
            out << "    Toggle log2-coverage.\n        The coverage track will be scaled by log2.\n\n";
        } else if (s == "mate") {
            out << "    Goto mate alignment.\n        Either moves the current view to the mate locus, or adds a new view of the mate locus.\n    Examples:\n        'mate', 'mate add'\n\n";
        } else if (s == "mismatches" || s == "mm") {
            out << "    Toggle mismatches.\n        Mismatches with the reference genome are turned on or off.\n\n";
        } else if (s == "online") {
            out << "    Show links to online browsers for the current region.\n        A genome tag may need to be added e.g. 'online hg38'\n\n";
        } else if (s == "refresh" || s == "r") {
            out << "    Refresh the drawing.\n        All filters will be removed any everything will be redrawn.\n\n";
        } else if (s == "remove" || s == "rm") {
            out << "    Remove a region, bam, ideogram or track.\n"
                   "        Remove a region, bam, track or ideogram. To remove a bam or track add a 'bam' or 'track' prefix.\n"
                   "    Examples:\n"
                   "        'rm 0'         # This will remove region 0 (left-most region)\n"
                   "        'rm bam1'      # This will remove bam index 1 (second from top)'\n"
                   "        'rm track2'    # This will remove track 2 (3rd from top)\n"
                   "        'rm ideogram'  # This will remove the ideogram\n\n";
        } else if (s == "roi") {
            out << "    Add a region of interest as a new track. If no region is supplied, the visible active window is used\n    Examples:\n        'roi', 'roi chr1:1-20000'\n\n";
        } else if (s == "sam") {
            out << "    Print the sam format of the read.\n"
                   "        First select a read using the mouse then type 'sam'.\n"
                   "        The selected read can also be written or appended to a file:\n"
                   "    Examples:\n"
                   "        sam\n"
                   "        sam > single_read.sam   # Save read to a file - the header will be written\n"
                   "        sam >> collection.bam   # Append reads to an unsorted bam file\n"
                   "        sam >> collection.cram  # save reads in cram format\n\n";
        } else if (s == "save") {
            out << "    Save reads, snapshot, session file, or labels file.\n"
                         "        The filepath extension will determine the output file type.\n\n"
                         "    Examples:\n"
                         "        'save reads.bam'        # Save visible reads to reads.bam file.\n"
                         "        'save reads.bam [0, 1]' # Indexing can be used - here reads from row 0, column 1 will be saved\n"
                         "        'save reads.cram'       # Reads saved in cram format\n"
                         "        'save reads.sam'        # Reads saved in sam format (human readable)\n\n"
                         "        'save view.pdf'         # The current view is saved to view.png. Same functionality as 'snapshot'\n"
                         "        'save session.ini'      # The current session will be saved, allowing this session to be revisited\n"
                         "        'save labels.tsv'       # The output label file will be saved here\n\n"
                         "    Notes:\n"
                         "        Any read-filters are applied when saving reads\n"
                         "        Reads are saved in sorted order, however issues may arise if different bam headers\n"
                         "        are incompatible.\n"
                         "        If two regions overlap, then reads in both regions are only written once."
                         "\n\n";
		} else if (s == "snapshot" || s == "s") {
            out << "    Save an image of the screen.\n"
                         "        Saves current window. If no name is provided, the image name will be 'chrom_start_end.png', \n"
                         "        or if you are in tile-mode, the image name will be 'index_start_end.png'.\n"
                         "        Supported file extensions are .png, .pdf and .svg\n"
                         "            snapshot\n"
                         "            snapshot my_image.png\n\n"
                         "        If you have a vcf/bcf open in 'single' mode (not 'tiled') it is also possible to parse info\n"
                         "        from the vcf record. Use curley braces to select what fields to use in the filename:\n"
                         "            snapshot {pos}_{qual}.png        # parse the position and qual fields\n"
                         "            snapshot {info.SU}.png           # parse SU from info field\n"
                         "            s {format[samp1].SU}.png         # samp1 sample, SU column from format field\n\n"
                         "        Valid fields are chrom, pos, id, ref, alt, qual, filter, info, format. Just to note,\n"
                         "        you can press the repeat key (R) to repeat the last command, which can save typing this\n"
                         "        command over and over.\n\n";
        } else if (s == "soft-clips" || s == "sc") {
            out << "    Toggle soft-clips.\n        Soft-clipped bases or hard-clips are turned on or off.\n\n";
        } else if (s == "sort") {
            out << "    Sort reads by strand, haplotype (defined by HP tag in bam file), or pos.\n\n"
                   "    Examples:\n"
                   "        sort hap\n"
                   "        sort strand\n"
                   "        sort 6400234       # Sorting based on genomic position\n"
                   "        sort strand 120000 # By strand and then position\n"
                   "        sort hap 120000    # Haplotype then position\n";
        } else if (s == "tags") {
            out << "    Print selected sam tags.\n        This will print all the tags of the selected read\n\n";
        } else if (s == "theme") {
            out << "    Switch the theme.\n        Currently 'igv', 'dark' or 'slate' themes are supported.\n\n";
        } else if (s == "tlen-y") {
            out << "    Toggle --tlen-y option.\n        The --tlen-y option scales reads by template length. Applies to paired-end reads only.\n\n";
        } else if (s == "var" || s == "v") {
            out << "    Print variant information.\n"
                         "        Using 'var' will print the selected variant.\n"
                         "        If you are viewing a vcf/bcf then columns can be parsed e.g:\n"
                         "            var pos              # position\n"
                         "            var info.SU          # SU column from info\n"
                         "            v chrom pos info.SU  # list of variables to print\n"
                         "            v format.SU          # SU column from format\n"
                         "            v format[samp1].SU   # using sample name to select SU\n\n"
                         "        Valid fields are chrom, pos, id, ref, alt, qual, filter, info, format. Just to note,\n"
                         "        you can press ENTER to repeat the last command, which can save typing this\n"
                         "        command over and over.\n\n";
        } else if (s == "ylim") {
            out << "    Set the y limit.\n        The y limit is the maximum depth shown on the drawing e.g. 'ylim 100'.\n\n";
        } else {
            out << termcolor::red << "Error:" << termcolor::reset << " no manual for command " << s << std::endl;
        }
    }
    constexpr char basemap[] = {'.', 'A', 'C', '.', 'G', '.', '.', '.', 'T', '.', '.', '.', '.', '.', 'N', 'N', 'N'};


    void clearLine(std::ostream& out) {
        std::string s(Utils::get_terminal_width(), ' ');
        out << "\r" << s << std::flush;
    }

    void editInputText(std::string &inputText, const char *letter, int &charIndex) {
        if (charIndex != (int)inputText.size()) {
            inputText.insert(charIndex, letter);
            charIndex += 1;
        } else {
            inputText.append(letter);
            charIndex = inputText.size();
        }
    }

    void printCigar(std::vector<Segs::Align>::iterator r, std::ostream& out) {
        uint32_t l, cigar_l, op, k;
        uint32_t *cigar_p;
        cigar_l = r->delegate->core.n_cigar;
        cigar_p = bam_get_cigar(r->delegate);
        bool print_dots = true;
        for (k = 0; k < cigar_l; k++) {
            op = cigar_p[k] & BAM_CIGAR_MASK;
            l = cigar_p[k] >> BAM_CIGAR_SHIFT;

            if (cigar_l > 30 && !(k == 0 || k == cigar_l - 1)) {
                if (print_dots) {
                    out << " ... ";
                    print_dots = false;

                }
                continue;
            }
            switch (op) {
                case 0:
                    out << l << "M";
                    break;
                case 1:
                    out << termcolor::magenta << l << "I" << termcolor::reset;
                    break;
                case 2:
                    out << termcolor::red << l << "D" << termcolor::reset;
                    break;
                case 3:
                    out << termcolor::bright_magenta << l << "N" << termcolor::reset;
                    break;
                case 4:
                    out << termcolor::bright_blue << l << "S" << termcolor::reset;
                    break;
                case 5:
                    out << termcolor::blue << l << "H" << termcolor::reset;
                    break;
                case 6:
                    out << termcolor::blue << l << "P" << termcolor::reset;
                    break;
                case 7:
                    out << termcolor::blue << l << "=" << termcolor::reset;
                    break;
                case 8:
                    out << l << "X";
                    break;
                default:
                    out << termcolor::blue << l << "?" << termcolor::reset;
                    break;
            }
        }
    }

    void printSeq(std::vector<Segs::Align>::iterator r, const char *refSeq, int refStart, int refEnd, int max, std::ostream& out, int pos, int indel_length, bool show_mod, const char* target_mod) {
        auto l_seq = (int)r->delegate->core.l_qseq;
        if (l_seq == 0) {
            out << "*";
            return;
        }
        auto mod_it = r->any_mods.begin();
        auto mod_end = r->any_mods.end();

        uint32_t cigar_l, op, k;
        int l;
        uint32_t *cigar_p;
        cigar_l = r->delegate->core.n_cigar;
        cigar_p = bam_get_cigar(r->delegate);
        uint8_t *ptr_seq = bam_get_seq(r->delegate);
        int i = 0;
        int p = r->pos;
        bool started = false;
        bool done_final_op = false;
        int printed = 0;
        for (k = 0; k < cigar_l; k++) {
            op = cigar_p[k] & BAM_CIGAR_MASK;
            l = (int)(cigar_p[k] >> BAM_CIGAR_SHIFT);

            int block_end = p + l;
            bool overlaps = (pos >= p && pos <= block_end);

            if (show_mod) {
                while (mod_it != mod_end && mod_it->index < i) {
                    ++mod_it;
                }
            }

            if (op == BAM_CHARD_CLIP) {
                continue;
            } else if (op == BAM_CDEL) {
                if (started || std::abs(pos - p) < max / 2 || std::abs(pos - block_end) < max / 2 || overlaps) {
                    started = true;
                } else {
                    p += l;
                    continue;
                }
                printed += l;
                std::string str = std::to_string(l);
                if (l > max) {
                    for (int n = 0; n < 100; ++n) {
                        out << "-";
                    }
                    out << str;
                    for (int n = 0; n < 100; ++n) {
                        out << "-";
                    }
                } else if (l < (int)str.size() + 6) {
                    for (int n = 0; n < l; ++n) {
                        out << "-";
                    }
                } else {
                    int n_dash = ((l - (int)str.size()) / 2);
                    for (int n = 0; n < n_dash; ++n) {
                        out << "-";
                    }
                    out << str;
                    for (int n = 0; n < n_dash; ++n) {
                        out << "-";
                    }
                }
                p += l;
                if (printed > max) {
                    if (done_final_op) {
                        out << "...";
                        break;
                    } else {
                        done_final_op = true;
                    }
                }

            } else if (op == BAM_CMATCH) {
                if (started || std::abs(pos - p)  < max / 2 || std::abs(pos - block_end) < max / 2  || overlaps) {
                    started = true;
                } else {
                    p += l;
                    i += l;
                    continue;
                }

                printed += l;
                for (int n = 0; n < l; ++n) {
                    uint8_t base = bam_seqi(ptr_seq, i);
                    bool mm = false;
                    if (p >= refStart && p < refEnd && refSeq != nullptr && std::toupper(refSeq[p - refStart]) != basemap[base]) {
                        mm = true;
                    }
                    if (p == pos) {
                        out << termcolor::bold;
                    }
                    if (!show_mod) {
                        if (mm) {
                            out << termcolor::underline;
                            switch (basemap[base]) {
                                case 65 :
                                    out << termcolor::green << "A" << termcolor::reset;
                                    break;
                                case 67 :
                                    out << termcolor::blue << "C" << termcolor::reset;
                                    break;
                                case 71 :
                                    out << termcolor::yellow << "G" << termcolor::reset;
                                    break;
                                case 78 :
                                    out << termcolor::grey << "N" << termcolor::reset;
                                    break;
                                case 84 :
                                    out << termcolor::red << "T" << termcolor::reset;
                                    break;
                            }
                        } else {
                            switch (basemap[base]) {
                                case 65 :
                                    out << "A";
                                    break;
                                case 67 :
                                    out << "C";
                                    break;
                                case 71 :
                                    out << "G";
                                    break;
                                case 78 :
                                    out << "N";
                                    break;
                                case 84 :
                                    out << "T";
                                    break;
                            }
                        }
                    } else {
                        if (mod_it != mod_end && i == mod_it->index) {
                            for (int j=0; j < mod_it->n_mods; ++j) {
                                if (mod_it->mods[j] == *target_mod) {
                                    if (*target_mod == 'm') {
                                        out << termcolor::on_yellow << termcolor::grey;
                                    } else if (*target_mod == 'h') {
                                        out << termcolor::on_green << termcolor::grey;
                                    }
                                    out << "M" << termcolor::reset;
                                }
                            }

                            ++mod_it;
                        } else {
                            out << "_";
                        }
                    }

                    i += 1;
                    if (p == pos) {
                        out << termcolor::reset ;
                    }
                    p += 1;

                }
                if (printed > max*2) {
                    if (done_final_op) {
                        out << "...";
                        break;
                    } else {
                        done_final_op = true;
                    }
                }

            } else if (op == BAM_CEQUAL) {
                if (started || std::abs(pos - p)  < max / 2 || std::abs(pos - block_end) < max / 2  || overlaps) {
                    started = true;
                } else {
                    p += l;
                    i += l;
                    continue;
                }
                printed += l;
                for (int n = 0; n < (int)l; ++n) {
                    uint8_t base = bam_seqi(ptr_seq, i);
                    if (p == pos) {
                        out << termcolor::bold;
                    }
                    if (!show_mod) {
                        switch (basemap[base]) {
                            case 65 :
                                out << "A";
                                break;
                            case 67 :
                                out << "C";
                                break;
                            case 71 :
                                out << "G";
                                break;
                            case 78 :
                                out << "N";
                                break;
                            case 84 :
                                out << "T";
                                break;
                        }
                    } else {
                        if (mod_it != mod_end && i == mod_it->index) {

                            for (int j=0; j < mod_it->n_mods; ++j) {
                                if (mod_it->mods[j] == *target_mod) {
                                    if (*target_mod == 'm') {
                                        out << termcolor::on_yellow << termcolor::grey;
                                    } else if (*target_mod == 'h') {
                                        out << termcolor::on_green << termcolor::grey;
                                    }
                                    out << "=" << termcolor::reset;
                                }
                            }

                            ++mod_it;
                        } else {
                            out << "_";
                        }
                    }
                    i += 1;
                    p += 1;
                    out << termcolor::reset;
                }
//                p += l;
                if (printed > max / 2) {
                    if (done_final_op) {
                        out << "...";
                        break;
                    } else {
                        done_final_op = true;
                    }
                }

            } else if (op == BAM_CDIFF || op == BAM_CINS) {

                if (started || std::abs(pos - p)  < max / 2 || std::abs(pos - block_end) < max / 2 || overlaps) {
                    started = true;
                } else {
                    if (op == BAM_CDIFF) {
                        p += l;
                    }
                    i += l;
                    continue;
                }

                if (op == BAM_CINS && l > indel_length) {
                    out << termcolor::magenta << "[" << std::to_string(l) << "]" << termcolor::reset;
                }
                for (int n = 0; n < l; ++n) {

                    if (!show_mod) {
                        uint8_t base = bam_seqi(ptr_seq, i);
                        switch (basemap[base]) {
                            case 65 :
                                out << termcolor::green << "A" << termcolor::reset;
                                break;
                            case 67 :
                                out << termcolor::blue << "C" << termcolor::reset;
                                break;
                            case 71 :
                                out << termcolor::yellow << "G" << termcolor::reset;
                                break;
                            case 78 :
                                out << termcolor::grey << "N" << termcolor::reset;
                                break;
                            case 84 :
                                out << termcolor::red << "T" << termcolor::reset;
                                break;
                        }
                    } else {
                        if (mod_it != mod_end && i == mod_it->index) {
                            char o = (op == BAM_CINS) ? 'I' : 'X';

                            for (int j=0; j < mod_it->n_mods; ++j) {
                                if (mod_it->mods[j] == *target_mod) {
                                    if (*target_mod == 'm') {
                                        out << termcolor::on_yellow << termcolor::grey;
                                    } else if (*target_mod == 'h') {
                                        out << termcolor::on_green << termcolor::grey;
                                    }
                                    out << o << termcolor::reset;
                                }
                            }

                            ++mod_it;
                        } else {
                            out << "_";
                        }
                    }
                    i += 1;
                }
                if (op == BAM_CDIFF) {
                    p += l;
                    printed += l;
                }
                if (printed > max) {
                    if (done_final_op) {
                        out << "...";
                        break;
                    } else {
                        done_final_op = true;
                    }
                }

            } else {  // soft-clips
                if (k == 0) {
                    if (pos - p > max) {
                        i = l;
                        continue;
                    } else {
                        started = true;
                    }
                }
                int n = 0;
                int stop = l;
                if (l > max) {
                    if (k == 0) {
                        n = (int)l - max;
                        out << "...";
                    } else {
                        stop = max;
                    }
                }
                i += n;
                for (; n < stop; ++n) {  // soft-clips
                    if (!show_mod) {
                        uint8_t base = bam_seqi(ptr_seq, i);
                        switch (basemap[base]) {
                            case 65 :
                                out << termcolor::green << "A" << termcolor::reset;
                                break;
                            case 67 :
                                out << termcolor::blue << "C" << termcolor::reset;
                                break;
                            case 71 :
                                out << termcolor::yellow << "G" << termcolor::reset;
                                break;
                            case 78 :
                                out << termcolor::grey << "N" << termcolor::reset;
                                break;
                            case 84 :
                                out << termcolor::red << "T" << termcolor::reset;
                                break;
                        }
                    } else {
                        if (mod_it != mod_end && i == mod_it->index) {
                            for (int j=0; j < mod_it->n_mods; ++j) {
                                if (mod_it->mods[j] == *target_mod) {
                                    if (*target_mod == 'm') {
                                        out << termcolor::on_yellow << termcolor::grey;
                                    } else if (*target_mod == 'h') {
                                        out << termcolor::on_green << termcolor::grey;
                                    }
                                    out << "S" << termcolor::reset;
                                }
                            }
                            ++mod_it;
                        } else {
                            out << "_";
                        }
                    }
                    i += 1;
                }
                if (l > max && k > 0) {
                    out << "...";
                }
            }
        }
    }

    void read2sam(std::vector<Segs::Align>::iterator r, const sam_hdr_t* hdr, std::string &sam, bool low_mem, std::ostream& out) {
        kstring_t kstr = {0, 0, nullptr};
        int res = sam_format1(hdr, r->delegate, &kstr);
        if (res < 0) {
            out << termcolor::red << "Error:" << termcolor::reset << " failed to parse alignment record\n";
        }
        sam = kstr.s;
        ks_free(&kstr);
        return;
    }

    void printRead(std::vector<Segs::Align>::iterator r, const sam_hdr_t* hdr, std::string &sam, const char *refSeq, int refStart, int refEnd, bool low_mem, std::ostream& out, int pos, int indel_length, bool show_mod) {
        const char *rname = sam_hdr_tid2name(hdr, r->delegate->core.tid);
        const char *rnext = sam_hdr_tid2name(hdr, r->delegate->core.mtid);

        int term_width = std::max(Utils::get_terminal_width() - 9, 50);

        out << std::endl << std::endl;
        out << termcolor::bold << "qname    " << termcolor::reset << bam_get_qname(r->delegate) << std::endl;
        out << termcolor::bold << "span     " << termcolor::reset << rname << ":" << r->pos << "-" << r->reference_end << std::endl;
        if (rnext) {
            out << termcolor::bold << "mate     " << termcolor::reset << rnext << ":" << r->delegate->core.mpos << std::endl;
        }
        out << termcolor::bold << "flag     " << termcolor::reset << r->delegate->core.flag << std::endl;
        out << termcolor::bold << "mapq     " << termcolor::reset << (int)r->delegate->core.qual << std::endl;
        out << termcolor::bold << "len      " << termcolor::reset << (int)r->delegate->core.l_qseq << std::endl;
        out << termcolor::bold << "cigar    " << termcolor::reset; printCigar(r, out); out << std::endl;
        out << termcolor::bold << "seq      " << termcolor::reset; printSeq(r, refSeq, refStart, refEnd, term_width, out, pos, indel_length, false, ""); out << std::endl << std::endl;

        if (show_mod) {
            std::unordered_set<char> mods;  // make a list of mods in this read
            for (const auto& m : r->any_mods) {
                for (int n=0; n < m.n_mods; ++n) {
                    mods.insert(m.mods[n]);
                }
            }
            for (const auto& mod_type : mods) {

                out << termcolor::bold << "mod '" << mod_type << "'  " << termcolor::reset; printSeq(r, refSeq, refStart, refEnd, term_width, out, pos, indel_length, true, &mod_type); out << std::endl << std::endl;
            }
//
        }
        read2sam(r, hdr, sam, low_mem, out);
    }

    void printSelectedSam(std::string &sam, std::ostream& out) {
        out << std::endl << sam << std::endl << std::endl;
    }

    void printKeyFromValue(int v, std::ostream& out) {
        std::unordered_map<std::string, int> key_table;
        Keys::getKeyTable(key_table);
        for (auto &p: key_table) {
            if (p.second == v) {
                out << p.first;
                break;
            }
        }
    }

    void printRefSeq(Utils::Region *region, float x, float xOffset, float xScaling, std::ostream& out) {
        if (region == nullptr) {
            return;
        }
        float min_x = xOffset;
        float max_x = xScaling * ((float)(region->end - region->start)) + min_x;
        int size = region->end - region->start;

        if (region->end - region->start > region->refSeqLen) {
            if (region->end < region->chromLen) {
                return;  // refseq not fully loaded from region. user needs to zoom in to fetch refseq
            }
        }
        if (x > min_x && x < max_x && size <= 20000) {
            const char *s = region->refSeq;
            if (s == nullptr) {
                return;
            }
            out << "\n\n>" << region->chrom << ":" << region->start << "-" << region->end << "\n";
            while (*s) {
                switch ((unsigned int)*s) {
                    case 65: out << termcolor::green << "a"; break;
                    case 67: out << termcolor::blue << "c"; break;
                    case 71: out << termcolor::yellow << "g"; break;
                    case 78: out << termcolor::bright_grey << "n"; break;
                    case 84: out << termcolor::red << "t"; break;
                    case 97: out << termcolor::green << "A"; break;
                    case 99: out << termcolor::blue << "C"; break;
                    case 103: out << termcolor::yellow << "G"; break;
                    case 110: out << termcolor::bright_grey << "N"; break;
                    case 116: out << termcolor::red << "T"; break;
                    default: out << "?"; break;
                }
                ++s;
            }
            out << termcolor::reset << std::endl << std::endl;
            return;
        }
    }

    std::string intToStringCommas(int pos) {
        auto s = std::to_string(pos + 1);
        int n = (int)s.length() - 3;
        int end = (pos + 1 >= 0) ? 0 : 1;
        while (n > end) {
            s.insert(n, ",");
            n -= 3;
        }
        return s;
    }

	void printCoverage(int pos, Segs::ReadCollection &cl, std::ostream& out) {
        if (cl.readQueue.empty()) {
            return;
        }
		auto bnd = std::lower_bound(cl.readQueue.begin(), cl.readQueue.end(), pos,
		                       [&](const Segs::Align &lhs, const int pos) {
			return (int)lhs.pos <= pos;
		});
		int mA = 0, mT = 0, mC = 0, mG = 0, mN = 0;  // mismatch
		int A = 0, T = 0, C = 0, G = 0, N = 0;  // ref
		std::vector <int> dels;
		int cnt = 0;
		while (true) {
			cnt += 1;
			if (cnt > 50000) {
				break;
			}
			if ((int)bnd->pos <= pos && pos <= (int)bnd->reference_end) {
				Segs::Align &align = *bnd;
				uint32_t r_pos = align.pos;
                if (align.delegate == nullptr) {
                    if (bnd == cl.readQueue.begin()) {
                        break;
                    }
                    --bnd;
                    continue;
                }
				uint32_t cigar_l = align.delegate->core.n_cigar;
				uint8_t *ptr_seq = bam_get_seq(align.delegate);
				uint32_t *cigar_p = bam_get_cigar(align.delegate);
                if (cigar_p == nullptr || cigar_l == 0 || ptr_seq == nullptr) {
                    if (bnd == cl.readQueue.begin()) {
                        break;
                    }
                    --bnd;
                    continue;
                }
				int r_idx;
				uint32_t idx = 0;
				const char *refSeq = cl.region->refSeq;
				if (refSeq == nullptr) {
					return;
				}
				int rlen = cl.region->end - cl.region->start;
				int op, l;

				for (int k = 0; k < (int)cigar_l; k++) {
					op = cigar_p[k] & BAM_CIGAR_MASK;
					l = cigar_p[k] >> BAM_CIGAR_SHIFT;
					if (op == BAM_CSOFT_CLIP || op == BAM_CINS) {
						idx += l;
						continue;
					}
					else if (op == BAM_CDEL) {
						if (r_pos <= (uint32_t)pos && (uint32_t)pos < r_pos + l) {
							dels.push_back(l);
						}
						r_pos += l;
						continue;
					}
					else if (op == BAM_CREF_SKIP) {
						r_pos += l;
						continue;
					}
					else if (op == BAM_CHARD_CLIP || op == BAM_CEQUAL) {
						continue;
					}
					else if (op == BAM_CDIFF) {
						for (int i=0; i < l; ++i) {
							if (r_pos == (uint32_t)pos) {
								char bam_base = bam_seqi(ptr_seq, idx);
								switch (bam_base) {
									case 1: mA+=1; break;
									case 2: mC+=1; break;
									case 4: mG+=1; break;
									case 8: mT+=1; break;
									default: mN+=1; break;
								}
							}
							idx += 1;
							r_pos += 1;
						}
					}
					else {  // BAM_CMATCH
						// A==1, C==2, G==4, T==8, N==>8
						for (int i=0; i < l; ++i) {
							if (r_pos != (uint32_t)pos) {
								idx += 1;
								r_pos += 1;
								continue;
							}
							r_idx = (int)r_pos - cl.region->start;
							if (r_idx < 0) {
								idx += 1;
								r_pos += 1;
								continue;
							}
							if (r_idx >= rlen) {
								break;
							}
//							char ref_base;
//							switch (refSeq[r_idx]) {
//								case 'A': ref_base = 1; break;
//								case 'C': ref_base = 2; break;
//								case 'G': ref_base = 4; break;
//								case 'T': ref_base = 8; break;
//								case 'N': ref_base = 15; break;
//								case 'a': ref_base = 1; break;
//								case 'c': ref_base = 2; break;
//								case 'g': ref_base = 4; break;
//								case 't': ref_base = 8; break;
//								default: ref_base = 15; break;
//							}
							char bam_base = bam_seqi(ptr_seq, idx);
//							if (bam_base != ref_base) {
                            switch (bam_base) {
                                case 1: mA+=1; break;
                                case 2: mC+=1; break;
                                case 4: mG+=1; break;
                                case 8: mT+=1; break;
                                default: mN+=1; break;
                            }
//							} else {
//								switch (bam_base) {
//									case 1: A+=1; break;
//									case 2: C+=1; break;
//									case 4: G+=1; break;
//									case 8: T+=1; break;
//									default: N+=1; break;
//								}
//							}
							idx += 1;
							r_pos += 1;
						}
					}
				}

			}
			if (bnd == cl.readQueue.begin()) {
				break;
			}
			--bnd;
		}
		int totCov = A + T + C + G + N + mA + mT + mC + mG + mN;

        int term_space = Utils::get_terminal_width();
        std::string line = "Coverage    " + std::to_string(totCov) + "      A:" + std::to_string(A) + "  T:" + std::to_string(T) + "  C:" + std::to_string(C) + "  G:" + std::to_string(T) + "     ";
        if (term_space < (int)line.size()) {
            return;
        }

		out << termcolor::bold << "\rCoverage    " << termcolor::reset << totCov << "    ";
		if (A) {
			out << "  A:" << A;
		} else if (T) {
			out << "  T:" << T;
		} else if (C) {
			out << "  C:" << C;
		} else if (G) {
			out << "  G:" << G;
		} else {
			out << "     ";
		}
        term_space -= (int)line.size();
        line.clear();
        line = "  A:" + std::to_string(mA) + "  T:" + std::to_string(mT) + "  C:" + std::to_string(mC) + "  G:" + std::to_string(mT);
        if (term_space < (int)line.size()) {
            out << std::flush;
            return;
        }

		if (mA > 0 || mT > 0 || mC > 0 || mG > 0) {
			if (mA) {
				out << termcolor::green << "  A" << termcolor::reset << ":" << mA;
			}
			if (mT) {
				out << termcolor::red << "  T" << termcolor::reset << ":"<< mT;
			}
			if (mC) {
				out << termcolor::blue << "  C" << termcolor::reset << ":" << mC;
			}
			if (mG) {
				out << termcolor::yellow << "  G" << termcolor::reset << ":" << mG;
			}
		}
        line.clear();
        std::string s = intToStringCommas(pos);
        line = "    Pos  " + s;
        if (term_space < (int)line.size()) {
            out << std::flush;
            return;
        }
        out << termcolor::bold << "    Pos  " << termcolor::reset << s;
		out << std::flush;
	}

	void printTrack(float x, HGW::GwTrack &track, Utils::Region *rgn, bool mouseOver, int targetLevel, int trackIdx, std::string &target_name, int *target_pos, std::ostream& out) {
        if (rgn == nullptr) {
            return;
        }
		int target = (int)((float)(rgn->end - rgn->start) * x) + rgn->start;
        int jitter = (rgn->end - rgn->start) * 0.025;
		std::filesystem::path p = track.path;
        bool isGFF = (track.kind == HGW::FType::GFF3_NOI || track.kind == HGW::FType::GFF3_IDX || track.kind == HGW::FType::GTF_NOI || track.kind == HGW::FType::GTF_IDX );
        if (trackIdx >= (int)rgn->featuresInView.size()) {
            return;
        }
        bool same_pos, same_name = false;
        for (auto &b : rgn->featuresInView.at(trackIdx)) {
            if (b.start - jitter <= target && b.end + jitter >= target && b.level == targetLevel) {
                clearLine(out);
                if (target_name == b.name) {
                    same_name = true;
                } else {
                    target_name = b.start;
                }
                if (*target_pos == b.start) {
                    same_pos = true;
                } else {
                    *target_pos = b.start;
                }

                out << "\r" << termcolor::bold << p.filename().string() << termcolor::reset << "    " << \
                    termcolor::cyan << b.chrom << ":" << b.start << "-" << b.end << termcolor::reset;

                if (!b.parent.empty()) {
                    out << termcolor::bold << "    Parent  " << termcolor::reset << b.parent;
                } else if (!b.name.empty()) {
                    out << termcolor::bold << "    ID  " << termcolor::reset << b.name;
                    target_name = b.name;
                }
                if (!b.vartype.empty()) {
                    out << termcolor::bold << "    Type  " << termcolor::reset << b.vartype;
                }
                out << std::flush;

                if (same_name && same_pos && !mouseOver && !b.line.empty()) {
                    out << "\n" << b.line << std::endl;
                    return;
                }

                if (!mouseOver) {
                    out << std::endl;
                    std::vector<std::string> &parts = b.parts;
                    if (isGFF) {
                        int i = 0; int j = 0;
                        while (i < (int)parts.size()) {
                            for (j = i; j < (int)parts.size(); ++j ) {
                                if (parts[j] == "\n") {
                                    int start = std::stoi(parts[i + 3]);
                                    int end = std::stoi(parts[i + 4]);
                                    if (start <= target && end >= target) {
                                        bool first = true;
                                        assert (j >=9);
                                        for (int k=j-9; k <= j; ++k) {
                                            if (first) { out << parts[k]; first = false;}
                                            else { out << "\t" << parts[k]; }
                                        }
                                    }
                                    break;
                                }
                            }
                            i = j + 1;
                        }

                    } else {
                        bool first = true;
                        for (auto &p: parts) {
                            if (first) { out << p; first = false; }
                            else { out << "\t" << p; }
                        }
                    }
                    out << std::endl;
                } else {
                    break;
                }
            }
        }

        if (!mouseOver) {
            out << std::endl;
        }
	}

    void printVariantFileInfo(Utils::Label *label, int index, std::ostream& out) {
        if (label->pos < 0) {
            return;
        }
        Term::clearLine(out);
        std::string v = "\rPos     ";

        int term_space = Utils::get_terminal_width();
        if (term_space < (int)v.size()) {
            out << std::flush; return;
        }
        out << termcolor::bold << v << termcolor::reset;
        term_space -= (int)v.size();

        if (label->pos != -1) {
            v = label->chrom + ":" + std::to_string(label->pos);
            if (term_space < (int)v.size()) {
                out << std::flush; return;
            }
            out << v;
            term_space -= (int)v.size();
        }
        v = "    ID  ";
        if (term_space < (int)v.size()) {
            out << std::flush; return;
        }
        out << termcolor::bold << v << termcolor::reset;
        term_space -= (int)v.size();

        v = label->variantId;
        if (term_space < (int)v.size()) {
            out << std::flush; return;
        }
        out << v;
        term_space -= (int)v.size();

        v = "    Type  ";
        if (term_space < (int)v.size()) {
            out << std::flush; return;
        }
        out << termcolor::bold << v << termcolor::reset;
        term_space -= (int)v.size();

        if (!label->vartype.empty()) {
            v = label->vartype;
            if (term_space < (int)v.size()) {
                out << std::flush; return;
            }
            out << v;
            term_space -= (int)v.size();
        }

        v = "    Index  ";
        if (term_space < (int)v.size()) {
            out << std::flush; return;
        }
        out << termcolor::bold << v << termcolor::reset;
        term_space -= (int)v.size();

        v = std::to_string(index);
        if (term_space < (int)v.size()) {
            out << std::flush; return;
        }
        out << v;
        term_space -= (int)v.size();
        out << std::flush;

        if (label->comment.empty()) {
            return;
        }

        v = "    " + label->comment;
        if ((int)v.size() < term_space) {
            out << v << std::flush;
        } else if (term_space > 10) {
            v.erase(v.begin(), v.begin() + term_space);
            out << v << std::flush;
        }

    }

    int check_url(const char *url) {
# if !defined(__EMSCRIPTEN__)
        CURL *curl;
        CURLcode response;
        curl = curl_easy_init();
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
        response = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        return (response == CURLE_OK) ? 1 : 0;
#else
        return 1;
#endif
    }

    void replaceRegionInLink(std::regex &chrom_pattern, std::regex &start_pattern, std::regex &end_pattern, std::string &link, std::string &chrom, int start, int end) {
        link = std::regex_replace(link, chrom_pattern, chrom);
        link = std::regex_replace(link, start_pattern, std::to_string(start));
        link = std::regex_replace(link, end_pattern, std::to_string(end));
    }

    int checkAndPrintRegionLink(std::string &link, const char *info, std::ostream& out) {
        if (check_url(link.c_str())) {
            out << termcolor::green << "\n" << info << "\n" << termcolor::reset;
            out << link << std::endl;
            return 1;
        }
        return 0;
    }

    void printOnlineLinks(std::vector<HGW::GwTrack> &tracks, Utils::Region &rgn, std::string &genome_tag, std::ostream& out) {
        if (genome_tag.empty()) {
            out << termcolor::red << "Error:" << termcolor::reset
                      << " a 'genome_tag' must be provided for the current reference genome e.g. hg19 or hs1"
                      << std::endl;
            return;
        }
        out << "\nFetching online resources for genome: " << termcolor::bold << genome_tag << termcolor::reset
                  << std::endl;

        std::string chrom = rgn.chrom;
        std::string chrom_no_chr = chrom;
        if (Utils::startsWith(chrom_no_chr, "chr")) {
            chrom_no_chr.erase(0, 3);
        }
        std::string link;

        std::regex genome_pattern("GENOME");
        std::regex chrom_pattern("CHROM");
        std::regex start_pattern("START");
        std::regex end_pattern("END");

        bool is_hg19 = (genome_tag == "grch37" || Utils::startsWith(genome_tag, "GRCh37") || genome_tag == "hg19");
        bool is_hg38 = (genome_tag == "grch38" || Utils::startsWith(genome_tag, "GRCh38") || genome_tag == "hg38");
        bool is_t2t = (Utils::startsWith(genome_tag, "t2t") || Utils::startsWith(genome_tag, "T2T") ||
                       Utils::startsWith(genome_tag, "chm13v2") || genome_tag == "hs1");
        int p = 0;
        // Ucsc, Decipher, Gnomad, ensemble, ncbi
        if (is_hg19) {
            link = "https://genome.ucsc.edu/cgi-bin/hgTracks?db=hg19&lastVirtModeType=default&lastVirtModeExtraState=&virtModeType=default&virtMode=0&nonVirtPosition=&position=CHROM%3ASTART%2DEND&hgsid=1791027784_UAbj7DxAIuZZsoF0z5BQmYQgoS6j";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "UCSC Genome Browser GRCh37", out);

            link = "https://www.deciphergenomics.org/browser#q/grch37:CHROM:START-END/location/CHROM:START-END";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom_no_chr, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "DECIPHER Genome Browser GRCh37", out);

            link = "https://gnomad.broadinstitute.org/region/CHROM-START-END?dataset=gnomad_r2_1";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom_no_chr, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "Gnomad Genome Browser v2.1", out);

            link = "http://grch37.ensembl.org/Homo_sapiens/Location/View?r=CHROM:START-END";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom_no_chr, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "Ensemble Genome Browser GRCh37", out);

            link = "https://www.ncbi.nlm.nih.gov/genome/gdv/browser/genome/?chr=CHROM&from=START&to=END&id=GCF_000001405.40";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom_no_chr, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "NCBI Genome Browser GRCh37", out);

        } else if (is_hg38) {
            link  = "https://genome.ucsc.edu/cgi-bin/hgTracks?db=hg38&lastVirtModeType=default&lastVirtModeExtraState=&virtModeType=default&virtMode=0&nonVirtPosition=&position=CHROM%3ASTART%2DEND&hgsid=1791027784_UAbj7DxAIuZZsoF0z5BQmYQgoS6j";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "UCSC Genome Browser GRCh38", out);

            link = "https://www.deciphergenomics.org/browser#q/CHROM:START-END/location/CHROM:START-END";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom_no_chr, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "DECIPHER Genome Browser GRCh38", out);

            link = "https://gnomad.broadinstitute.org/region/CHROM-START-END?dataset=gnomad_sv_r4";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom_no_chr, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "Gnomad Genome Browser v4", out);

            link = "http://www.ensembl.org/Homo_sapiens/Location/View?r=CHROM%3ASTART-END";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom_no_chr, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "Ensemble Genome Browser GRCh38", out);

            link = "https://www.ncbi.nlm.nih.gov/genome/gdv/browser/genome/?chr=CHROM&from=START&to=END&id=GCF_000001405.40";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom_no_chr, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "NCBI Genome Browser GRCh38", out);

        } else if (is_t2t) {
            link  = "https://genome.ucsc.edu/cgi-bin/hgTracks?db=hub_3267197_GCA_009914755.4&lastVirtModeType=default&lastVirtModeExtraState=&virtModeType=default&virtMode=0&nonVirtPosition=&position=CHROM%3ASTART%2DEND&hgsid=1791153048_ztJI6SNAA8eGqv01Z11jfkDfa76v";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "UCSC Genome Browser T2T v2.0", out);

            link = "https://rapid.ensembl.org/Homo_sapiens_GCA_009914755.4/Location/View?r=CHROM%3ASTART-END";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom_no_chr, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "Ensemble Genome Browser T2T v2.0", out);

            link  = "https://www.ncbi.nlm.nih.gov/genome/gdv/browser/genome/?chr=CHROM&from=START&to=END&id=GCF_009914755.1";
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, "NCBI Genome Browser T2T v2.0", out);
        } else {
            link  = "https://genome-euro.ucsc.edu/cgi-bin/hgTracks?db=GENOME&lastVirtModeType=default&lastVirtModeExtraState=&virtModeType=default&virtMode=0&nonVirtPosition=&position=CHROM%3ASTART%2DEND&hgsid=308573407_B0yK8LZQyOQXcYLjPy7a3u2IW7an";
            link = std::regex_replace(link, genome_pattern, genome_tag);
            replaceRegionInLink(chrom_pattern, start_pattern, end_pattern, link, chrom, rgn.start, rgn.end);
            p += checkAndPrintRegionLink(link, genome_tag.c_str(), out);
        }
        if (!p) {
            out << "Could not find find any valid links, invalid genome_tag? Should be e.g. hg19 or mm39 etc\n";
        } else {
            out << std::endl;
        }
    }

    void updateRefGenomeSeq(Utils::Region *region, float xW, float xOffset, float xScaling, std::ostream& out) {
        if (region->refSeqLen == 0) {
            return;
        }

        if ((region->end - region->start) < region->refSeqLen) {
            if (region->end < region->chromLen) {
                return;  // refseq not fully loaded from region. user needs to zoom in to fetch refseq
            }
        }

        float min_x = xOffset;
        float max_x = xScaling * ((float)(region->end - region->start)) + min_x;
        if (xW > min_x && xW < max_x) {
            int pos = (int) (((xW - (float)xOffset) / xScaling) + (float)region->start);
            int chars =  Utils::get_terminal_width() - 11;
            if (chars <= 0) {
                return;
            }
            int i = chars / 2;
            int startIdx = pos - region->start - i;
            if (startIdx < 0) {
                i += startIdx;
                startIdx = 0;
            }
            if (startIdx > region->end - region->start) {
                return;  // something went wrong
            }
            if (region->refSeq == nullptr || startIdx >= (int)strlen(region->refSeq)) {
                return;
            }
            const char * s = &region->refSeq[startIdx];
            Term::clearLine(out);
            out << termcolor::bold << "\rRef       " << termcolor::reset ;
            int l = 0;
            while (*s && l < chars) {
                switch ((unsigned int)*s) { // this is a bit of mess, but gets the job done
                    case 65: ((l == i) ? (out << termcolor::underline << termcolor::green << "a" << termcolor::reset) : (out << termcolor::green << "a") ); break;
                    case 67: ((l == i) ? (out << termcolor::underline << termcolor::blue << "c" << termcolor::reset) : (out << termcolor::blue << "c") ); break;
                    case 71: ((l == i) ? (out << termcolor::underline << termcolor::yellow << "g" << termcolor::reset) : (out << termcolor::yellow << "g") ); break;
                    case 78: ((l == i) ? (out << termcolor::underline << termcolor::bright_grey << "n" << termcolor::reset) : (out << termcolor::bright_grey << "n") ); break;
                    case 84: ((l == i) ? (out << termcolor::underline << termcolor::red << "t" << termcolor::reset) : (out << termcolor::red << "t") ); break;
                    case 97: ((l == i) ? (out << termcolor::underline << termcolor::green << "A" << termcolor::reset) : (out << termcolor::green << "A") ); break;
                    case 99: ((l == i) ? (out << termcolor::underline << termcolor::blue << "C" << termcolor::reset) : (out << termcolor::blue << "C") ); break;
                    case 103: ((l == i) ? (out << termcolor::underline << termcolor::yellow << "G" << termcolor::reset) : (out << termcolor::yellow << "G") ); break;
                    case 110: ((l == i) ? (out << termcolor::underline << termcolor::bright_grey << "N" << termcolor::reset) : (out << termcolor::bright_grey << "N") ); break;
                    case 116: ((l == i) ? (out << termcolor::underline << termcolor::red << "T" << termcolor::reset) : (out << termcolor::red << "T") ); break;
                    default: out << "?"; break;
                }
                ++s;
                l += 1;
            }
            out << termcolor::reset << std::flush;
            return;
        }
    }

#if !defined(__EMSCRIPTEN__)

    size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    std::string getLatestVersion() {
        CURL* curl;
        CURLcode res;
        std::string readBuffer;

        curl = curl_easy_init();
        if (curl) {
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "User-Agent: gw-app");

            curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/kcleal/gw/tags");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);

            if (res == CURLE_OK) {
                // Simple JSON parsing to find the latest version
                size_t pos = readBuffer.find("\"name\"");
                if (pos != std::string::npos) {
                    size_t start = readBuffer.find("\"", pos + 7) + 1;
                    size_t end = readBuffer.find("\"", start);
                    return readBuffer.substr(start, end - start);
                }
            }
        }
        return "";
    }

    void checkVersion() {
        std::string latestVersion = getLatestVersion();
        if (!latestVersion.empty()) {
            std::vector<std::string> partsLatest = Utils::split(latestVersion, '.');
            std::vector<std::string> partsCurrent = Utils::split(GW_VERSION, '.');
            if ( std::stoi(partsLatest[1]) > std::stoi(partsCurrent[1]) ||
                 std::stoi(partsLatest[2]) > std::stoi(partsCurrent[2]) ) {
                std::cout << "\nA new update is available: https://github.com/kcleal/gw/releases/tag/" << latestVersion << std::endl;
            }
        }
    }

    void startVersionCheck() {
        std::thread versionCheckThread(checkVersion);
        versionCheckThread.detach(); // Detach the thread to run independently
    }
#endif

}
