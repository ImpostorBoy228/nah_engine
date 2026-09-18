#include <iostream>

#include "heck.hpp"

int main() {
    Hell_Machina hell(900, 900);
    std::cout << "hello from huinya engine\n";

    if (RGFW_window* win = hell.get_window()) {
        RGFW_window_setExitKey(win, RGFW_escape);
    }

    while (!hell.shouldClose()) {
        hell.pollEvents();

        hell.clear();
        hell.push_rectangle({ 100.0f, 100.0f }, { 200.0f, 200.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, 0);

        if (!hell.draw()) {
            std::cerr << "failed to draw frame\n";
            break;
        }
    }

    std::cout << "bye\n";
    return 0;
}
