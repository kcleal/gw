//
// Created by Kez Cleal on 25/07/2022.
//
#include "argparse.h"

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program("program_name");

    program.add_argument("square")
            .help("display the square of a given integer")
            .scan<'i', int>();

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

    return 0;
}