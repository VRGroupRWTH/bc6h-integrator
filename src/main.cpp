#include "application.hpp"

int main(int argc, char* argv[]) {
    Application app(argc, argv);

    if (!app.setup()) {
        return lava::error::not_ready;
    }
    app.load_dataset("/home/so225523/Data/BC6/Data/cl/ctbl3d_bc6_highest.ktx");
    // app.load_dataset("/home/so225523/Data/ctbl3d.nc.raw");

    return app.run();
}
