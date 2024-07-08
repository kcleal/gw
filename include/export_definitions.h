//
// Created by Kez Cleal on 23/02/2024.
//

#pragma once

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
    #if defined(__EMSCRIPTEN__)
        // Use EMSCRIPTEN_KEEPALIVE for functions only
        #if defined(BUILDING_LIBGW)
            #define EXPORT_FUNCTION EMSCRIPTEN_KEEPALIVE
            #define EXPORT
        #else
            #define EXPORT_FUNCTION
            #define EXPORT
        #endif
    #else
        // Non-Emscripten GCC or Clang
        #if defined(BUILDING_LIBGW)
            #define EXPORT_FUNCTION __attribute__((visibility("default")))
            #define EXPORT __attribute__((visibility("default")))
        #else
            #define EXPORT_FUNCTION
            #define EXPORT
        #endif
    #endif
#else
    #error "__GNUC__ not defined or version is less than 4"
#endif


