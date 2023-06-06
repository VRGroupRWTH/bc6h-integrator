#include "application.hpp"

int main(int argc, char* argv[]) {
    Application app(argc, argv);

    if (!app.setup()) {
        return lava::error::not_ready;
    }

    return app.run();
}
