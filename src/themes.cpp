//
// Created by kez on 01/08/22.
//
#include <filesystem>
#include "menu.h"
#include "themes.h"
#include "glfw_keys.h"
#include "defaultIni.hpp"
#include "ankerl_unordered_dense.h"
#include "gw_fonts.h"

#include "include/core/SkFontMgr.h"
#include "include/core/SkPathEffect.h"
#include "include/effects/SkDashPathEffect.h"
#if !defined(OLD_SKIA) || OLD_SKIA == 0
    #if defined(__APPLE__)
        #include <CoreText/CoreText.h>
        #include "include/ports/SkFontMgr_mac_ct.h"
    #elif defined(_WIN32) || defined(_WIN64)
        #include "include/ports/SkTypeface_win.h"
    #else
        #include <fontconfig/fontconfig.h>
        #include "include/ports/SkFontMgr_fontconfig.h"
        #include "include/ports/SkFontConfigInterface.h"
        #include "include/ports/SkFontMgr_FontConfigInterface.h"
    #endif
    #include "include/core/SkFontMetrics.h"
#endif

#if !defined(__EMSCRIPTEN__)
    #include <curl/curl.h>
    #include <curl/easy.h>
#endif


namespace Themes {

    SkScalar intervals[] = {10.0f, 5.0f}; // Length of dash, length of gap

    EXPORT BaseTheme::BaseTheme() {

//        fcCoverage.setStyle(SkPaint::kStrokeAndFill_Style);
        fcCoverage.setStyle(SkPaint::kFill_Style);
        fcCoverage.setStrokeWidth(0);

        // Set ARGB values directly for each SkPaint object in the mate_fc vector
        mate_fc[0].setARGB(255, 158, 1, 66);
        mate_fc[1].setARGB(255, 179, 24, 71);
        mate_fc[2].setARGB(255, 203, 51, 76);
        mate_fc[3].setARGB(255, 220, 73, 75);
        mate_fc[4].setARGB(255, 233, 92, 71);
        mate_fc[5].setARGB(255, 244, 114, 69);
        mate_fc[6].setARGB(255, 248, 142, 82);
        mate_fc[7].setARGB(255, 252, 167, 94);
        mate_fc[8].setARGB(255, 253, 190, 110);
        mate_fc[9].setARGB(255, 253, 212, 129);
        mate_fc[10].setARGB(255, 254, 228, 147);
        mate_fc[11].setARGB(255, 254, 242, 169);
        mate_fc[12].setARGB(255, 254, 254, 190);
        mate_fc[13].setARGB(255, 244, 250, 174);
        mate_fc[14].setARGB(255, 233, 246, 158);
        mate_fc[15].setARGB(255, 213, 238, 155);
        mate_fc[16].setARGB(255, 190, 229, 160);
        mate_fc[17].setARGB(255, 164, 218, 164);
        mate_fc[18].setARGB(255, 134, 206, 164);
        mate_fc[19].setARGB(255, 107, 196, 164);
        mate_fc[20].setARGB(255, 83, 173, 173);
        mate_fc[21].setARGB(255, 61, 148, 183);
        mate_fc[22].setARGB(255, 57, 125, 184);
        mate_fc[23].setARGB(255, 76, 101, 172);
        mate_fc[24].setARGB(255, 94, 79, 162);
        mate_fc[25].setARGB(255, 255, 255, 229);
        mate_fc[26].setARGB(255, 252, 254, 215);
        mate_fc[27].setARGB(255, 249, 253, 200);
        mate_fc[28].setARGB(255, 246, 251, 184);
        mate_fc[29].setARGB(255, 237, 248, 178);
        mate_fc[30].setARGB(255, 227, 244, 170);
        mate_fc[31].setARGB(255, 216, 239, 162);
        mate_fc[32].setARGB(255, 202, 233, 156);
        mate_fc[33].setARGB(255, 187, 227, 149);
        mate_fc[34].setARGB(255, 172, 220, 141);
        mate_fc[35].setARGB(255, 155, 213, 135);
        mate_fc[36].setARGB(255, 137, 205, 127);
        mate_fc[37].setARGB(255, 119, 197, 120);
        mate_fc[38].setARGB(255, 101, 189, 111);
        mate_fc[39].setARGB(255, 82, 179, 102);
        mate_fc[40].setARGB(255, 64, 170, 92);
        mate_fc[41].setARGB(255, 55, 158, 84);
        mate_fc[42].setARGB(255, 44, 144, 75);
        mate_fc[43].setARGB(255, 34, 131, 66);
        mate_fc[44].setARGB(255, 23, 122, 62);
        mate_fc[45].setARGB(255, 11, 112, 58);
        mate_fc[46].setARGB(255, 0, 103, 54);
        mate_fc[47].setARGB(255, 0, 92, 50);
        mate_fc[48].setARGB(255, 0, 79, 45);
        mate_fc[49].setARGB(255, 0, 69, 41);

        ecMateUnmapped.setARGB(255, 255, 0, 0);
        ecMateUnmapped.setStyle(SkPaint::kStroke_Style);
        ecMateUnmapped.setStrokeWidth(1);

        fcIns.setStyle(SkPaint::kFill_Style);

        lwMateUnmapped = 0.5;
        lwSplit = 0.5;
        lwCoverage = 1;

        lcCoverage.setARGB(255, 162, 192, 192);

        alpha = 204;
        mapq0_alpha = 102;

        fcMarkers.setStyle(SkPaint::kStrokeAndFill_Style);
        fcMarkers.setStrokeWidth(3);

        fcRoi.setARGB(180, 230, 10, 45);
        fcRoi.setStyle(SkPaint::kStrokeAndFill_Style);

        fcBigWig.setARGB(255, 20, 90, 190);
        fcBigWig.setStyle(SkPaint::kStrokeAndFill_Style);
        fcBigWig.setStrokeWidth(1);
    }

