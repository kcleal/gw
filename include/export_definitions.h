//
// Created by Kez Cleal on 23/02/2024.
//

#pragma once

#if defined(__GNUC__) && __GNUC__ >= 4
    #if BUILDING_LIBGW
        #define EXPORT __attribute__ ((visibility ("default")))
    #else
        #define EXPORT
    #endif
#endif
