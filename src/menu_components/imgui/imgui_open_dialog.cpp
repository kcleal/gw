// imgui_open_dialog.cpp
// ImGui open-file dialog and variant load chooser
// Extracted from menu.cpp with no functional changes.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "menu.h"
#include "plot_manager.h"
#include "plot_commands.h"
#include "utils.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"

namespace Menu {

// -----------------------------------------------------------------------------
// File browser singleton
// -----------------------------------------------------------------------------

// The file types GW recognises by name
static const std::vector<std::string>& knownFileTypes()
{
    static const std::vector<std::string> types = {
        ".bam", ".cram", ".sam",
        ".bed", ".bed.gz", ".bed.bgz",
        ".vcf", ".vcf.gz", ".bcf",
        ".gff3", ".gff3.gz", ".gff", ".gff.gz",
        ".gtf", ".gtf.gz",
        ".tsv", ".tsv.gz", ".txt", ".txt.gz",
        ".bigwig", ".bw", ".bigbed", ".bb",
        ".paf",
        ".fa", ".fasta", ".fa.gz", ".fasta.gz", ".fa.bgz", ".fasta.bgz",
        ".png",
    };
    return types;
}

static ImGui::FileBrowser& getFileBrowser()
{
    static ImGui::FileBrowser browser(
        ImGuiFileBrowserFlags_MultipleSelection |
        ImGuiFileBrowserFlags_EnterNewFilename |
        ImGuiFileBrowserFlags_CloseOnEsc |
        ImGuiFileBrowserFlags_ConfirmOnEnter |
        ImGuiFileBrowserFlags_EditPathString |
        ImGuiFileBrowserFlags_SkipItemsCausingError);

    static bool initialised = false;

    if (!initialised) {
        browser.SetTitle("Open File");
        browser.SetTypeFilters(knownFileTypes());
        browser.AddStandardShortcuts();
        initialised = true;
    }

    return browser;
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// File types are tested on the typed name: the file name, plus the extension the user gave
// it in the "unknown file type" prompt, if any
static std::string typedFileName(const std::filesystem::path& p)
{
    return Online::typedName(p.string());
}

static bool isFastaPath(const std::filesystem::path& p)
{
    std::string s = typedFileName(p);
    return Utils::endsWith(s, ".fa") ||
           Utils::endsWith(s, ".fasta") ||
           Utils::endsWith(s, ".fa.gz") ||
           Utils::endsWith(s, ".fasta.gz") ||
           Utils::endsWith(s, ".fa.bgz") ||
           Utils::endsWith(s, ".fasta.bgz");
}

static bool isVariantPath(const std::filesystem::path& p)
{
    std::string s = typedFileName(p);
    return Utils::endsWith(s, ".vcf") ||
           Utils::endsWith(s, ".vcf.gz") ||
           Utils::endsWith(s, ".bcf") ||
           Utils::endsWith(s, ".bed") ||
           Utils::endsWith(s, ".bed.gz");
}

static bool isKnownPath(const std::filesystem::path& p)
{
    std::string s = typedFileName(p);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (Utils::endsWith(s, ".ini")) return true;  // session file
    for (const auto& ext : knownFileTypes()) {
        if (Utils::endsWith(s, ext)) return true;
    }
    return false;
}

// What a file with an unrecognised name can be opened as
struct FileKind { const char* label; const char* extension; const char* compressedExtension; };
static const FileKind fileKinds[] = {
    {"Alignments (BAM / CRAM / SAM)", ".bam", ".bam"},
    {"Variants (VCF)", ".vcf", ".vcf.gz"},
    {"Variants (BCF)", ".bcf", ".bcf"},
    {"BED", ".bed", ".bed.gz"},
    {"GFF3", ".gff3", ".gff3.gz"},
    {"GTF", ".gtf", ".gtf.gz"},
    {"bigWig", ".bw", ".bw"},
    {"bigBed", ".bb", ".bb"},
    {"PAF", ".paf", ".paf"},
    {"Reference genome (FASTA)", ".fa", ".fa.gz"},
};

// gzip and bgzip files start with the bytes 1f 8b
static bool isGzipFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    unsigned char magic[2] = {0, 0};
    in.read(reinterpret_cast<char*>(magic), 2);
    return in.gcount() == 2 && magic[0] == 0x1f && magic[1] == 0x8b;
}

// -----------------------------------------------------------------------------
// Open dialog
// -----------------------------------------------------------------------------

void drawImGuiOpenDialog(Manager::GwPlot* plot,
                         bool* p_open,
                         bool& redraw)
{
    drawImGuiOnlineGenomesButton(plot);
    drawImGuiOnlineFileDialog(plot, redraw);
    ImGui::FileBrowser& browser = getFileBrowser();

    static std::vector<std::string> pendingVariantFiles;
    static bool showVariantDialog = false;

    // Files whose type GW could not tell from the name; the user is asked about each in turn
    static std::vector<std::string> pendingUnknownFiles;
    static int unknownKind = 0;

    static std::string openFileError;
    static float openFileErrorTimer = 0.f;

    // Genome-tag input shown after a FASTA is selected in the file browser.
    static bool showGenomeTagDialog = false;
    static std::string pendingGenomePath;
    static char genomeTagBuf[128] = {};

#ifdef __EMSCRIPTEN__
    static bool localReference=false, localTrack=true, localTiles=false;
    if (p_open && *p_open) { ImGui::OpenPopup("Open local files##gw_local_kind"); *p_open=false; }
    if (ImGui::BeginPopupModal("Open local files##gw_local_kind", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if(ImGui::Checkbox("Reference genome", &localReference) && localReference){localTrack=false;localTiles=false;}
        if(ImGui::Checkbox("Track / alignment", &localTrack) && localTrack)localReference=false;
        if(ImGui::Checkbox("Variant file as tiled images", &localTiles) && localTiles)localReference=false;
        ImGui::TextDisabled("Select the file and its index together, or add the index later.");
        if(ImGui::Button("GW downloaded files")) { browser.SetDirectory(Online::downloadDirectory(plot->opts.ini_path));browser.Open();ImGui::CloseCurrentPopup(); }
        ImGui::BeginDisabled(!localReference && !localTrack && !localTiles);
        bool chooseFiles = ImGui::Button("Choose files…");
        // Safari opens a file picker only inside the click itself, which ends before this
        // frame runs, so the page opens it on release while the pointer is over the button.
        if(ImGui::IsItemHovered())
            EM_ASM({ if(window.gwArmFilePicker)window.gwArmFilePicker(!!$0,!!$1,!!$2); },localReference,localTrack,localTiles);
        if(chooseFiles) {
            EM_ASM({ if(window.gwOpenFiles)window.gwOpenFiles(!!$0,!!$1,!!$2); },localReference,localTrack,localTiles);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button("Cancel") || closeOnEscape())ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
#else
    if (p_open && *p_open && !browser.IsOpened()) browser.Open();
#endif

    // Set contextual OK label
    {
        auto peek = browser.PeekSelected();
        bool hasAlignment = false;
        bool hasFasta = false;
        bool hasVariant = false;
        bool hasOther = false;

        for (const auto& sp : peek) {
            if (Utils::endsWith(sp.filename().string(), ".bam") ||
                Utils::endsWith(sp.filename().string(), ".cram"))
                hasAlignment = true;
            else if (isFastaPath(sp))
                hasFasta = true;
            else if (isVariantPath(sp))
                hasVariant = true;
            else
                hasOther = true;
        }

        int kinds = (int)hasAlignment +
                    (int)hasFasta +
                    (int)hasVariant +
                    (int)hasOther;

        if (kinds == 1) {
            if (hasAlignment)
                browser.SetOkButtonLabel("Load alignments");
            else if (hasFasta)
                browser.SetOkButtonLabel("Load reference");
            else
                browser.SetOkButtonLabel("Load");
        }
        else if (!peek.empty()) {
            browser.SetOkButtonLabel("Load");
        } else {
            browser.SetOkButtonLabel("ok");
        }
    }

    const auto downloaded=Online::downloadDirectory(plot->opts.ini_path);
    static std::string downloadFolder;
    if(downloadFolder!=downloaded){std::error_code error;std::filesystem::create_directories(downloaded,error);if(!error)downloadFolder=downloaded;}
    browser.SetShortcut("GW downloaded files",downloaded);
    browser.Display();

    // Send one chosen file to the right loader. FASTA and variant files are held for their
    // own follow-up dialogs, which finishRouting() opens
    static std::vector<std::string> failedFiles;
    auto routeFile = [&](const std::string& path) {
        const std::filesystem::path sel(path);
        if (isFastaPath(sel)) {
            pendingGenomePath = path;  // loaded once the user has had a chance to enter a tag
        } else if (isVariantPath(sel)) {
            pendingVariantFiles.push_back(path);
        } else {
            std::string trackPath = path;  // addTrack takes a non-const reference
            if (!plot->addTrack(trackPath, true, plot->opts.vcf_as_tracks, plot->opts.bed_as_tracks))
                failedFiles.push_back(sel.filename().string());
        }
    };
    auto finishRouting = [&]() {
        if (!pendingGenomePath.empty())
            showGenomeTagDialog = true;
        else if (!pendingVariantFiles.empty())
            showVariantDialog = true;
        if (!failedFiles.empty()) {
            openFileError = "Could not open: ";
            for (size_t i = 0; i < failedFiles.size(); ++i) {
                if (i > 0) openFileError += ", ";
                openFileError += failedFiles[i];
            }
            openFileErrorTimer = 4.0f;
            failedFiles.clear();
        }
    };

    // Handle selection
    if (browser.HasSelected()) {

        auto selections = browser.GetMultiSelected();

        for (auto& sel : selections) {
            if (isKnownPath(sel)) routeFile(sel.string());
            else pendingUnknownFiles.push_back(sel.string());
        }

        browser.ClearSelected();
        if (pendingUnknownFiles.empty()) finishRouting();

        redraw = true;
        plot->processed = false;

        if (p_open)
            *p_open = false;
    }

    // -------------------------------------------------------------------------
    // Unknown file type: ask what the file is
    // -------------------------------------------------------------------------

    if (!pendingUnknownFiles.empty())
        ImGui::OpenPopup("Unknown file type##gw_filekind");

    if (ImGui::BeginPopupModal("Unknown file type##gw_filekind", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto next = [&]() {
            pendingUnknownFiles.erase(pendingUnknownFiles.begin());
            if (pendingUnknownFiles.empty()) {
                finishRouting();
                ImGui::CloseCurrentPopup();
            }
        };
        if (pendingUnknownFiles.empty()) {
            ImGui::CloseCurrentPopup();
        } else {
            const std::string path = pendingUnknownFiles.front();
            ImGui::TextUnformatted("GW cannot tell what kind of file this is from its name:");
            ImGui::Text("  %s", std::filesystem::path(path).filename().string().c_str());
            if (pendingUnknownFiles.size() > 1)
                ImGui::TextDisabled("  and %d more to follow", (int)pendingUnknownFiles.size() - 1);
            ImGui::Spacing();
            ImGui::TextUnformatted("Open it as:");
            for (int i = 0; i < (int)IM_ARRAYSIZE(fileKinds); ++i)
                ImGui::RadioButton(fileKinds[i].label, &unknownKind, i);
            ImGui::Spacing();
            if (ImGui::Button("Open", ImVec2(120, 0))) {
                const FileKind& kind = fileKinds[unknownKind];
                Online::setTypeAlias(path, isGzipFile(path) ? kind.compressedExtension : kind.extension);
                routeFile(path);
                redraw = true;
                plot->processed = false;
                next();
            }
            ImGui::SameLine();
            if (ImGui::Button("Skip", ImVec2(120, 0)) || closeOnEscape()) next();
        }
        ImGui::EndPopup();
    }

    // Sync external open flag
    if (p_open && *p_open && !browser.IsOpened()) {
        *p_open = false;
    }

    // -------------------------------------------------------------------------
    // Genome tag dialog (shown after selecting a FASTA)
    // -------------------------------------------------------------------------

    if (showGenomeTagDialog)
        ImGui::OpenPopup("Load reference genome##gw_genometag");

    bool tagPopupOpen = true;
    if (ImGui::BeginPopupModal(
            "Load reference genome##gw_genometag",
            &tagPopupOpen,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Genome: %s",
            std::filesystem::path(pendingGenomePath).filename().string().c_str());

        ImGui::Spacing();
        ImGui::TextUnformatted("Optional tag name for .gw.ini:");
        ImGui::InputText("##genometag", genomeTagBuf, sizeof(genomeTagBuf));

        ImGui::Spacing();

        if (ImGui::Button("Load genome", ImVec2(120, 0))) {
            std::ostream& out =
                plot->terminalOutput ? std::cout : plot->outStr;
            plot->loadGenome(pendingGenomePath, out);

            // If the user supplied a tag, store it in the ini file.
            if (genomeTagBuf[0] != '\0') {
                plot->opts.myIni["genomes"][genomeTagBuf] = pendingGenomePath;
                plot->opts.genome_tag = genomeTagBuf;
            }

            pendingGenomePath.clear();
            genomeTagBuf[0] = '\0';
            showGenomeTagDialog = false;
            redraw = true;
            plot->processed = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            pendingGenomePath.clear();
            genomeTagBuf[0] = '\0';
            showGenomeTagDialog = false;
            ImGui::CloseCurrentPopup();
        }

        if (!tagPopupOpen || closeOnEscape()) {
            pendingGenomePath.clear();
            genomeTagBuf[0] = '\0';
            showGenomeTagDialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // -------------------------------------------------------------------------
    // Variant load chooser dialog
    // -------------------------------------------------------------------------

    if (showVariantDialog)
        ImGui::OpenPopup("Load variant/annotation files##gw_varload");

    bool varPopupOpen = true;
    if (ImGui::BeginPopupModal(
            "Load variant/annotation files##gw_varload",
            &varPopupOpen,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%d file(s) selected:",
                    (int)pendingVariantFiles.size());

        for (const auto& f : pendingVariantFiles) {
            ImGui::TextDisabled(" %s",
                std::filesystem::path(f).filename().string().c_str());
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("How should they be loaded?");
        ImGui::Spacing();

        static bool asTiles=false, asTrack=true;
        ImGui::Checkbox("Variant file as tiled images", &asTiles);
        ImGui::Checkbox("Annotation track", &asTrack);
        ImGui::BeginDisabled(!asTiles && !asTrack);
        if (ImGui::Button("Open")) {
            for (auto& f : pendingVariantFiles) {
                if(asTiles) plot->addTrack(f, true, false, false);
                if(asTrack) plot->addTrack(f, true, true, true);
            }
            pendingVariantFiles.clear(); showVariantDialog=false;
            redraw=true;plot->processed=false;ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button("Cancel")){pendingVariantFiles.clear();showVariantDialog=false;ImGui::CloseCurrentPopup();}

        if (!varPopupOpen || closeOnEscape()) {
            pendingVariantFiles.clear();
            showVariantDialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // -------------------------------------------------------------------------
    // Error toast
    // -------------------------------------------------------------------------

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) openFileErrorTimer = 0.f;
    if (openFileErrorTimer > 0.f) {

        openFileErrorTimer -= ImGui::GetIO().DeltaTime;

        ImGuiIO& io = ImGui::GetIO();

        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x * 0.5f,
                   io.DisplaySize.y - 40.f),
            ImGuiCond_Always,
            ImVec2(0.5f, 0.5f));

        ImGui::PushStyleColor(
            ImGuiCol_WindowBg,
            ImVec4(0.6f, 0.1f, 0.1f, 0.9f));

        ImGui::Begin(
            "##OpenFileError",
            nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav);

        ImGui::TextUnformatted(openFileError.c_str());
        ImGui::End();
        ImGui::PopStyleColor();

        if (openFileErrorTimer <= 0.f) {
            openFileError.clear();
        }
    }
}

} // namespace Menu