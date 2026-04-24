#include <dr/app/app.hpp>

#include "scene.hpp"

int main(int /*argc*/, char* /*argv*/[])
{
    using namespace dr;

    App::run({
        .scene = scene(),
        .sokol_config{
            .app =
                [](sapp_desc& desc) {
                    desc.window_title = "Demo: Mesh Parameterize";
                    desc.html5.canvas_selector = "#mesh-parameterize";
                },
        },
    });

    return 0;
}
