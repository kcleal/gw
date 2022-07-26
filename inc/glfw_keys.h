//
// Created by Kez Cleal on 26/07/2022.
//

#ifndef GW_GLFW_KEYS_H
#define GW_GLFW_KEYS_H

#endif //GW_GLFW_KEYS_H

#include <GLFW/glfw3.h>
#include "robin_hood.h"

void getKeyTable(robin_hood::unordered_map<std::string, int>& key_table) {
    key_table["SPACE"] = GLFW_KEY_SPACE;
    key_table["APOSTROPHE"] = GLFW_KEY_APOSTROPHE;
    key_table["COMMA"] = GLFW_KEY_COMMA;
    key_table["MINUS"] = GLFW_KEY_MINUS;
    key_table["PERIOD"] = GLFW_KEY_PERIOD;
    key_table["SLASH"] = GLFW_KEY_SLASH;
    key_table["0"] = GLFW_KEY_0;
    key_table["1"] = GLFW_KEY_1;
    key_table["2"] = GLFW_KEY_2;
    key_table["3"] = GLFW_KEY_3;
    key_table["4"] = GLFW_KEY_4;
    key_table["5"] = GLFW_KEY_5;
    key_table["6"] = GLFW_KEY_6;
    key_table["7"] = GLFW_KEY_7;
    key_table["8"] = GLFW_KEY_8;
    key_table["9"] = GLFW_KEY_9;
    key_table["SEMICOLON"] = GLFW_KEY_SEMICOLON;
    key_table["EQUAL"] = GLFW_KEY_EQUAL;
    key_table["A"] = GLFW_KEY_A;
    key_table["B"] = GLFW_KEY_B;
    key_table["C"] = GLFW_KEY_C;
    key_table["D"] = GLFW_KEY_D;
    key_table["E"] = GLFW_KEY_E;
    key_table["F"] = GLFW_KEY_F;
    key_table["G"] = GLFW_KEY_G;
    key_table["H"] = GLFW_KEY_H;
    key_table["I"] = GLFW_KEY_I;
    key_table["J"] = GLFW_KEY_J;
    key_table["K"] = GLFW_KEY_K;
    key_table["L"] = GLFW_KEY_L;
    key_table["M"] = GLFW_KEY_M;
    key_table["N"] = GLFW_KEY_N;
    key_table["O"] = GLFW_KEY_O;
    key_table["P"] = GLFW_KEY_P;
    key_table["Q"] = GLFW_KEY_Q;
    key_table["R"] = GLFW_KEY_R;
    key_table["S"] = GLFW_KEY_S;
    key_table["T"] = GLFW_KEY_T;
    key_table["U"] = GLFW_KEY_U;
    key_table["V"] = GLFW_KEY_V;
    key_table["W"] = GLFW_KEY_W;
    key_table["X"] = GLFW_KEY_X;
    key_table["Y"] = GLFW_KEY_Y;
    key_table["Z"] = GLFW_KEY_Z;
    key_table["LEFT_BRACKET"] = GLFW_KEY_LEFT_BRACKET;
    key_table["BACKSLASH"] = GLFW_KEY_BACKSLASH;
    key_table["RIGHT_BRACKET"] = GLFW_KEY_RIGHT_BRACKET;
    key_table["GRAVE_ACCENT"] = GLFW_KEY_GRAVE_ACCENT;
    key_table["WORLD_1"] = GLFW_KEY_WORLD_1;
    key_table["WORLD_2"] = GLFW_KEY_WORLD_2;
    key_table["ESCAPE"] = GLFW_KEY_ESCAPE;
    key_table["ENTER"] = GLFW_KEY_ENTER;
    key_table["TAB"] = GLFW_KEY_TAB;
    key_table["BACKSPACE"] = GLFW_KEY_BACKSPACE;
    key_table["INSERT"] = GLFW_KEY_INSERT;
    key_table["DELETE"] = GLFW_KEY_DELETE;
    key_table["RIGHT"] = GLFW_KEY_RIGHT;
    key_table["LEFT"] = GLFW_KEY_LEFT;
    key_table["DOWN"] = GLFW_KEY_DOWN;
    key_table["UP"] = GLFW_KEY_UP;
    key_table["PAGE_UP"] = GLFW_KEY_PAGE_UP;
    key_table["PAGE_DOWN"] = GLFW_KEY_PAGE_DOWN;
    key_table["HOME"] = GLFW_KEY_HOME;
    key_table["END"] = GLFW_KEY_END;
    key_table["CAPS_LOCK"] = GLFW_KEY_CAPS_LOCK;
    key_table["SCROLL_LOCK"] = GLFW_KEY_SCROLL_LOCK;
    key_table["NUM_LOCK"] = GLFW_KEY_NUM_LOCK;
    key_table["PRINT_SCREEN"] = GLFW_KEY_PRINT_SCREEN;
    key_table["PAUSE"] = GLFW_KEY_PAUSE;
    key_table["F1"] = GLFW_KEY_F1;
    key_table["F2"] = GLFW_KEY_F2;
    key_table["F3"] = GLFW_KEY_F3;
    key_table["F4"] = GLFW_KEY_F4;
    key_table["F5"] = GLFW_KEY_F5;
    key_table["F6"] = GLFW_KEY_F6;
    key_table["F7"] = GLFW_KEY_F7;
    key_table["F8"] = GLFW_KEY_F8;
    key_table["F9"] = GLFW_KEY_F9;
    key_table["F10"] = GLFW_KEY_F10;
    key_table["F11"] = GLFW_KEY_F11;
    key_table["F12"] = GLFW_KEY_F12;
    key_table["F13"] = GLFW_KEY_F13;
    key_table["F14"] = GLFW_KEY_F14;
    key_table["F15"] = GLFW_KEY_F15;
    key_table["F16"] = GLFW_KEY_F16;
    key_table["F17"] = GLFW_KEY_F17;
    key_table["F18"] = GLFW_KEY_F18;
    key_table["F19"] = GLFW_KEY_F19;
    key_table["F20"] = GLFW_KEY_F20;
    key_table["F21"] = GLFW_KEY_F21;
    key_table["F22"] = GLFW_KEY_F22;
    key_table["F23"] = GLFW_KEY_F23;
    key_table["F24"] = GLFW_KEY_F24;
    key_table["F25"] = GLFW_KEY_F25;
    key_table["KP_0"] = GLFW_KEY_KP_0;
    key_table["KP_1"] = GLFW_KEY_KP_1;
    key_table["KP_2"] = GLFW_KEY_KP_2;
    key_table["KP_3"] = GLFW_KEY_KP_3;
    key_table["KP_4"] = GLFW_KEY_KP_4;
    key_table["KP_5"] = GLFW_KEY_KP_5;
    key_table["KP_6"] = GLFW_KEY_KP_6;
    key_table["KP_7"] = GLFW_KEY_KP_7;
    key_table["KP_8"] = GLFW_KEY_KP_8;
    key_table["KP_9"] = GLFW_KEY_KP_9;
    key_table["KP_DECIMAL"] = GLFW_KEY_KP_DECIMAL;
    key_table["KP_DIVIDE"] = GLFW_KEY_KP_DIVIDE;
    key_table["KP_MULTIPLY"] = GLFW_KEY_KP_MULTIPLY;
    key_table["KP_SUBTRACT"] = GLFW_KEY_KP_SUBTRACT;
    key_table["KP_ADD"] = GLFW_KEY_KP_ADD;
    key_table["KP_ENTER"] = GLFW_KEY_KP_ENTER;
    key_table["KP_EQUAL"] = GLFW_KEY_KP_EQUAL;
    key_table["LEFT_SHIFT"] = GLFW_KEY_LEFT_SHIFT;
    key_table["LEFT_CONTROL"] = GLFW_KEY_LEFT_CONTROL;
    key_table["LEFT_ALT"] = GLFW_KEY_LEFT_ALT;
    key_table["LEFT_SUPER"] = GLFW_KEY_LEFT_SUPER;
    key_table["RIGHT_SHIFT"] = GLFW_KEY_RIGHT_SHIFT;
    key_table["RIGHT_CONTROL"] = GLFW_KEY_RIGHT_CONTROL;
    key_table["RIGHT_ALT"] = GLFW_KEY_RIGHT_ALT;
    key_table["RIGHT_SUPER"] = GLFW_KEY_RIGHT_SUPER;
    key_table["MENU"] = GLFW_KEY_MENU;
};