    void BaseTheme::setAlphas() {
        fcNormal0 = fcNormal;
        fcDel0 = fcDel;
        fcDup0 = fcDup;
        fcInvF0 = fcInvF;
        fcInvR0 = fcInvR;
        fcTra0 = fcTra;
        fcSoftClip0 = fcSoftClip;

        bgPaint.setStyle(SkPaint::kStrokeAndFill_Style);
        bgPaint.setStrokeWidth(0);

        fcNormal.setAlpha(alpha);

        fcNormal0.setAlpha(mapq0_alpha);
        fcDel0.setAlpha(mapq0_alpha);
        fcDup0.setAlpha(mapq0_alpha);
        fcInvF0.setAlpha(mapq0_alpha);
        fcInvR0.setAlpha(mapq0_alpha);
        fcTra0.setAlpha(mapq0_alpha);
        fcSoftClip0.setAlpha(mapq0_alpha);

        fcTrack.setStyle(SkPaint::kStrokeAndFill_Style);
        fcTrack.setStrokeWidth(0);
        fcTrack.setAntiAlias(true);

        lcJoins.setStyle(SkPaint::kStroke_Style);
        lcJoins.setStrokeWidth(2);
        lcJoins.setAntiAlias(true);

        lcLightJoins.setStyle(SkPaint::kStroke_Style);
        lcLightJoins.setStrokeWidth(1);

        lcGTFJoins.setStyle(SkPaint::kStroke_Style);
        lcGTFJoins.setStrokeWidth(2);
        lcGTFJoins.setPathEffect(SkDashPathEffect::Make(intervals, 2, 0.0f));

        lcLabel.setStyle(SkPaint::kStroke_Style);
        lcLabel.setStrokeWidth(1);
        lcLabel.setAntiAlias(true);

        lcBright.setStyle(SkPaint::kStroke_Style);
        lcBright.setStrokeWidth(2);
        lcBright.setAntiAlias(true);

        ecSelected.setAntiAlias(true);
        ecSplit.setAntiAlias(true);

        fcMarkers.setStyle(SkPaint::kStrokeAndFill_Style);
        fcMarkers.setAntiAlias(true);
        fcMarkers.setStrokeMiter(0.1);
        fcMarkers.setStrokeWidth(0.5);

        fc5mc.setAntiAlias(true);
        fc5mc.setStrokeCap(SkPaint::kRound_Cap);
        fc5hmc.setAntiAlias(true);
        fc5hmc.setStrokeCap(SkPaint::kRound_Cap);
        fcOther.setAntiAlias(true);
        fcOther.setStrokeCap(SkPaint::kRound_Cap);
        fcOther.setARGB(127, 255, 0, 0);

        mate_fc0 = mate_fc;
        for (size_t i=0; i < mate_fc.size(); ++i) {
//            SkPaint p = mate_fc[i];
            mate_fc[i].setAlpha(alpha);
            mate_fc0[i].setAlpha(mapq0_alpha);
//            p.setAlpha(mapq0_alpha);
//            mate_fc0.emplace_back(p);
        }
        SkPaint p;
        // A==1, C==2, G==4, T==8, N==>8
        for (size_t i=0; i<11; ++i) {
            p = fcA;
            p.setAlpha(base_qual_alpha[i]);
            BasePaints[1][i] = p;

            p = fcT;
            p.setAlpha(base_qual_alpha[i]);
            BasePaints[8][i] = p;

            p = fcC;
            p.setAlpha(base_qual_alpha[i]);
            BasePaints[2][i] = p;

            p = fcG;
            p.setAlpha(base_qual_alpha[i]);
            BasePaints[4][i] = p;

            p = fcN;
            p.setAlpha(base_qual_alpha[i]);
            BasePaints[9][i] = p;
            BasePaints[10][i] = p;
            BasePaints[11][i] = p;
            BasePaints[12][i] = p;
            BasePaints[13][i] = p;
            BasePaints[14][i] = p;
            BasePaints[15][i] = p;
        }
        int alph = 63;
        for (size_t i=0; i < 4; ++i) {
            ModPaints[0][i] = fc5mc;
            ModPaints[0][i].setAlpha(alph);

            ModPaints[1][i] = fc5hmc;
            ModPaints[1][i].setAlpha(alph);

            ModPaints[2][i] = fcOther;
            ModPaints[2][i].setAlpha(alph);

            alph += 64;
        }
    }

    void BaseTheme::setPaintARGB(int paint_enum, int a, int r, int g, int b) {
        switch (paint_enum) {
            case GwPaint::bgPaint: this->bgPaint.setARGB(a, r, g, b); break;
            case GwPaint::bgPaintTiled: this->bgPaintTiled.setARGB(a, r, g, b); break;
            case GwPaint::fcNormal: this->fcNormal.setARGB(a, r, g, b); break;
            case GwPaint::fcDel: this->fcDel.setARGB(a, r, g, b); break;
            case GwPaint::fcDup: this->fcDup.setARGB(a, r, g, b); break;
            case GwPaint::fcInvF: this->fcInvF.setARGB(a, r, g, b); break;
            case GwPaint::fcInvR: this->fcInvR.setARGB(a, r, g, b); break;
            case GwPaint::fcTra: this->fcTra.setARGB(a, r, g, b); break;
            case GwPaint::fcIns: this->fcIns.setARGB(a, r, g, b); break;
            case GwPaint::fcSoftClip: this->fcSoftClip.setARGB(a, r, g, b); break;
            case GwPaint::fcA: this->fcA.setARGB(a, r, g, b); break;
            case GwPaint::fcT: this->fcT.setARGB(a, r, g, b); break;
            case GwPaint::fcC: this->fcC.setARGB(a, r, g, b); break;
            case GwPaint::fcG: this->fcG.setARGB(a, r, g, b); break;
            case GwPaint::fcN: this->fcN.setARGB(a, r, g, b); break;
            case GwPaint::fc5mc: this->fc5mc.setARGB(a, r, g, b); break;
            case GwPaint::fc5hmc: this->fc5hmc.setARGB(a, r, g, b); break;
            case GwPaint::fcOther: this->fcOther.setARGB(a, r, g, b); break;
            case GwPaint::fcCoverage: this->fcCoverage.setARGB(a, r, g, b); break;
            case GwPaint::fcTrack: this->fcTrack.setARGB(a, r, g, b); break;
            case GwPaint::fcNormal0: this->fcNormal0.setARGB(a, r, g, b); break;
            case GwPaint::fcDel0: this->fcDel0.setARGB(a, r, g, b); break;
            case GwPaint::fcDup0: this->fcDup0.setARGB(a, r, g, b); break;
            case GwPaint::fcInvF0: this->fcInvF0.setARGB(a, r, g, b); break;
            case GwPaint::fcInvR0: this->fcInvR0.setARGB(a, r, g, b); break;
            case GwPaint::fcTra0: this->fcTra0.setARGB(a, r, g, b); break;
            case GwPaint::fcSoftClip0: this->fcSoftClip0.setARGB(a, r, g, b); break;
            case GwPaint::fcBigWig: this->fcBigWig.setARGB(a, r, g, b); break;
            case GwPaint::ecMateUnmapped: this->ecMateUnmapped.setARGB(a, r, g, b); break;
            case GwPaint::ecSplit: this->ecSplit.setARGB(a, r, g, b); break;
            case GwPaint::ecSelected: this->ecSelected.setARGB(a, r, g, b); break;
            case GwPaint::lcJoins: this->lcJoins.setARGB(a, r, g, b); break;
            case GwPaint::lcCoverage: this->lcCoverage.setARGB(a, r, g, b); break;
            case GwPaint::lcLightJoins: this->lcLightJoins.setARGB(a, r, g, b); break;
            case GwPaint::lcGTFJoins: this->lcGTFJoins.setARGB(a, r, g, b); break;
            case GwPaint::lcLabel: this->lcLabel.setARGB(a, r, g, b); break;
            case GwPaint::lcBright: this->lcBright.setARGB(a, r, g, b); break;
            case GwPaint::tcDel: this->tcDel.setARGB(a, r, g, b); break;
            case GwPaint::tcIns: this->tcIns.setARGB(a, r, g, b); break;
            case GwPaint::tcLabels: this->tcLabels.setARGB(a, r, g, b); break;
            case GwPaint::tcBackground: this->tcBackground.setARGB(a, r, g, b); break;
            case GwPaint::fcMarkers: this->fcMarkers.setARGB(a, r, g, b); break;
            case GwPaint::fcRoi: this->fcRoi.setARGB(a, r, g, b); break;
            default: break;
        }
    }

