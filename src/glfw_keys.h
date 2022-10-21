//
// Created by Kez Cleal on 26/07/2022.
//
#pragma once

#include <cstring>
#include <string>
#include <GLFW/glfw3.h>
//#include <unordered_map>
#include "../inc/robin_hood.h"

namespace Keys {

    void getKeyTable(robin_hood::unordered_map<std::string, int>& kt);

}
