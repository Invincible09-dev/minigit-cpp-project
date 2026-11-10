#include "minigit/command.hpp"
#include <iostream>

int main(int argc, char **argv) {
    return minigit::Application{}.run({argv + 1, argv + argc}, std::filesystem::current_path(),
                                      std::cout, std::cerr);
}