    void BaseTheme::getPaintARGB(int paint_enum, int& alpha, int& red, int& green, int& blue) {
        SkPaint p;
        switch (paint_enum) {
            case GwPaint::bgPaint: p = this->bgPaint; break;
            case GwPaint::bgPaintTiled: p = this->bgPaintTiled; break;
            case GwPaint::fcNormal: p = this->fcNormal; break;
            case GwPaint::fcDel: p = this->fcDel; break;
            case GwPaint::fcDup: p = this->fcDup; break;
            case GwPaint::fcInvF: p = this->fcInvF; break;
            case GwPaint::fcInvR: p = this->fcInvR; break;
            case GwPaint::fcTra: p = this->fcTra; break;
            case GwPaint::fcIns: p = this->fcIns; break;
            case GwPaint::fcSoftClip: p = this->fcSoftClip; break;
            case GwPaint::fcA: p = this->fcA; break;
            case GwPaint::fcT: p = this->fcT; break;
            case GwPaint::fcC: p = this->fcC; break;
            case GwPaint::fcG: p = this->fcG; break;
            case GwPaint::fcN: p = this->fcN; break;
            case GwPaint::fc5mc: p = this->fc5mc; break;
            case GwPaint::fc5hmc: p = this->fc5hmc; break;
            case GwPaint::fcOther: p = this->fcOther; break;
            case GwPaint::fcCoverage: p = this->fcCoverage; break;
            case GwPaint::fcTrack: p = this->fcTrack; break;
            case GwPaint::fcNormal0: p = this->fcNormal0; break;
            case GwPaint::fcDel0: p = this->fcDel0; break;
            case GwPaint::fcDup0: p = this->fcDup0; break;
            case GwPaint::fcInvF0: p = this->fcInvF0; break;
            case GwPaint::fcInvR0: p = this->fcInvR0; break;
            case GwPaint::fcTra0: p = this->fcTra0; break;
            case GwPaint::fcSoftClip0: p = this->fcSoftClip0; break;
            case GwPaint::fcBigWig: p = this->fcBigWig; break;
            case GwPaint::ecMateUnmapped: p = this->ecMateUnmapped; break;
            case GwPaint::ecSplit: p = this->ecSplit; break;
            case GwPaint::ecSelected: p = this->ecSelected; break;
            case GwPaint::lcJoins: p = this->lcJoins; break;
            case GwPaint::lcCoverage: p = this->lcCoverage; break;
            case GwPaint::lcLightJoins: p = this->lcLightJoins; break;
            case GwPaint::lcGTFJoins: p = this->lcGTFJoins; break;
            case GwPaint::lcLabel: p = this->lcLabel; break;
            case GwPaint::lcBright: p = this->lcBright; break;
            case GwPaint::tcDel: p = this->tcDel; break;
            case GwPaint::tcIns: p = this->tcIns; break;
            case GwPaint::tcLabels: p = this->tcLabels; break;
            case GwPaint::tcBackground: p = this->tcBackground; break;
            case GwPaint::fcMarkers: p = this->fcMarkers; break;
            case GwPaint::fcRoi: p = this->fcRoi; break;
            default: break;
        }
        SkColor clr = p.getColor();
        alpha = SkColorGetA(clr);
        red = SkColorGetR(clr);
        green = SkColorGetG(clr);
        blue = SkColorGetB(clr);
    }

    EXPORT IgvTheme::IgvTheme() {
        name = "igv";
        fcCoverage.setARGB(255, 195, 195, 195);
        fcTrack.setARGB(200, 20, 20, 20);
        bgPaint.setARGB(255, 255, 255, 255);
        bgPaintTiled.setARGB(255, 235, 235, 235);
        bgMenu.setARGB(255, 248, 248, 246);
        fcNormal.setARGB(255, 202, 202, 202);
        fcDel.setARGB(255, 225, 19, 67);
        fcDup.setARGB(255, 0, 54, 205);
        fcInvF.setARGB(255, 0, 199, 50);
        fcInvR.setARGB(255, 0, 199, 50);
        fcTra.setARGB(255, 255, 105, 180);
        fcSoftClip.setARGB(255, 96,160,176);
        fcA.setARGB(255, 109, 230, 64);
        fcT.setARGB(255, 255, 0, 107);
        fcC.setARGB(255, 66, 127, 255);
        fcG.setARGB(255, 235, 150, 23);
        fcN.setARGB(255, 128, 128, 128);
        fcIns.setARGB(255, 158, 112, 250);

        fc5mc.setARGB(255, 155, 20, 15);
        fc5hmc.setARGB(255, 10, 100, 250);
        lcJoins.setARGB(255, 80, 80, 80);
        lcLightJoins.setARGB(255, 140, 140, 140);
        lcGTFJoins.setARGB(255, 145, 172, 190);
        lcLabel.setARGB(255, 80, 80, 80);
        lcBright.setColor(SK_ColorBLACK);
        tcDel.setARGB(255, 80, 80, 80);
        tcLabels.setARGB(255, 80, 80, 80);
        tcIns.setARGB(255, 255, 255, 255);
        tcBackground.setARGB(255, 255, 255, 255);
        fcMarkers.setARGB(255, 0, 0, 0);
        ecSelected.setARGB(255, 0, 0, 0);
        ecSelected.setStyle(SkPaint::kStroke_Style);
        ecSelected.setStrokeWidth(2);
        ecSplit.setARGB(255, 0, 0, 255);
        ecSplit.setStyle(SkPaint::kStroke_Style);
        ecSplit.setStrokeWidth(1);
    }

    EXPORT DarkTheme::DarkTheme() {
        name = "dark";
        fcCoverage.setARGB(255, 95, 95, 105);
        fcTrack.setARGB(200, 227, 232, 255);
        bgPaint.setARGB(255, 10, 10, 20);
        bgPaintTiled.setARGB(255, 40, 40, 40);
        bgMenu.setARGB(255, 30, 30, 40);
        fcNormal.setARGB(255, 90, 90, 95);
        fcDel.setARGB(255, 185, 25, 25);
        fcDup.setARGB(255, 24, 100, 198);
        fcInvF.setARGB(255, 49, 167, 118);
        fcInvR.setARGB(255, 49, 167, 0);
        fcTra.setARGB(255, 225, 185, 185);
        fcSoftClip.setARGB(255, 0, 128, 128);
        fcA.setARGB(255, 106, 186, 79);
        fcT.setARGB(255, 232, 55, 99);
        fcC.setARGB(255, 77, 125, 245);
        fcG.setARGB(255, 226, 132, 19);
        fcN.setARGB(255, 128, 128, 128);
        fcIns.setARGB(255, 158, 112, 250);
//        fc5mc.setARGB(127, 252, 186, 3);
        fc5mc.setARGB(227, 30, 176, 230);
//        fc5hmc.setARGB(127, 52, 255, 96);
        fc5hmc.setARGB(227, 215, 85, 23);
        lcJoins.setARGB(255, 142, 142, 142);
        lcLightJoins.setARGB(255, 82, 82, 82);
        lcGTFJoins.setARGB(255, 140, 140, 170);
        lcLabel.setARGB(255, 182, 182, 182);
        lcBright.setColor(SK_ColorWHITE);
        tcDel.setARGB(255, 227, 227, 227);
        tcLabels.setARGB(255, 0, 0, 0);
        tcIns.setARGB(255, 227, 227, 227);
        tcBackground.setARGB(255, 10, 10, 20);
        fcMarkers.setARGB(255, 220, 220, 220);
        ecSelected.setARGB(255, 255, 255, 255);
        ecSelected.setStyle(SkPaint::kStroke_Style);
        ecSelected.setStrokeWidth(2);
        ecSplit.setARGB(255, 109, 160, 199);
        ecSplit.setStyle(SkPaint::kStroke_Style);
        ecSplit.setStrokeWidth(1);
    }

    EXPORT SlateTheme::SlateTheme() {
        name = "slate";
        fcCoverage.setARGB(255, 103, 102, 109);
        fcTrack.setARGB(220, 227, 232, 255);
        bgPaint.setARGB(255, 45, 45, 48);
        bgPaintTiled.setARGB(255, 25, 25, 28);
        bgMenu.setARGB(255, 0, 0, 0);
        fcNormal.setARGB(255, 93, 92, 99);
        fcDel.setARGB(255, 185, 25, 25);
        fcIns.setARGB(255, 128, 91, 240);
        fcDup.setARGB(255, 24, 100, 198);
        fcInvF.setARGB(255, 49, 167, 118);
        fcInvR.setARGB(255, 49, 167, 0);
        fcTra.setARGB(255, 225, 185, 185);
        fcSoftClip.setARGB(255, 0, 128, 128);
        fcA.setARGB(255, 105, 213, 92);
        fcT.setARGB(255, 232, 55, 99);
        fcC.setARGB(255, 77, 155, 245);
        fcG.setARGB(255, 226, 132, 19);
        fcN.setARGB(255, 128, 128, 128);
//        fc5mc.setARGB(127, 252, 186, 3);
        fc5mc.setARGB(227, 30, 176, 230);
//        fc5hmc.setARGB(127, 52, 255, 96);
        fc5hmc.setARGB(227, 215, 85, 23);
        lcJoins.setARGB(255, 142, 142, 142);
        lcLightJoins.setARGB(255, 82, 82, 82);
        lcGTFJoins.setARGB(255, 140, 140, 170);
        lcLabel.setARGB(255, 182, 182, 182);
        lcBright.setColor(SK_ColorWHITE);
        tcDel.setARGB(255, 255, 255, 255);
        tcLabels.setARGB(255, 100, 100, 100);
        tcIns.setARGB(255, 227, 227, 227);
        tcBackground.setARGB(255, 10, 10, 20);
        fcMarkers.setARGB(255, 220, 220, 220);
        ecSelected.setARGB(255, 255, 255, 255);
        ecSelected.setStyle(SkPaint::kStroke_Style);
        ecSelected.setStrokeWidth(2);
        ecSplit.setARGB(255, 109, 160, 199);
        ecSplit.setStyle(SkPaint::kStroke_Style);
        ecSplit.setStrokeWidth(1);
    }

