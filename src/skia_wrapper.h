#include "GLFW/glfw3.h"
#include <OpenGL/gl.h>

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




#include "robin_hood.h"

#include <stdio.h>
#include <stdlib.h>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <chrono>
#include <vector>
#include <utility>
#include <string>




//#define GL_FRAMEBUFFER_SRGB 0x8DB9
//#define GL_SRGB8_ALPHA8 0x8C43

struct shape {
	std::vector<GLfloat> vertices; //Here we use a vector (similar to C# lists) instead of an arry to store our vertices as it is more flexible.
	GLuint VAO;
	GLuint VBO;
};


SkCanvas* canvas = nullptr;
SkSurface* sSurface = nullptr;

sk_sp<SkSurface> rasterSurface = nullptr;
GrDirectContext* sContext = nullptr;


// draw using gpu backend
class SkiaSurface {
    public:
        SkiaSurface() {};
        ~SkiaSurface() {};

//        SkCanvas* canvas = nullptr;
//        SkSurface* sSurface = nullptr;

//        sk_sp<SkSurface> rasterSurface = nullptr;
//        GrDirectContext* sContext = nullptr;


        int width, height, surface_kind;
        float fontSize, fontHeight;
        SkRect rect = SkRect::MakeEmpty();
        SkPaint paint, delfont_paint, insfont_paint, join_paint, insfc_paint;
        SkColor background_paint;
        SkPath path = SkPath();

        char fn[6] = "Arial";
        sk_sp<SkTypeface> face = SkTypeface::MakeFromName(fn, SkFontStyle::Normal());
        SkFont fonty = SkFont();
        float text_widths[10];

        robin_hood::unordered_map<int, SkPaint> paint_table;
        robin_hood::unordered_map<char, SkPaint> paint_table_mm;

        void set_font(float polygon_height, float y_scaling) {

            SkScalar ts = 14;
            fonty.setSize(ts);
            char fn[6] = "Arial";
            sk_sp<SkTypeface> face = SkTypeface::MakeFromName(fn, SkFontStyle::Normal());
            fonty.setTypeface(face);

            const SkGlyphID glyphs[1] = {100};
            SkRect bounds[1];
            const SkPaint* pnt = &delfont_paint;
            SkScalar height;
            int font_size = 50;
            while (font_size>0) {
                fonty.setSize(font_size);
                fonty.getBounds(glyphs, 1, bounds, pnt);
                height = bounds[0].height();
                if (height < polygon_height * y_scaling) { break; }
                --font_size;
            }
            fontSize = font_size;
            fontHeight = height;
        }

        int get_fontSize() { return fontSize; }
        float get_fontHeight() { return fontHeight; }

        void set_text_width(int i, float w) { text_widths[i] = w; }
        float get_text_width(int n) { return text_widths[n]; }

        void add_paint(int key, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha, bool anti_alias) {
            SkPaint paint = SkPaint();
            if (anti_alias) { paint.setAntiAlias(true); }
            paint.setARGB(alpha, r, g, b);
            paint_table[key] = paint;
        }

        void add_paint_kFill_style(int key, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
            SkPaint paint = SkPaint();
            paint.setARGB(alpha, r, g, b);
            paint.setStyle(SkPaint::kFill_Style);
            paint_table[key] = paint;
        }

        void add_paint_kStroke_style(int key, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha, float lw) {
            SkPaint paint = SkPaint();
            paint.setARGB(alpha, r, g, b);
            paint.setStyle(SkPaint::kStroke_Style);
            paint.setStrokeWidth(lw);
            paint_table[key] = paint;
        }

         void add_paint_mismatch(char key, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
            SkPaint paint = SkPaint();
            paint.setARGB(alpha, r, g, b);
            paint_table_mm[key] = paint;
        }

        void set_background_paint(int r, int g, int b) {
            background_paint = SkColorSetARGB(255, r, g, b);
        }

        void set_delfont_paint(int r, int g, int b) {
            delfont_paint.setARGB(255, r, g, b);
            delfont_paint.setAntiAlias(true);
        }

        void set_insfont_paint(int r, int g, int b) {
            insfont_paint.setARGB(255, r, g, b);
            insfont_paint.setAntiAlias(true);
        }

        void set_join_paint(int r, int g, int b) {
            join_paint.setStyle(SkPaint::kStroke_Style);
            join_paint.setStrokeWidth(2); //1.5);
            join_paint.setARGB(255, r, g, b);
        }

