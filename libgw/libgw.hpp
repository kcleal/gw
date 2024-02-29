//
// Created by Kez Cleal on 28/02/2024.
//

#pragma once

#include "themes.h"
#include "export_definitions.h"


namespace Gw {

    enum GwPaint {
        bgPaint, fcNormal, fcDel, fcDup, fcInvF, fcInvR, fcTra, fcIns, fcSoftClip,
        fcA, fcT, fcC, fcG, fcN, fcCoverage, fcTrack, fcNormal0, fcDel0, fcDup0, fcInvF0, fcInvR0, fcTra0,
        fcSoftClip0, fcBigWig, mate_fc, mate_fc0, ecMateUnmapped, ecSplit, ecSelected,
        lcJoins, lcCoverage, lcLightJoins, insF, insS, lcLabel, lcBright, tcDel, tcIns, tcLabels, tcBackground,
        marker_paint
    };

    class EXPORT BaseTheme {
    public:
        BaseTheme();
        ~BaseTheme() = default;

        void setAlphas();
        void setPaintARGB(int paint_enum, int alpha, int red, int green, int blue);

    };
}