    EXPORT IniOptions::IniOptions() {
        menu_level = "";
        menu_table = MAIN;
        theme_str = "dark";
        dimensions_str = "1366x768";
        dimensions = {1366, 768};
        link = "none";
        link_op = 0;
        number_str = "3x3";
        number = {3, 3};
        labels = "PASS,FAIL";
        canvas_width = 0;
        canvas_height = 0;
        indel_length = 10;
        ylim = 50;
        split_view_size = 10000;
        threads = 3;
        pad = 500;
        start_index = 0;
        font_str = "Menlo";
        font_size = 14;
        mods_qual_threshold = 50;

        soft_clip_threshold = 20000;
        small_indel_threshold = 100000;
        snp_threshold = 1000000;
        mod_threshold = 250000;
        edge_highlights = 100000;
        variant_distance = 100000;
        low_memory = 1500000;

        max_coverage = 100000;
        max_tlen = 2000;

        editing_underway = false;
        no_show = false;
        log2_cov = false;
        tlen_yscale = false;
        expand_tracks = false;
        vcf_as_tracks = false;
        bed_as_tracks = false;
        sv_arcs = true;
        parse_mods = false;
        scale_bar = true;
        alignments = true;
        data_labels = true;

        scroll_speed = 0.15;
        tab_track_height = 0.25;
        read_y_gap = 1;
        scroll_right = GLFW_KEY_RIGHT;
        scroll_left = GLFW_KEY_LEFT;
        scroll_down = GLFW_KEY_PAGE_DOWN;
        scroll_up = GLFW_KEY_PAGE_UP;
        next_region_view = GLFW_KEY_RIGHT_BRACKET;
        previous_region_view = GLFW_KEY_LEFT_BRACKET;
        zoom_out = GLFW_KEY_DOWN;
        zoom_in = GLFW_KEY_UP;
        cycle_link_mode = GLFW_KEY_L;
        find_alignments=GLFW_KEY_F;
        repeat_command=GLFW_KEY_R;
    }

    void IniOptions::setTheme(std::string &theme_str) {
        if (theme_str == "slate") {
            this->theme_str = theme_str; theme = SlateTheme();
        } else if (theme_str == "igv") {
            this->theme_str = theme_str; theme = IgvTheme();
        } else if (theme_str == "dark") {
            this->theme_str = theme_str; theme = DarkTheme();
        } else {
            std::cerr << "theme_str must be slate, igv, or dark\n";
        }
    }

    void IniOptions::getOptionsFromIni() {

        std::unordered_map<std::string, int> key_table;
        Keys::getKeyTable(key_table);
        bool update_ini = false;

        mINI::INIMap<std::string>& sub = myIni["general"];

        theme_str = sub["theme"];
        if (theme_str == "dark") { theme = Themes::DarkTheme(); } else if (theme_str == "slate") { theme = Themes::SlateTheme(); } else { theme = Themes::IgvTheme(); }
        dimensions_str = sub["dimensions"];
        dimensions = Utils::parseDimensions(dimensions_str);

        link = sub["link"];
        link_op = 0;
        if (link == "sv") {
            link_op = 1;
        } else if (link == "all") {
            link_op = 2;
        } else {
            link = "none";
        }

        indel_length = std::stoi(sub["indel_length"]);
        ylim = std::stoi(sub["ylim"]);
        split_view_size = std::stoi(sub["split_view_size"]);
        threads = std::stoi(sub["threads"]);
        pad = std::stoi(sub["pad"]);
        max_coverage = (sub["coverage"] == "true") ? 100000 : 0;
        log2_cov = sub["log2_cov"] == "true";
        scroll_speed = std::stof(sub["scroll_speed"]);
        tab_track_height = std::stof(sub["tabix_track_height"]);
        if (sub.has("font")) {
            font_str = sub["font"];
        } else {
            sub["font"] = "Menlo";
            update_ini = true;
        }
        if (sub.has("font_size")) {
            font_size = std::stoi(sub["font_size"]);
        } else {
            sub["font_size"] = "14";
            update_ini = true;
        }
        if (sub.has("expand_tracks")) {
            expand_tracks = sub["expand_tracks"] == "true";
        } else {
            sub["expand_tracks"] = "true";
            update_ini = true;
        }
        if (sub.has("scale_bar")) {
            scale_bar = sub["scale_bar"] == "true";
        } else {
            sub["scale_bar"] = "true";
            update_ini = true;
        }
        if (sub.has("data_labels")) {
            data_labels = sub["data_labels"] == "true";
        } else {
            sub["data_labels"] = "false";
            update_ini = true;
        }
        if (sub.has("sv_arcs")) {
            sv_arcs = sub["sv_arcs"] == "true";
        } else {
            sub["sv_arcs"] = "true";
            update_ini = true;
        }
        if (sub.has("mods")) {
            parse_mods = sub["mods"] == "true";
        } else {
            sub["mods"] = "false";
            update_ini = true;
        }
        if (sub.has("mods_qual_threshold")) {
            mods_qual_threshold = std::stoi(sub["mods_qual_threshold"]);
        } else {
            sub["mods_qual_threshold"] = "50";
            update_ini = true;
        }
        if (sub.has("read_y_gap")) {
            read_y_gap = std::stoi(sub["read_y_gap"]);
        } else {
            sub["read_y_gap"] = "1";
            update_ini = true;
        }
        if (sub.has("session_file")) {
            session_file = sub["session_file"];
        }  // defer update to saveCurrentSession

        mINI::INIMap<std::string>& vt = myIni["view_thresholds"];

        soft_clip_threshold = std::stoi(vt["soft_clip"]);
        small_indel_threshold = std::stoi(vt["small_indel"]);
        snp_threshold = std::stoi(vt["snp"]);
        if (vt.has("edge_highlights")) {
            edge_highlights = std::stoi(vt["edge_highlights"]);
        } else {
            vt["edge_highlights"] = "1000000";
            update_ini = true;
        }
        if (vt.has("variant_distance")) {
            variant_distance = std::stoi(vt["variant_distance"]);
        } else {
            vt["variant_distance"] = "100000";
            update_ini = true;
        }
        if (vt.has("low_memory")) {
            low_memory = std::stoi(vt["low_memory"]);
        } else {
            vt["low_memory"] = "1500000";
            update_ini = true;
        }
        if (vt.has("mod")) {
            mod_threshold = std::stoi(vt["mod"]);
        } else {
            vt["mod"] = "1000000";
        }

        mINI::INIMap<std::string>& ng = myIni["navigation"];

        scroll_right = key_table[ng["scroll_right"]];
        scroll_left = key_table[ng["scroll_left"]];
        scroll_up = key_table[ng["scroll_up"]];
        scroll_down = key_table[ng["scroll_down"]];
        zoom_out = key_table[ng["zoom_out"]];
        zoom_in = key_table[ng["zoom_in"]];

        mINI::INIMap<std::string>& ia = myIni["interaction"];

        cycle_link_mode = key_table[ia["cycle_link_mode"]];

        if (ia.has("find_alignments")) {
            find_alignments = key_table[ia["find_alignments"]];
        } else {
            ia["find_alignments"] = "F";
            update_ini = true;
        }
        if (ia.has("repeat_command")) {
            repeat_command = key_table[ia["repeat_command"]];
        } else {
            ia["repeat_command"] = "R";
            update_ini = true;
        }
        if (ia.has("vcf_as_tracks")) {
            vcf_as_tracks = ia["vcf_as_tracks"] == "true";
        } else {
            ia["vcf_as_tracks"] = "false";
            update_ini = true;
        }
        if (ia.has("bed_as_tracks")) {
            bed_as_tracks = ia["bed_as_tracks"] == "true";
        } else {
            ia["bed_as_tracks"] = "true";
            update_ini = true;
        }

        mINI::INIMap<std::string>& lb = myIni["labelling"];

        number_str = lb["number"];
        number = Utils::parseDimensions(number_str);
        parse_label = lb["parse_label"];

        labels = lb["labels"];

        if (myIni.has("shift_keymap")) {

            mINI::INIMap<std::string>& km = myIni["shift_keymap"];

            shift_keymap["'"] = "@";
            shift_keymap["="] = "+";
            shift_keymap["2"] = "\"";
            shift_keymap["3"] = "£";
            shift_keymap["5"] = "%";
            shift_keymap["6"] = "^";
            shift_keymap["8"] = "*";
            shift_keymap["9"] = "(";
            shift_keymap["0"] = ")";
            shift_keymap["`"] = "~";

            if (km.has("ampersand")) {
                shift_keymap[km["ampersand"]] = "&";
            } else {
                shift_keymap["7"] = "&";
                update_ini = true;
            }
            if (km.has("bar")) {
                shift_keymap[km["bar"]] = "|";
            } else {
                shift_keymap["\\"] = "|";
                update_ini = true;
            }
            if (km.has("colon")) {
                shift_keymap[km["colon"]] = ":";
            } else {
                shift_keymap[";"] = ":";
                update_ini = true;
            }
            if (km.has("curly_open")) {
                shift_keymap[km["curly_open"]] = "{";
            } else {
                shift_keymap["["] = "{";
                update_ini = true;
            }
            if (km.has("curly_close")) {
                shift_keymap[km["curly_close"]] = "{";
            } else {
                shift_keymap["]"] = "}";
                update_ini = true;
            }
            if (km.has("dollar")) {
                shift_keymap[km["dollar"]] = "$";
            } else {
                shift_keymap["4"] = "$";
                update_ini = true;
            }
            if (km.has("exclamation")) {
                shift_keymap[km["exclamation"]] = "!";
            } else {
                shift_keymap["1"] = "!";
                update_ini = true;
            }
            if (km.has("greater_than")) {
                shift_keymap[km["greater_than"]] = ">";
            } else {
                shift_keymap["."] = ">";
                update_ini = true;
            }
            if (km.has("less_than")) {
                shift_keymap[km["less_than"]] = "<";
            } else {
                shift_keymap[","] = "<";
                update_ini = true;
            }
            if (km.has("tilde")) {
                shift_keymap[km["tilde"]] = "~";
            } else {
                shift_keymap["`"] = "~";
                update_ini = true;
            }
            if (km.has("underscore")) {
                shift_keymap[km["underscore"]] = "_";
            } else {
                shift_keymap["-"] = "_";
                update_ini = true;
            }

        }
        if (update_ini) {
            saveIniChanges();
        }
    }

