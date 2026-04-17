// imgui_open_dialog.cpp
// ImGui open-file dialog and variant load chooser
// Extracted from menu.cpp with no functional changes.

#include <algorithm>
#include <filesystem>
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

static ImGui::FileBrowser& getFileBrowser()
{
    static ImGui::FileBrowser browser(
        ImGuiFileBrowserFlags_MultipleSelection |
        ImGuiFileBrowserFlags_CloseOnEsc |
        ImGuiFileBrowserFlags_ConfirmOnEnter |
        ImGuiFileBrowserFlags_EditPathString |
        ImGuiFileBrowserFlags_SkipItemsCausingError);

    static bool initialised = false;

    if (!initialised) {
        browser.SetTitle("Open File");
        browser.SetTypeFilters({
            ".*",
            ".bam", ".cram",
            ".bed", ".bed.gz",
            ".vcf", ".vcf.gz", ".bcf",
            ".gff3", ".gff3.gz",
            ".gtf", ".gtf.gz",
            ".png",
            ".tsv", ".tsv.gz",
            ".txt", ".txt.gz",
            ".fa", ".fasta",
            ".fa.gz", ".fasta.gz",
        });
        initialised = true;
    }

    return browser;
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static bool isFastaPath(const std::filesystem::path& p)
{
    std::string s = p.filename().string();
    return Utils::endsWith(s, ".fa") ||
           Utils::endsWith(s, ".fasta") ||
           Utils::endsWith(s, ".fa.gz") ||
           Utils::endsWith(s, ".fasta.gz") ||
           Utils::endsWith(s, ".fa.bgz") ||
           Utils::endsWith(s, ".fasta.bgz");
}

static bool isVariantPath(const std::filesystem::path& p)
{
    std::string s = p.filename().string();
    return Utils::endsWith(s, ".vcf") ||
           Utils::endsWith(s, ".vcf.gz") ||
           Utils::endsWith(s, ".bcf") ||
           Utils::endsWith(s, ".bed") ||
           Utils::endsWith(s, ".bed.gz");
}

// -----------------------------------------------------------------------------
// Open dialog
// -----------------------------------------------------------------------------

void drawImGuiOpenDialog(Manager::GwPlot* plot,
                         bool* p_open,
                         bool& redraw)
{
    ImGui::FileBrowser& browser = getFileBrowser();

    static std::vector<std::string> pendingVariantFiles;
    static bool showVariantDialog = false;

    static std::string openFileError;
    static float openFileErrorTimer = 0.f;

    // Open browser when requested
    if (p_open && *p_open && !browser.IsOpened()) {
        browser.Open();
    }

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

    browser.Display();

    // Handle selection
    if (browser.HasSelected()) {

        auto selections = browser.GetMultiSelected();
        std::vector<std::string> failed;

        std::ostream& out =
            plot->terminalOutput ? std::cout : plot->outStr;

        for (auto& sel : selections) {
            std::string path = sel.string();

            if (isFastaPath(sel)) {
                plot->loadGenome(path, out);
            }
            else if (isVariantPath(sel)) {
                pendingVariantFiles.push_back(path);
            }
            else {
                bool ok = plot->addTrack(
                    path,
                    true,
                    plot->opts.vcf_as_tracks,
                    plot->opts.bed_as_tracks);

                if (!ok)
                    failed.push_back(sel.filename().string());
            }
        }

        browser.ClearSelected();

        if (!pendingVariantFiles.empty())
            showVariantDialog = true;

        if (!failed.empty()) {
            openFileError = "Could not open: ";
            for (size_t i = 0; i < failed.size(); ++i) {
                if (i > 0) openFileError += ", ";
                openFileError += failed[i];
            }
            openFileErrorTimer = 4.0f;
        }

        redraw = true;
        plot->processed = false;

        if (p_open)
            *p_open = false;
    }

    // Sync external open flag
    if (p_open && *p_open && !browser.IsOpened()) {
        *p_open = false;
    }

    // -------------------------------------------------------------------------
    // Variant load chooser dialog
    // -------------------------------------------------------------------------

    if (showVariantDialog)
        ImGui::OpenPopup("Load variant/annotation files##gw_varload");

    if (ImGui::BeginPopupModal(
            "Load variant/annotation files##gw_varload",
            nullptr,
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

        if (ImGui::Button("Load as tiles")) {
            for (auto& f : pendingVariantFiles)
                plot->addTrack(f, true, false, false);

            pendingVariantFiles.clear();
            showVariantDialog = false;
            redraw = true;
            plot->processed = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Load as track")) {
            for (auto& f : pendingVariantFiles)
                plot->addTrack(f, true, true, true);

            pendingVariantFiles.clear();
            showVariantDialog = false;
            redraw = true;
            plot->processed = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel")) {
            pendingVariantFiles.clear();
            showVariantDialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // -------------------------------------------------------------------------
    // Error toast
    // -------------------------------------------------------------------------

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