        int init_gpu(int w, int h) {

//            glViewport(0, 0, w, h);
//
//            //Initalizing the first entity - a simple triangle from the last example
//            shape triangle;
//            triangle.vertices = {
//                // POSITION
//                -1.0f, -0.5f, 0.0f,
//                0.0f, -0.5f, 0.0f,
//                -0.5f, 0.5f, 0.0f,
//            };
//
//            //These steps are pretty standard for
//            //rendering objects as described in the last example
//            glGenVertexArraysAPPLE(1, &triangle.VAO);																					//Generating VAO
//            glGenBuffers(1, &triangle.VBO);																							//Generating VBO
//            glBindVertexArrayAPPLE(triangle.VAO);																						//Binding the VAO so we can manipulate it
//            glBindBuffer(GL_ARRAY_BUFFER, triangle.VBO);																		    //Binding the VBO so we can manipulate it
//
//            //Since we are using a vector to store our vertices, some of the parameters have to be changed
//            glBufferData(GL_ARRAY_BUFFER,
//                sizeof(GLfloat)*triangle.vertices.size(), //The data size is the size of one GLfloat multiplied by the number of vertices
//                &triangle.vertices[0],					  //We want to start at the first index of the vector, so we point to it
//                GL_STATIC_DRAW);
//
//            //Defining our vertex data
//            glEnableVertexAttribArray(0);
//            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0); //No need to make changes here as our data type (GLfloat) is the same. In later examples we will be changing it.
//
//            //Unbinding both our OpenGL objects
//            glBindBuffer(0, triangle.VBO);
//            glBindVertexArrayAPPLE(0);
//
//
//            //Initalizing the second entity, a simple square
//            //This is done by drawing 2 sets of triangles
//            shape square;
//
//            //As you can see we repeat vertices
//            //In future examples, index buffers will be introduced
//            //Index buffers make sure we don't load repeated vertices
//            square.vertices = {
//                // Triangle 1
//                0.0f, -0.5f, 0.0f,
//                1.0f, -0.5f, 0.0f,
//                0.0f, 0.5f, 0.0f,
//                //Triangle 2
//                0.0f, 0.5f, 0.0f,
//                1.0f, -0.5f, 0.0f,
//                1.0f, 0.5f, 0.0f
//            };
//
//            //Same as the above code
//            glGenVertexArraysAPPLE(1, &square.VAO);
//            glGenBuffers(1, &square.VBO);
//            glBindVertexArrayAPPLE(square.VAO);
//            glBindBuffer(GL_ARRAY_BUFFER, square.VBO);
//            glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat)*square.vertices.size(), &square.vertices[0], GL_STATIC_DRAW);;
//            glEnableVertexAttribArray(0);
//            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0);
//            glBindBuffer(0, square.VBO);
//            glBindVertexArrayAPPLE(0);
//
//            glBindVertexArrayAPPLE(triangle.VAO);
//            glDrawArrays(GL_TRIANGLES, 0, 3); // 3 vertices in the triangle
//            glBindVertexArrayAPPLE(0);	//Unbind after we're finished drawing this entity.

            //Drawing our square by binding it's VAO
//            glBindVertexArrayAPPLE(square.VAO);
//            glDrawArrays(GL_TRIANGLES, 0, 6); // 6 vertices in triangle (2 sets of 3)
//            glBindVertexArrayAPPLE(0); //Unbind after we're finished drawing this entity.

//            return 2;
//            std::printf("%s\n%s\n",
//            glGetString(GL_RENDERER),  // e.g. AMD Radeon R9 M290X OpenGL Engine
//            glGetString(GL_VERSION)    // e.g. 2.1 ATI-4.6.21
//            );

//            std::cout << "c++ glfw " << &glfwGetVersionString << std::endl;


//            if (!glfwInit()) {
//                exit(EXIT_FAILURE);
//            }
//
//            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
//            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
////            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
////            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
//            //(uncomment to enable correct color spaces) glfwWindowHint(GLFW_SRGB_CAPABLE, GL_TRUE);
//            glfwWindowHint(GLFW_STENCIL_BITS, 0);
//            //glfwWindowHint(GLFW_ALPHA_BITS, 0);
//            glfwWindowHint(GLFW_DEPTH_BITS, 0);
//
//            window = glfwCreateWindow(kWidth, kHeight, "Simple example", NULL, NULL);
//            if (!window) {
//                glfwTerminate();
//                exit(EXIT_FAILURE);
//            }

//            GLFWwindow * window = glfwGetCurrentContext();
//            glfwMakeContextCurrent(window);


            std::cout << w << " " << h << std::endl;

//            std::cout << glGetFramebufferAttachmentParameteriv(0, GL_COLOR_ATTACHMENT0, ) << std::endl;
            width = w;
            height = h;

            auto interface = GrGLMakeNativeInterface();
	        sContext = GrDirectContext::MakeGL(interface).release();
//	        sContext = new GrDirectContext::MakeGL(GrGLMakeNativeInterface()).release();
	        if (!sContext) { return -1; }
            std::cout << "c2  " << width << " " << height << std::endl;


	        GrGLFramebufferInfo framebufferInfo;
            framebufferInfo.fFBOID = 0; // assume default framebuffer
            // We are always using OpenGL and we use RGBA8 internal format for both RGBA and BGRA configs in OpenGL.
            framebufferInfo.fFormat = GL_RGBA8; // GL_FRAMEBUFFER_SRGB; //GL_RGBA8;  // GL_SRGB8_ALPHA8;  //

            SkColorType colorType = kRGBA_8888_SkColorType;

//             this crashes ->
            GrBackendRenderTarget backendRenderTarget(width, height,
                0, // sample count
                0, // stencil bits
                framebufferInfo);

            std::cout << "c3 " << std::endl;

            std::cout << "c3 a " << std::endl;

            // https://gist.github.com/ad8e/dd150b775ae6aa4d5cf1a092e4713add
            sSurface = SkSurface::MakeFromBackendRenderTarget(sContext, backendRenderTarget,
                                                              kBottomLeft_GrSurfaceOrigin, colorType,
                                                              SkColorSpace::MakeSRGB(), nullptr).release();  // SkColorSpace::MakeSRGB()
            std::cout << "c4 " << std::endl;

            if (!sSurface) { return -1; }
//
            canvas = sSurface->getCanvas();

            was_init = 1;
            surface_kind = 1;

            std::cout << "c5 " << std::endl;


//            while (!glfwWindowShouldClose(window) && !(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)) {
//                glfwWaitEvents();
//                SkPaint paint;
//                paint.setColor(SK_ColorWHITE);
//                canvas->drawPaint(paint);
//                paint.setColor(SK_ColorBLUE);
//                canvas->drawRect({100, 200, 300, 500}, paint);
//                sContext->flush();
//
//                glfwSwapBuffers(window);
//
//            }
//
//            std::cout << "got here " << std::endl;
//
//            cleanup_skia();
//            std::cout << "got here 2" << std::endl;
//            glfwDestroyWindow(window);
//            std::cout << "got here 3" << std::endl;
////            glfwTerminate();
//            std::cout << "got here 4" << std::endl;

            return 1;

        }

