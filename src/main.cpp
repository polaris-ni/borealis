#include "aurora/aurora.h"
#include "aurora/window/native_surfaces.h"

int main() {
    au::Scene scene{au::Node{au::Column{}}};

    au::WindowOptions opts;
    opts.size = au::Size{.width = 960.0F, .height = 640.0F};
    opts.title = "Borealis";

    auto window = au::create_native_window(opts);
    au::Application app{std::move(scene), window ? std::move(window.value()) : nullptr, opts};
    app.run();
    return 0;
}
