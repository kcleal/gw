//
// Created by Kez Cleal on 25/07/2022.
//
#include <filesystem>
#include <GLFW/glfw3.h>
#include <iostream>
#include <pwd.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>

#include "argparse.h"
#include "glfw_keys.h"
#include "inicpp.h"
#include "robin_hood.h"
#include "utils.h"


bool is_file_exist( std::string FileName ) {
    const std::filesystem::path p = FileName ;
    return ( std::filesystem::exists(p) );
}

class IniOptions {
    public:
        IniOptions() { getIni(); };
        ~IniOptions() {};

        std::string theme = "igv";
        std::string dimensions = "2048x1024";
        std::string fmt = "png";
        std::string link = "None";
        std::string number = "3x3";
        std::string labels = "PASS,FAIL";

        int indel_length = 10;
        int ylim = 50;
        int split_view_size = 10000;
        int threads = 3;
        int pad = 500;
        bool log2_cov = false;
        float scroll_speed = 0.15;
        float tab_track_height = 0.05;
        int scroll_right = GLFW_KEY_RIGHT;
        int scroll_left = GLFW_KEY_LEFT;
        int scroll_down = GLFW_KEY_PAGE_DOWN;
        int scroll_up = GLFW_KEY_PAGE_UP;
        int next_region_view = GLFW_KEY_SLASH;
        int zoom_out = GLFW_KEY_DOWN;
        int zoom_in = GLFW_KEY_UP;
        int cycle_link_mode = GLFW_KEY_L;
        int print_screen = GLFW_KEY_PRINT_SCREEN;
        int delete_labels = GLFW_KEY_DELETE;
        int enter_interactive_mode = GLFW_KEY_ENTER;

        robin_hood::unordered_map<std::string, std::string> references;
        robin_hood::unordered_map<std::string, std::string> tracks;


    void readIni(std::string path) {
        robin_hood::unordered_map<std::string, int> key_table;
        getKeyTable(key_table);

        std::cout << "Loading " << path << std::endl;
        ini::IniFile myIni;
        myIni.setMultiLineValues(true);
        myIni.load(path);
        theme = myIni["general"]["theme"].as<const char*>();
        dimensions = myIni["general"]["dimensions"].as<const char*>();
        fmt = myIni["general"]["fmt"].as<const char*>();
        link = myIni["general"]["link"].as<const char*>();
        number = myIni["general"]["number"].as<const char*>();
        labels = myIni["general"]["labels"].as<const char*>();
        indel_length = myIni["general"]["indel_length"].as<int>();
        ylim = myIni["general"]["ylim"].as<int>();
        split_view_size = myIni["general"]["split_view_size"].as<int>();
        threads = myIni["general"]["threads"].as<int>();
        pad = myIni["general"]["pad"].as<int>();
        log2_cov = myIni["general"]["log2_cov"].as<bool>();
        scroll_speed = myIni["general"]["scroll_speed"].as<float>();

        scroll_right = key_table[myIni["navigation"]["scroll_right"].as<const char*>()];
        scroll_left = key_table[myIni["navigation"]["scroll_left"].as<const char*>()];
        scroll_up = key_table[myIni["navigation"]["scroll_up"].as<const char*>()];
        scroll_down = key_table[myIni["navigation"]["scroll_down"].as<const char*>()];
        zoom_out = key_table[myIni["navigation"]["zoom_out"].as<const char*>()];
        zoom_in = key_table[myIni["navigation"]["zoom_in"].as<const char*>()];

        cycle_link_mode = key_table[myIni["interaction"]["cycle_link_mode"].as<const char*>()];
        print_screen = key_table[myIni["interaction"]["print_screen"].as<const char*>()];

        number = key_table[myIni["labelling"]["number"].as<const char*>()];
        labels = key_table[myIni["labelling"]["number"].as<const char*>()];
        delete_labels = key_table[myIni["labelling"]["number"].as<const char*>()];
        enter_interactive_mode = key_table[myIni["labelling"]["enter_interactive_mode"].as<const char*>()];
    }

    void getIni() {
        // get home directory
        struct passwd *pw = getpwuid(getuid());
        std::string home(pw->pw_dir);
        if (is_file_exist(home + "/.gw.ini")) {
            readIni(home + "/.gw.ini");
        } else if (is_file_exist(home + "/.config/.gw.ini")) {
            readIni(home + "/.config/.gw.ini");
        } else if (is_file_exist(MyPaths::getExecutableDir() + "/.gw.ini")) {
            readIni(MyPaths::getExecutableDir() + "/.gw.ini");
        }
    }
};


int main(int argc, char *argv[]) {
    argparse::ArgumentParser program("gw");
    program.add_argument("genome")
            .help("Reference genome to use in .fasta format with .fai index file");
    program.add_argument("--color")
            .default_value<std::vector<std::string>>({ "orange" })
            .append()
            .help("specify the cat's fur color");

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    auto input = program.get<int>("square");
    std::cout << (input * input) << std::endl;

    IniOptions iopts;

    std::cout << MyPaths::getExecutableDir() << std::endl;
    return 0;
};