    void IniOptions::getOptionsFromSessionIni(mINI::INIStructure& sesh) {
        if (sesh.has("general")) {
            mINI::INIMap<std::string>& sub = sesh["general"];
            if (sub.has("theme")) {
                theme_str = sub["theme"];
                if (theme_str == "dark") {
                    theme = Themes::DarkTheme();
                } else if (theme_str == "slate") {
                    theme = Themes::SlateTheme();
                } else {
                    theme = Themes::IgvTheme();
                }
                theme.name = theme_str;
            }
            if (sub.has("dimensions")) {
                dimensions_str = sub["dimensions"];
                dimensions = Utils::parseDimensions(dimensions_str);
            }
            if (sub.has("indel_length")) {
                indel_length = std::stoi(sub["indel_length"]);
            }
            if (sub.has("ylim")) {
                ylim = std::stoi(sub["ylim"]);
            }
            if (sub.has("coverage")) {
                max_coverage = (sub["coverage"] == "true") ? 100000 : 0;
            }
            if (sub.has("log2_cov")) {
                log2_cov = sub["log2_cov"] == "true";
            }
            if (sub.has("expand_tracks")) {
                expand_tracks = sub["expand_tracks"] == "true";
            }
            if (sub.has("scale_bar")) {
                scale_bar = sub["scale_bar"] == "true";
            }
            if (sub.has("alignments")) {
                alignments = sub["alignments"] == "true";
            }
            if (sub.has("data_labels")) {
                data_labels = sub["data_labels"] == "true";
            }
            if (sub.has("link")) {
                link = sub["link"];
                link_op = 0;
                if (link == "sv") {
                    link_op = 1;
                } else if (link == "all") {
                    link_op = 2;
                } else {
                    link = "none";
                }
            }
            if (sub.has("split_view_size")) {
                split_view_size = std::stoi(sub["split_view_size"]);
            }
            if (sub.has("pad")) {
                pad = std::stoi(sub["pad"]);
            }
            if (sub.has("read_y_gap")) {
                read_y_gap = std::stof(sub["read_y_gap"]);
            }
            if (sub.has("tabix_track_height")) {
                tab_track_height = std::stof(sub["tabix_track_height"]);
            }
            if (sub.has("font")) {
                font_str = sub["font"];
            }
            if (sub.has("font_size")) {
                font_size = std::stoi(sub["font_size"]);
            }
            if (sub.has("mods_qual_threshold")) {
                mods_qual_threshold = std::stoi(sub["mods_qual_threshold"]);
            }
            if (sub.has("mods")) {
                parse_mods = sub["mods"] == "true";
            }
        }
        if (sesh.has("view_thresholds")) {
            mINI::INIMap<std::string>& vt = sesh["view_thresholds"];
            if (vt.has("soft_clip")) {
                soft_clip_threshold = std::stoi(vt["soft_clip"]);
            }
            if (vt.has("small_indel_threshold")) {
                small_indel_threshold = std::stoi(vt["small_indel_threshold"]);
            }
            if (vt.has("snp_threshold")) {
                snp_threshold = std::stoi(vt["snp_threshold"]);
            }
            if (vt.has("edge_highlights")) {
                edge_highlights = std::stoi(vt["edge_highlights"]);
            }
        }
        if (sesh.has("labelling")) {
            mINI::INIMap<std::string> &lb = sesh["labelling"];
            if (lb.has("number")) {
                number_str = lb["number"];
                number = Utils::parseDimensions(number_str);
            }
            if (lb.has("parse_label")) {
                parse_label =lb["parse_label"];
            }
            if (lb.has("labels")) {
                labels = lb["labels"];
            }
        }
    }

    std::filesystem::path IniOptions::writeDefaultIni(std::filesystem::path &homedir, std::filesystem::path &home_config, std::filesystem::path &gwIni) {
        std::ofstream outIni;
        std::filesystem::path outPath;
        if (std::filesystem::exists(homedir / home_config)) {
            outPath = homedir / home_config / gwIni ;
        } else if (std::filesystem::exists(homedir)) {
            outPath = homedir / gwIni;
        } else {
            return outPath;
        }
        outIni.open(outPath);
        if (!outIni) {
            return outPath;
        }
        std::cout << "Saving .gw.ini file to " << outPath.string() << std::endl;
        outIni << DefaultIni::defaultIniString();
        outIni.close();
        return outPath;
    }

