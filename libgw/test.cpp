//
// Created by Kez Cleal on 29/02/2024.
//
#include <iostream>
#include "libgw.hpp"

int main(int argc, char *argv[]) {
    Gw::BaseTheme t;

    int p = Gw::GwPaint::fcNormal;
    t.setPaintARGB(p, 100, 100, 100, 100);
    return 0;
}