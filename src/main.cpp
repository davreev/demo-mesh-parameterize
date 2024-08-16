#include <dr/app/app.hpp>

#include "scene.hpp"

dr::App::Desc DR_APP_MAIN(int /*argc*/, char* /*argv*/[])
{
    using namespace dr;

    App::set_scene(scene());

    App::Desc desc = App::desc();
    {
        desc.width = 1280;
        desc.height = 720;
        desc.sample_count = 4;
        desc.window_title = "Demo: Mesh Parameterize";
#if __EMSCRIPTEN__
        desc.html5_canvas_name = "mesh-parameterize";
#endif
    }

    return desc;
}