    bool IniOptions::readIni() {

#if defined(_WIN32) || defined(_WIN64)
        const char *homedrive_c = std::getenv("HOMEDRIVE");
        const char *homepath_c = std::getenv("HOMEPATH");
        std::string homedrive(homedrive_c ? homedrive_c : "");
        std::string homepath(homepath_c ? homepath_c : "");
        std::string home = homedrive + homepath;
#else
        struct passwd *pw = getpwuid(getuid());
        std::string home(pw->pw_dir);
#endif
        std::filesystem::path path;
        std::filesystem::path homedir(home);
        std::filesystem::path gwini(".gw.ini");
//        std::filesystem::path gw_session(".gw_session.ini");
        if (std::filesystem::exists(homedir / gwini)) {
            path = homedir / gwini;
        } else {
            std::filesystem::path home_config(".config");
            if (std::filesystem::exists(homedir / home_config / gwini)) {
                path = homedir / home_config / gwini;
            } else {
                std::filesystem::path exe_path (Utils::getExecutableDir());
                if (std::filesystem::exists(exe_path / gwini)) {
                    path = exe_path / gwini;
                } else {
                    path = writeDefaultIni(homedir, home_config, gwini);
                    if (path.empty()) {
                        std::cerr << "Error: .gw.ini file could not be read or created. Unexpected behavior may arise\n";
                        theme = Themes::DarkTheme();

                        return false;
                    }
                }
            }
        }
        ini_path = path.string();
        mINI::INIFile file(ini_path);
        file.read(myIni);
        getOptionsFromIni();
        return true;
    }

    void IniOptions::saveIniChanges() {
        mINI::INIFile file(ini_path);
        file.write(myIni, true);
    }

    void IniOptions::saveCurrentSession(std::string& genome_path, std::string& ideogram_path, std::vector<std::string>& bam_paths,
                                        std::vector<std::string>& track_paths, std::vector<Utils::Region>& regions,
                                        std::vector<std::pair<std::string, int>>& variant_paths_info,
                                        std::vector<std::string>& commands, std::string output_session,
                                        int mode, int window_x_pos, int window_y_pos, float monitorScale,
                                        int screen_width, int screen_height, int sortReadsBy) {
        if (output_session.empty()) {

            if (session_file.empty()) {  // fill new session
                std::filesystem::path gwini(ini_path);
                std::filesystem::path sesh(".gw_session.ini");
                std::filesystem::path sesh_path = gwini.parent_path() / sesh;
                myIni["general"]["session_file"] = sesh_path.string();
                saveIniChanges();
            }
            output_session = myIni["general"]["session_file"];
        }
        mINI::INIFile file(output_session);

        seshIni.clear();
        seshIni["data"]["genome_tag"] = genome_tag;
        seshIni["data"]["genome_path"] = genome_path;
        if (!ideogram_path.empty()) {
            seshIni["data"]["ideogram_path"] = ideogram_path;
        }
        int count = 0;
        for (const auto& item : bam_paths) {
            seshIni["data"]["bam" + std::to_string(count)] = item;
            count += 1;
        }
        count = 0;
        for (const auto& item : track_paths) {
            seshIni["data"]["track" + std::to_string(count)] = item;
            count += 1;
        }
        count = 0;
        for (auto& item: variant_paths_info) {
            std::filesystem::path fspath(item.first);
            std::string path = std::filesystem::absolute(fspath).string();
            seshIni["data"]["var" + std::to_string(count)] = path;
            count += 1;
        }
        count = 0;
        for (auto& item: regions) {
            seshIni["show"]["region" + std::to_string(count)] = item.toString();
            count += 1;
        }
        seshIni["show"]["mode"] = (mode == 1) ? "tiled" : "single";

        if (sortReadsBy == 0) {
            seshIni["show"]["sort"] = "none";
        } else if (sortReadsBy == 1) {
            seshIni["show"]["sort"] = "strand";
        } else if (sortReadsBy == 2) {
            seshIni["show"]["sort"] = "hap";
        }

        count = 0;
        for (auto& item: variant_paths_info) {
            seshIni["show"]["var" + std::to_string(count)] = std::to_string(item.second);
            count += 1;
        }

        count = 0;
        size_t last_refresh = 0;
        std::vector<std::string> keep = {"egdes", "expand-tracks", "soft-clips", "sc", "mm", "mismatches", "ins", "insertions", "mods"};
        for (auto& item: commands) {
            for (const auto& k: keep) {
                if (Utils::startsWith(item, k)) {
                    seshIni["commands"][std::to_string(count)] = item;
                }
            }
            if (item == "r" || item == "refresh") {
                last_refresh = count;
            }
            count += 1;
        }
        keep = {"filter ", "find ", "f ", "roi"}; // "colour", "color" need to be matched with tracks, other rm track causes issues
        size_t j = 0;
        for (; last_refresh < commands.size(); ++last_refresh) {
            for (const auto& k: keep) {
                if (Utils::startsWith(commands[last_refresh], k)) {
                    seshIni["commands"][std::to_string(j)] = commands[last_refresh];
                    j++;
                }
            }
        }
        mINI::INIMap<std::string>& sub = seshIni["general"];
        sub["theme"] = theme_str;
        sub["dimensions"] = std::to_string(screen_width) + "x" + std::to_string(screen_height);
        sub["window_position"] = std::to_string(window_x_pos) + "x" + std::to_string(window_y_pos);
        sub["ylim"] = std::to_string(ylim);
        sub["coverage"] = (max_coverage) ? "true" : "false";
        sub["log2_cov"] = (log2_cov) ? "true" : "false";
        sub["expand_tracks"] = (expand_tracks) ? "true" : "false";
        sub["scale_bar"] = (scale_bar) ? "true" : "false";
        sub["alignments"] = (alignments) ? "true" : "false";
        sub["data_labels"] = (data_labels) ? "true" : "false";
        sub["mods"] = (parse_mods) ? "true" : "false";
        sub["link"] = link;
        sub["split_view_size"] = std::to_string(split_view_size);
        sub["pad"] = std::to_string(pad);
        sub["read_y_gap"] = std::to_string(read_y_gap);
        sub["tabix_track_height"] = std::to_string(tab_track_height);
        sub["font"] = font_str;
        sub["font_size"] = std::to_string(font_size);
        sub["mods_qual_threshold"] = std::to_string(mods_qual_threshold);

        mINI::INIMap<std::string>& vt = seshIni["view_thresholds"];
        vt["soft_clip"] = std::to_string(soft_clip_threshold);
        vt["small_indel"] = std::to_string(small_indel_threshold);
        vt["snp"] = std::to_string(snp_threshold);
        vt["mod"] = std::to_string(mod_threshold);
        vt["edge_highlights"] = std::to_string(edge_highlights);

        mINI::INIMap<std::string>& lb = seshIni["labelling"];
        lb["number"] = std::to_string(number.x) + "x" + std::to_string(number.y);
        lb["parse_label"] = parse_label;
        lb["labels"] = labels;

        file.generate(seshIni, true);
    }

    void printAvailableFonts() {
#if !defined(OLD_SKIA) || OLD_SKIA == 0
        sk_sp<SkFontMgr> fontManager = SkFontMgr::RefEmpty();
#else
        sk_sp<SkFontMgr> fontManager = SkFontMgr::RefDefault();
#endif
        int familyCount = fontManager->countFamilies();
        std::cerr << "Available font families:" << std::endl;
        std::vector<std::string> fontFamilies;
        for (int i = 0; i < familyCount; ++i) {
            SkString familyName;
            fontManager->getFamilyName(i, &familyName);
            fontFamilies.push_back(familyName.c_str());
        }
        std::sort(fontFamilies.begin(), fontFamilies.end());
        for (const auto& family : fontFamilies) {
            std::cerr << family << std::endl;
        }
        std::cerr << "Total font families: " << familyCount << std::endl;
    }

