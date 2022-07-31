//
// Created by Kez Cleal on 25/07/2022.
//

#include <string>

#include "GLFW/glfw3.h"

#define SK_GL

#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/core/SkData.h"
#include "include/core/SkStream.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSize.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFont.h"
#include "include/core/SkTextBlob.h"

#include "../inc/robin_hood.h"


#ifndef GW_PARAMS_H
#define GW_PARAMS_H

#endif //GW_PARAMS_H

constexpr int NORMAL = 0;
constexpr int DEL = 200;
constexpr int INV_F = 400;
constexpr int INV_R = 600;
constexpr int DUP = 800;
constexpr int TRA = 1000;
constexpr int SC = 2000;
constexpr int INS_f = 5000;
constexpr int INS_s = 6000;


namespace Themes {

    class BaseTheme {
    public:
        BaseTheme() {};
        ~BaseTheme() = default;

        std::string name;

        SkPaint bg_paint;  //, fc_Normal;

    };

    class IgvTheme: public BaseTheme {
        IgvTheme() {

            name = "igv";

//            bg_paint.setColor(SkColorSetRGB(255, 255, 255));
//            fc_Normal.setColor(SkColorSetRGB(192, 192, 192));
        };
        ~IgvTheme() = default;

    };


    class Painter {
    public:
        Painter() {};
        ~Painter() = default;


    };

}