//
// Created by Kez Cleal on 26/07/2022.
//
#pragma once

#include <cstring>
#include <string>
#include <GLFW/glfw3.h>
#include "ankerl_unordered_dense.h"

namespace Keys {

    void getKeyTable(ankerl::unordered_dense::map<std::string, int>& kt);

}