    const SkGlyphID glyphs[1] = {100};

    EXPORT Fonts::Fonts() {
        rect = SkRect::MakeEmpty();
        path = SkPath();
    }

#if !defined(OLD_SKIA) || OLD_SKIA == 0
    sk_sp<SkFontMgr> createFontManager(bool system_default) {
        sk_sp<SkFontMgr> fontMgr;

#if defined(__APPLE__)

        if (system_default) {
            // This tries to avoid loading all the system fonts, and just choose the default system font
            CFStringRef fontFamilyName = CFSTR(".AppleSystemUIFont");

            CFDictionaryRef attributes = CFDictionaryCreate(
                    kCFAllocatorDefault,
                    (const void**)&kCTFontFamilyNameAttribute,
                    (const void**)&fontFamilyName,
                    1,
                    &kCFTypeDictionaryKeyCallBacks,
                    &kCFTypeDictionaryValueCallBacks
            );
            if (!attributes) { return nullptr; }

            CTFontDescriptorRef descriptor = CTFontDescriptorCreateWithAttributes(attributes);
            CFRelease(attributes);
            if (!descriptor) { return nullptr; }

            CFArrayRef descriptorArray = CFArrayCreate(
                    kCFAllocatorDefault,
                    (const void**)&descriptor,
                    1,
                    &kCFTypeArrayCallBacks
            );
            CFRelease(descriptor);
            if (!descriptorArray) { return nullptr; }

            CTFontCollectionRef collection = CTFontCollectionCreateWithFontDescriptors(
                    descriptorArray,
                    nullptr
            );
            CFRelease(descriptorArray);
            if (!collection) { return nullptr; }

            fontMgr = SkFontMgr_New_CoreText(collection);
            CFRelease(collection);

        } else {
            CTFontCollectionRef collection = CTFontCollectionCreateFromAvailableFonts(nullptr);
            fontMgr = SkFontMgr_New_CoreText(collection);
            CFRelease(collection);
        }

        if (!fontMgr) {
            std::cerr << (fontMgr != nullptr) << " Error: failed to create font manager. Text will likely be missing\n";
        } else {
            char *val = getenv("GW_DEBUG");
            if (val) {
                std::cerr << "SkFontMgr\n";
                int count = fontMgr->countFamilies();
                std::cerr << "SkFontMgr has " << count << " different fonts" << std::endl;
                if (count > 0) {
                    SkString familyName;
                    fontMgr->getFamilyName(0, &familyName);
                    std::cerr << "First family name is " << familyName.c_str() << std::endl;
                }
            }

        }
#elif defined(_WIN32) || defined(_WIN64)
        fontMgr = SkFontMgr_New_DirectWrite();
        if (!fontMgr) {
            std::cerr << "Error: failed to create DirectWrite font manager on Windows\n";
        }
#else
        // Use FontConfig for all other platforms
        FcInit();
        FcConfig* config = FcInitLoadConfigAndFonts();
        if (system_default) {
            FcPattern* pattern = FcPatternCreate();
            FcPatternAddString(pattern, FC_FAMILY, (const FcChar8*)"sans-serif");
            FcConfigSubstitute(config, pattern, FcMatchPattern);
            FcDefaultSubstitute(pattern);

            FcResult result;
            FcPattern* font = FcFontMatch(config, pattern, &result);

            if (font) {
                fontMgr = SkFontMgr_New_FontConfig(config);
                FcPatternDestroy(font);
            }
            FcPatternDestroy(pattern);
        } else {
            // Load all fonts
            fontMgr = SkFontMgr_New_FontConfig(nullptr);
        }
#endif
        return fontMgr;

    }

    void Fonts::setTypeface(std::string &fontStr, int size) {

        bool system_default = (fontStr == "Default");
        sk_sp<SkFontMgr> fontMgr = createFontManager(system_default);
        sk_sp<SkTypeface> face;
        if (system_default) {
            sk_sp<SkData> fontData = SkData::MakeWithoutCopy(GwFonts::Aileron_Regular_otf, GwFonts::Aileron_Regular_otf_len);
            face = fontMgr->makeFromData(fontData);
            if (!face) {
                std::cerr << "Error: failed to create font. Text might be missing\n";
                face = fontMgr->matchFamilyStyle(nullptr, SkFontStyle::Normal());
            }
        } else {
            const char * font_c = fontStr.c_str();
            face = fontMgr->matchFamilyStyle(font_c, SkFontStyle::Normal());
            if (!face) {
                face = fontMgr->matchFamilyStyle(nullptr, SkFontStyle::Normal());
                SkString familyName;
                face->getFamilyName(&familyName);
                std::cerr << "\nWarning: font '" << fontStr << "' could not be loaded, using system default instead '" << familyName.c_str() << ". et GW_DEBUG=1 to display available fonts.\n";
                char *val = getenv("GW_DEBUG");
                if (val) {
                    printAvailableFonts();
                }
            } else {
                SkString familyName;
                face->getFamilyName(&familyName);
                if (familyName.c_str() != fontStr) {
                    std::cerr << "\nWarning: font '" << fontStr << "' could not be loaded, swapped for '" << familyName.c_str() << " instead. Set GW_DEBUG=1 to display available fonts.\n";
                    char *val = getenv("GW_DEBUG");
                    if (val) {
                        printAvailableFonts();
                    }
                }
            }
        }

        overlay = SkFont(face);
        SkScalar ts = size;
        overlay.setSize(ts);
        fontTypefaceSize = size;
    }

#else
    void Fonts::setTypeface(std::string &fontStr, int size) {

        if (fontStr == "Default") {
            face = SkTypeface::MakeDefault();
            if (!face) {
                std::cerr << "Error: failed to create font. Text will likely be missing\n";
            }
        } else {
            const char * font_c = fontStr.c_str();
            face = SkTypeface::MakeFromName(font_c, SkFontStyle::Normal());
            if (!face) {
                face = SkTypeface::MakeDefault();
                SkString familyName;
                face->getFamilyName(&familyName);
                std::cerr << "\nWarning: font '" << fontStr << "' could not be loaded, using system default instead '" << familyName.c_str() << ". et GW_DEBUG=1 to display available fonts.\n";
                char *val = getenv("GW_DEBUG");
                if (val) {
                    printAvailableFonts();
                }
            } else {
                SkString familyName;
                face->getFamilyName(&familyName);
                if (familyName.c_str() != fontStr) {
                    std::cerr << "\nWarning: font '" << fontStr << "' could not be loaded, swapped for '" << familyName.c_str() << " instead. Set GW_DEBUG=1 to display available fonts.\n";
                    char *val = getenv("GW_DEBUG");
                    if (val) {
                        printAvailableFonts();
                    }
                }
            }
        }

        SkScalar ts = size;
        overlay.setSize(ts);
        overlay.setTypeface(face);
        fontTypefaceSize = size;
    }

#endif


    void Fonts::setOverlayHeight(float yScale) {
        SkRect bounds[1];
        SkPaint paint1;
        const SkPaint* pnt = &paint1;
        overlay.setSize(fontTypefaceSize * yScale);
        overlay.getBounds(glyphs, 1, bounds, pnt);
        overlayCharWidth = bounds[0].width();
        overlayHeight = bounds[0].height();
        overlayWidth = overlay.measureText("Gy", 1, SkTextEncoding::kUTF8);
        SkScalar w = overlay.measureText("Gy", 1, SkTextEncoding::kUTF8);
        for (int i = 0; i < 10; ++i) {
            textWidths[i] = (float)w * (i + 1);
        }
    }

