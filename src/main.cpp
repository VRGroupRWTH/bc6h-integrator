#include "application.hpp"

int main(int argc, char* argv[]) {
    Application app(argc, argv);

    if (!app.setup()) {
        return lava::error::not_ready;
    }
    app.load_dataset("/home/so225523/Data/BC6/Data/tg/tangaroa_bc6_highest.ktx");

    return app.run();
}