        int init_raster(int w, int h) {
            SkImageInfo img_info;
            img_info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
            rasterSurface = SkSurface::MakeRaster(img_info);
            if (!rasterSurface) { return -1; }
            canvas = rasterSurface->getCanvas();
            paint.setColor(SK_ColorBLUE);
            was_init = 1;
            surface_kind = 2;
            return 1;
        }

        int test_init() { return was_init; }

        void drawBackground() {
            canvas->drawColor(background_paint, SkBlendMode::kSrcOver);
        }

        void drawRectXYWH_colour(float x, float y, float w, float h, int color_index) {
            rect.setXYWH(x, y, w, h);
            canvas->drawRect(rect, paint_table[color_index]);
        }

        void drawPoly_colour(SkPoint point_arr[], unsigned int arr_len, int color_index) {
            path.reset();
            path.addPoly(point_arr, arr_len, true);
//            SkPath path = SkPath().addPoly(point_arr, arr_len, true);
            canvas->drawPath(path, paint_table[color_index]);
        }

        void drawJoin(float x1, float x2, float y) {
            SkPath j = SkPath();
            j.moveTo(x1, y);
            j.lineTo(x2, y);
            canvas->drawPath(j, join_paint);
        }

        void drawIns(float y0, float start, float x_scaling, float y_scaling, float x_offset,
                        float polygon_height, float y_offset, float txt_width, int color_index_f,
                        int color_index_s) {

            float x = start * x_scaling + x_offset;
            float y = y0 * y_scaling;
            float ph = polygon_height * y_scaling;
            float overhang = txt_width * 0.125;

            rect.setXYWH(x - (txt_width / 2), y + y_offset, txt_width, ph);
            canvas->drawRect(rect, paint_table[color_index_f]);

            SkPath j = SkPath();
            j.moveTo(x - (txt_width / 2) - overhang, y_offset + y + ph * 0.1);
            j.lineTo(x + (txt_width / 2) + overhang, y_offset + y + ph * 0.1);
            j.moveTo(x - (txt_width / 2) - overhang, y_offset + y + ph * 0.9);
            j.lineTo(x + (txt_width / 2) + overhang, y_offset + y + ph * 0.9);
            j.moveTo(x, y_offset + y);
            j.lineTo(x, y_offset + y + ph);
            canvas->drawPath(j, paint_table[color_index_s]);
        }

        void drawText(char * txt, float x, float y, bool is_ins) {
            SkPaint p;
            if (is_ins) {
                p = insfont_paint;
            } else {
                p = delfont_paint;
            }
            sk_sp<SkTextBlob> blob1 = SkTextBlob::MakeFromString(txt, fonty);
            canvas->drawTextBlob(blob1.get(), x, y, p);
        }

        void flushAndSubmit() { sSurface->flushAndSubmit(); }

        int save_png(const char *path) {
            sk_sp<SkImage> img(rasterSurface->makeImageSnapshot());
            if (!img) { return -1; }
            sk_sp<SkData> png(img->encodeToData());
            if (!png) { return -1; }
            SkFILEWStream out(path);
            (void)out.write(png->data(), png->size());
            return 0;
        }

        void cleanup_skia() {
            delete sSurface;
            delete sContext;
        }

    private:

        int was_init = 0;

};