//
// Created by Kez Cleal on 23/02/2024.
//

#pragma once

#if defined _WIN32 || defined __CYGWIN__
    #ifdef BUILDING_LIBGW
        #define EXPORT __declspec(dllexport)
    #else
        #define EXPORT __declspec(dllimport)
    #endif
#else
    #if __GNUC__ >= 4
        #define EXPORT __attribute__ ((visibility ("default")))
    #else
        #define EXPORT
    #endif
#endif