    float Fonts::measureTextWidth(const std::string& text) const {
        SkScalar width = this->overlay.measureText(text.c_str(), text.size(), SkTextEncoding::kUTF8);
        return (float)width;
    }

    #define next_t std::getline(iss, token, '\t')

    void readIdeogramFile(std::string file_path, std::unordered_map<std::string, std::vector<Ideo::Band>> &ideogram,
                          Themes::BaseTheme &theme) {
        std::shared_ptr<std::istream> fpu;
#if !defined(__EMSCRIPTEN__)

        if (Utils::startsWith(file_path, "http") || Utils::startsWith(file_path, "ftp")) {
            std::string content = Utils::fetchOnlineFileContent(file_path);
            fpu = std::make_shared<std::istringstream>(content);
        } else {
            auto file_stream = std::make_shared<std::ifstream>(file_path);
            if (!file_stream->is_open()) {
                std::cerr << "Error: opening ideogram file " << file_path << std::endl;
                throw std::runtime_error("Error opening file");
            }
            fpu = file_stream;
        }
#else
        fpu = std::make_shared<std::ifstream>();
            fpu->open(p);
            if (!fpu->is_open()) {
                std::cerr << "Error: opening track file " << file_path << std::endl;
                throw std::exception();
            }
#endif

        std::unordered_map<std::string, Ideo::Band> custom;
        std::string line, token, chrom, name, property;
        int acen = theme.fcT.getColor();
        int gvar = theme.fcC.getColor();
        while (std::getline(*fpu, line)) {
            std::istringstream iss(line);
            if (line[0] == '#') {
                if (Utils::startsWith(line, "#gw ")) {
                    std::vector<std::string> parts = Utils::split( line, ' ');
                    if (parts.size() == 3) {
                        std::vector<std::string> c = Utils::split(parts.back(), ',');
                        if (c.size() == 4) {
                            custom[parts[1]] = {0, 0, std::stoi(c[0]), std::stoi(c[1]), std::stoi(c[2]), std::stoi(c[3]), {}, parts[1]};
                        }
                    }
                }
                continue;
            }

            next_t;
            chrom = token;
            next_t;
            int start = std::stoi(token);
            next_t;
            int end = std::stoi(token);
            next_t;
            name = token;
            next_t;
            property = token;
            if (property == "gneg") {
                ideogram[chrom].emplace_back() = {start, end, 255, 255, 255, 255, {}, name};
            } else if (property == "gpos25") {
                ideogram[chrom].emplace_back() = {start, end, 255, 235, 235, 235, {}, name};
            } else if (property == "gpos50") {
                ideogram[chrom].emplace_back() = {start, end, 255, 185, 185, 185, {}, name};
            } else if (property == "gpos75") {
                ideogram[chrom].emplace_back() = {start, end, 255, 110, 110, 110, {}, name};
            } else if (property == "gpos100") {
                ideogram[chrom].emplace_back() = {start, end, 255, 60, 60, 60, {}, name};
            } else if (property == "acen") {
                ideogram[chrom].emplace_back() = {start, end, 255, SkColorGetR(acen), SkColorGetG(acen), SkColorGetB(acen), {}, name};
            } else if (property == "gvar") {
                ideogram[chrom].emplace_back() = {start, end, 255, SkColorGetR(gvar), SkColorGetG(gvar), SkColorGetB(gvar), {}, name};
            } else if (custom.find(name) != custom.end()) {
                Ideo::Band cust = custom[name];
                cust.start = start;
                cust.end = end;
                ideogram[chrom].emplace_back() = cust;
            } else if (!property.empty()) {  // try custom color scheme
                std::vector<std::string> a = Utils::split(property, ',');
                if (a.size() == 4) {
                    try {
                        ideogram[chrom].emplace_back() = {start, end, std::stoi(a[0]), std::stoi(a[1]), std::stoi(a[2]),
                                                          std::stoi(a[3]), {}, name};
                    } catch (...) {

                    }
                } else {
                    ideogram[chrom].emplace_back() = {start, end, 255, 85, 171, 159, {}, name};
                }
            } else {
                ideogram[chrom].emplace_back() = {start, end, 255, 85, 171, 159, {}, name};
            }
        }
        for (auto& kv : ideogram) {
            for (auto& i : kv.second) {
                i.paint.setARGB(i.alpha, i.red, i.green, i.blue);
            }
        }
    }


    void readIdeogramData(const unsigned char *data, size_t size, std::unordered_map<std::string, std::vector<Ideo::Band>> &ideogram,
                          Themes::BaseTheme &theme, bool strip_chr) {
        if (size == 0) {
            return;
        }
        std::istringstream fpu(std::string(reinterpret_cast<const char*>(data), size));

        std::unordered_map<std::string, Ideo::Band> custom;
        std::string line, token, chrom, name, property;
        int acen = theme.fcT.getColor();
        int gvar = theme.fcC.getColor();
        while (std::getline(fpu, line)) {
            std::istringstream iss(line);
            if (line[0] == '#') {
                if (Utils::startsWith(line, "#gw ")) {
                    std::vector<std::string> parts = Utils::split(line, ' ');
                    if (parts.size() == 3) {
                        std::vector<std::string> c = Utils::split(parts.back(), ',');
                        if (c.size() == 4) {
                            custom[parts[1]] = {0, 0, std::stoi(c[0]), std::stoi(c[1]), std::stoi(c[2]), std::stoi(c[3]), {}, parts[1]};
                        }
                    }
                }
                continue;
            }

            next_t;
            chrom = token;
            if (strip_chr) {
                chrom.erase(chrom.begin(), chrom.begin() + 3);
            }
            next_t;
            int start = std::stoi(token);
            next_t;
            int end = std::stoi(token);
            next_t;
            name = token;
            next_t;
            property = token;
            if (property == "gneg") {
                ideogram[chrom].emplace_back() = {start, end, 255, 255, 255, 255, {}, name};
            } else if (property == "gpos25") {
                ideogram[chrom].emplace_back() = {start, end, 255, 235, 235, 235, {}, name};
            } else if (property == "gpos50") {
                ideogram[chrom].emplace_back() = {start, end, 255, 185, 185, 185, {}, name};
            } else if (property == "gpos75") {
                ideogram[chrom].emplace_back() = {start, end, 255, 110, 110, 110, {}, name};
            } else if (property == "gpos100") {
                ideogram[chrom].emplace_back() = {start, end, 255, 60, 60, 60, {}, name};
            } else if (property == "acen") {
                ideogram[chrom].emplace_back() = {start, end, 255, SkColorGetR(acen), SkColorGetG(acen), SkColorGetB(acen), {}, name};
            } else if (property == "gvar") {
                ideogram[chrom].emplace_back() = {start, end, 255, SkColorGetR(gvar), SkColorGetG(gvar), SkColorGetB(gvar), {}, name};
            } else if (custom.find(name) != custom.end()) {
                Ideo::Band cust = custom[name];
                cust.start = start;
                cust.end = end;
                ideogram[chrom].emplace_back() = cust;
            } else if (!property.empty()) {  // try custom color scheme
                std::vector<std::string> a = Utils::split(property, ',');
                if (a.size() == 4) {
                    try {
                        ideogram[chrom].emplace_back() = {start, end, std::stoi(a[0]), std::stoi(a[1]), std::stoi(a[2]),
                                                          std::stoi(a[3]), {}, name};
                    } catch (...) {

                    }
                } else {
                    ideogram[chrom].emplace_back() = {start, end, 255, 85, 171, 159, {}, name};
                }
            } else {
                ideogram[chrom].emplace_back() = {start, end, 255, 85, 171, 159, {}, name};
            }
        }
        for (auto& kv : ideogram) {
            for (auto& i : kv.second) {
                i.paint.setARGB(i.alpha, i.red, i.green, i.blue);
            }
        }
    }

}
