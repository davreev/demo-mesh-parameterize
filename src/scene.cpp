#include "scene.hpp"

#include <sokol_gl.h>

#include <dr/math_ctors.hpp>
#include <dr/span.hpp>
#include <dr/transform.hpp>

#include <dr/app/debug_draw.hpp>
#include <dr/app/event_handlers.hpp>
#include <dr/app/gfx_utils.hpp>
#include <dr/app/orbit_camera.hpp>
#include <dr/app/shim/imgui.hpp>
#include <dr/app/task_queue.hpp>
#include <dr/app/thread_pool.hpp>

#include "assets.hpp"
#include "renderer.hpp"
#include "tasks.hpp"
#include "utils.hpp"

namespace dr
{
namespace
{

struct
{
    char const* name = "Mesh Parameterize";
    char const* author = "David Reeves";
    struct
    {
        u16 major{0};
        u16 minor{5};
        u16 patch{0};
    } version;
} constexpr scene_info{};

struct
{
    Renderer renderer;
    OrbitCamera camera;

    struct
    {
        MeshAsset const* asset{};
        DynamicArray<Vec2<f32>> tex_coords;
        DynamicArray<Vec2<i32>> boundary_edge_verts;
        Vec2<i32> ref_verts;
        Conformal3<f32> xform;
        Conformal3<f32> tex_xform;
        struct
        {
            Buffer<sizeof(i32)> index;
            Buffer<sizeof(f32[6])> vertex;
            Buffer<sizeof(f32[2])> tex_map;
        } gpu;
    } mesh;

    TaskQueue task_queue;
    struct
    {
        LoadMeshAsset load_mesh_asset;
        ExtractMeshBoundary extract_boundary;
        SolveTexCoords solve_tex_coords;
    } tasks;

    struct
    {
        Param<f32> tex_scale{0.01f, 0.001f, 0.1f};
        AssetHandle::Mesh mesh_handle;
        SolveTexCoords::Method solve_method{SolveTexCoords::Method_LeastSquaresConformal};
        bool flatten{};
    } params;
} state;

void mesh_set_asset(MeshAsset const* asset)
{
    assert(asset);

    auto& mesh = state.mesh;
    mesh.asset = asset;

    // Update GPU buffers
    set_mesh_indices(mesh.gpu.index, as_span(asset->faces.vertex_ids));
    set_mesh_vertices(
        mesh.gpu.vertex,
        as_span(asset->vertices.positions),
        as_span(asset->vertices.normals));

    // Fit to unit sphere in world space
    auto const& [cen, rad] = asset->bounds;
    f32 const scale = 1.0f / rad;
    mesh.xform = {
        .translation = -cen * scale,
        .scale = scale,
    };
}

void mesh_set_boundary(Span<Vec2<i32> const> const& boundary_edge_verts)
{
    auto& mesh = state.mesh;
    mesh.boundary_edge_verts.assign(begin(boundary_edge_verts), end(boundary_edge_verts));

    // Update ref verts
    {
        /*
            TODO(dr): Find these procedurally as part of the task.

            Could use most distant pair of boundary vertices? As a cheap approximation, could just
            project verts onto first eigenvector of covariance matrix.
        */

        static Vec2<i32> const table[]{
            {2729, 2730}, // Human head
            {1858, 1879}, // Pig head
            {9800, 6095}, // Camel head
            {7591, 6678}, // Ogre face
            {100, 164}, // VW Bug
        };
        static_assert(size(table) == AssetHandle::_Mesh_Count);
        mesh.ref_verts = table[state.params.mesh_handle];
    }
};

void mesh_set_tex_coords(Span<Vec2<f32> const> const& tex_coords)
{
    assert(tex_coords);

    auto& mesh = state.mesh;
    mesh.tex_coords.assign(begin(tex_coords), end(tex_coords));

    // Update GPU buffer
    set_mesh_vertices(mesh.gpu.tex_map, tex_coords);

    // Places flattened mesh on yz plane
    static Quat<f32> const rot{
        mat(vec(0.0f, 0.0f, 1.0f), vec(0.0f, 1.0f, 0.0f), vec(-1.0f, 0.0f, 0.0f))};
    mesh.tex_xform = {.rotation{.q = rot}};

    // Also fit to unit sphere if no texture map
    if (state.params.solve_method == SolveTexCoords::Method_None)
        mesh.tex_xform.scale = mesh.xform.scale;
}

bool mesh_has_tex_coords() { return state.mesh.gpu.tex_map.count > 0; }

void mesh_clear()
{
    auto& mesh = state.mesh;
    mesh.asset = nullptr;
    mesh.tex_coords.clear();
    mesh.boundary_edge_verts.clear();
    mesh.gpu.index.count = 0;
    mesh.gpu.vertex.count = 0;
    mesh.gpu.tex_map.count = 0;
}

void schedule_task(LoadMeshAsset& task)
{
    using Event = TaskQueue::PollEvent;

    state.task_queue.push(&task, nullptr, [](Event const& event) -> bool {
        auto const task = static_cast<LoadMeshAsset*>(event.task);
        switch (event.type)
        {
            case Event::BeforeSubmit:
            {
                task->input.handle = state.params.mesh_handle;
                return true;
            };
            case Event::AfterComplete:
            {
                mesh_set_asset(task->output.mesh);
                return true;
            };
            default:
            {
                return true;
            };
        }
    });
}

void schedule_task(ExtractMeshBoundary& task)
{
    using Event = TaskQueue::PollEvent;

    state.task_queue.push(&task, nullptr, [](Event const& event) -> bool {
        auto const task = static_cast<ExtractMeshBoundary*>(event.task);
        switch (event.type)
        {
            case Event::BeforeSubmit:
            {
                task->input.mesh = state.mesh.asset;
                return true;
            };
            case Event::AfterComplete:
            {
                mesh_set_boundary(task->output.boundary_edge_verts);
                return true;
            };
            default:
            {
                return true;
            };
        }
    });
}

void schedule_task(SolveTexCoords& task)
{
    using Event = TaskQueue::PollEvent;

    state.task_queue.push(&task, nullptr, [](Event const& event) -> bool {
        auto const task = static_cast<SolveTexCoords*>(event.task);
        switch (event.type)
        {
            case Event::BeforeSubmit:
            {
                task->input.mesh = state.mesh.asset;
                task->input.boundary_edge_verts = as_span(state.mesh.boundary_edge_verts);
                task->input.ref_verts = state.mesh.ref_verts;
                task->input.method = state.params.solve_method;
                return true;
            };
            case Event::AfterComplete:
            {
                mesh_set_tex_coords(task->output.tex_coords);
                return true;
            };
            default:
            {
                return true;
            };
        }
    });
}

void on_mesh_asset_change()
{
    mesh_clear();
    schedule_task(state.tasks.load_mesh_asset);
    state.task_queue.barrier();
    schedule_task(state.tasks.extract_boundary);
    state.task_queue.barrier();
    schedule_task(state.tasks.solve_tex_coords);
}

void draw_settings_tab()
{
    if (ImGui::BeginTabItem("Settings"))
    {
        ImGui::SeparatorText("Model");
        {
            ImGui::BeginDisabled(state.task_queue.size() > 0);

            AssetHandle::Mesh const curr_handle = state.params.mesh_handle;
            if (ImGui::BeginCombo("Shape", get_asset_meta(curr_handle).name))
            {
                for (u8 i = 0; i < AssetHandle::_Mesh_Count; ++i)
                {
                    AssetHandle::Mesh const handle{i};
                    bool const is_curr = (i == curr_handle);

                    if (ImGui::Selectable(get_asset_meta(handle).name, is_curr))
                    {
                        if (!is_curr)
                        {
                            state.params.mesh_handle = AssetHandle::Mesh{i};
                            on_mesh_asset_change();
                        }
                    }

                    if (is_curr)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }

            static constexpr char const* method_names[] = {
                "None",
                "Least squares conformal",
                "Spectral conformal",
            };

            SolveTexCoords::Method const method = state.params.solve_method;
            if (ImGui::BeginCombo("Method", method_names[method]))
            {
                for (u8 i = 0; i < SolveTexCoords::_Method_Count; ++i)
                {
                    bool const is_selected = (i == method);
                    if (ImGui::Selectable(method_names[i], is_selected))
                    {
                        if (!is_selected)
                        {
                            state.params.solve_method = SolveTexCoords::Method{i};
                            schedule_task(state.tasks.solve_tex_coords);
                        }
                    }

                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }

            ImGui::EndDisabled();
        }
        ImGui::Spacing();

        ImGui::SeparatorText("Display");
        {
            {
                Param<f32>& p = state.params.tex_scale;
                ImGui::SliderFloat("Texture scale", &p.value, p.min, p.max, "%.3f");
            }

            ImGui::Checkbox("Flatten", &state.params.flatten);
        }
        ImGui::Spacing();

        ImGui::EndTabItem();
    }
}

void draw_about_tab()
{
    if (ImGui::BeginTabItem("About"))
    {
        ImGui::SeparatorText("Info");
        ImGui::TextWrapped("Visual comparison of different mesh parameterization methods");
        ImGui::Spacing();

        ImGui::Text(
            "Version %u.%u.%u",
            scene_info.version.major,
            scene_info.version.minor,
            scene_info.version.patch);
        ImGui::Text("%s", scene_info.author);
        ImGui::TextLinkOpenURL("Source", "https://github.com/davreev/demo-mesh-parameterize");
        ImGui::Spacing();

        ImGui::SeparatorText("Controls");
        ImGui::Text("Left click: orbit");
        ImGui::Text("Right click: pan");
        ImGui::Text("Scroll: zoom");
        ImGui::Text("F key: frame shape");
        ImGui::Text("P key: toggle projection");
        ImGui::Spacing();

        ImGui::SeparatorText("References");
        ImGui::TextLinkOpenURL(
            "Least Squares Conformal Maps...",
            "https://www.cs.jhu.edu/~misha/Fall09/Levy02.pdf");
        ImGui::TextLinkOpenURL(
            "Spectral Conformal Parameterization",
            "https://hal.inria.fr/inria-00334477/document");
        ImGui::Spacing();

        ImGui::SeparatorText("Asset Credits");
        for (u8 i = 0; i < AssetHandle::_Mesh_Count; ++i)
        {
            auto const& meta = get_asset_meta(AssetHandle::Mesh{i});
            if (meta.link_url)
                ImGui::TextLinkOpenURL(meta.name, meta.link_url);
        }
        ImGui::Spacing();

        ImGui::EndTabItem();
    }
}

void draw_main_window()
{
    ImGui::SetNextWindowPos({20.0f, 20.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({200.0f, 0.0f}, {sapp_widthf(), sapp_heightf()});
    constexpr int window_flags = ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin(scene_info.name, nullptr, window_flags);

    if (ImGui::BeginTabBar("TabBar", ImGuiTabBarFlags_None))
    {
        draw_settings_tab();
        draw_about_tab();
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void draw_animated_text(Span<char const*> const messages, f64 const duration, f64 const time)
{
    f64 const t = fract(time / duration);
    ImGui::Text("%s", messages[static_cast<isize>(t * messages.size())]);
}

void draw_status_tooltip()
{
    if (state.task_queue.size() > 0)
    {
        ImGui::BeginTooltip();
        static char const* text[] = {
            "Working",
            "Working.",
            "Working..",
            "Working...",
        };
        draw_animated_text(as_span(text), 3.0, App::time_s());
        ImGui::EndTooltip();
    }
}

void draw_ui()
{
    draw_main_window();
    draw_status_tooltip();
}

void debug_draw_mesh_boundary(Mat4<f32> const& world_to_view)
{
    auto const& mesh = state.mesh;
    assert(mesh.asset);

    Mat4<f32> const local_to_world = state.params.flatten //
        ? mesh.tex_xform.to_matrix()
        : mesh.xform.to_matrix();
    Mat4<f32> const local_to_view = world_to_view * local_to_world;

    sgl_matrix_mode_modelview();
    sgl_load_matrix(local_to_view.data());

    sgl_begin_lines();
    sgl_c3f(1.0f, 1.0f, 1.0f);

    if (state.params.flatten)
    {
        auto const v_p = as_span(mesh.tex_coords);
        for (auto const& e_v : mesh.boundary_edge_verts)
        {
            auto const& p0 = v_p[e_v[0]];
            sgl_v3f(p0.x(), p0.y(), 0.0f);

            auto const& p1 = v_p[e_v[1]];
            sgl_v3f(p1.x(), p1.y(), 0.0f);
        }
    }
    else
    {
        auto const v_p = as_span(mesh.asset->vertices.positions);
        for (auto const& e_v : mesh.boundary_edge_verts)
        {
            auto const& p0 = v_p[e_v[0]];
            sgl_v3f(p0.x(), p0.y(), p0.z());

            auto const& p1 = v_p[e_v[1]];
            sgl_v3f(p1.x(), p1.y(), p1.z());
        }
    }

    sgl_end();
}

void draw_debug(Mat4<f32> const& world_to_view, Mat4<f32> const& view_to_clip)
{
    sgl_defaults();

    sgl_matrix_mode_projection();
    sgl_load_matrix(view_to_clip.data());

    debug_draw_axes(world_to_view, 0.1f);

    if (mesh_has_tex_coords())
        debug_draw_mesh_boundary(world_to_view);

    sgl_draw();
}

void open(void* /*context*/)
{
    ThreadPool::start(1);

    init_default_gfx_resources();

    // Center camera on unit sphere
    {
        auto& cam = state.camera;
        cam.target.position = vec<3>(0.0f);
        cam.target.radius = 1.2f;
        cam.frame_target_now();

        // Set default orbit
        cam.controls.orbit = {.polar{pi<f32> * 0.45f}, .azimuth{pi<f32> * 0.35f}};
    }

    // Load default mesh asset and solve
    on_mesh_asset_change();
}

void close(void* /*context*/)
{
    release_all_assets();
    ThreadPool::stop();
}

void update(void* /*context*/)
{
    state.camera.update(App::delta_time_s());
    state.task_queue.poll();
}

void draw(void* /*context*/)
{
    auto const& cam = state.camera;
    Mat4<f32> const world_to_view = cam.make_world_to_view();
    Mat4<f32> const view_to_clip = cam.make_view_to_clip(App::aspect());

    auto const& params = state.params;

    TextureDebugMaterial const mat{
        .tex_scale = params.tex_scale.value,
    };

    auto const& mesh = state.mesh;

    TexturedMeshGeometry const geom{
        .index = mesh.gpu.index.buffer,
        .vertex = mesh.gpu.vertex.buffer,
        .tex_map = mesh.gpu.tex_map.buffer,
        .index_count = mesh.gpu.index.count,
        .vertex_count = mesh.gpu.vertex.count,
    };

    TexturedMesh const render_mesh{
        .geometry = &geom,
        .materials{
            .texture_debug = &mat,
        },
        .transform = params.flatten ? mesh.tex_xform : mesh.xform,
        .flatten = params.flatten,
    };

    state.renderer.render(SceneDesc{
        .meshes = {&render_mesh, mesh_has_tex_coords() ? 1 : 0},
        .camera{
            .world_to_view = world_to_view,
            .view_to_clip = view_to_clip,
        },
    });

    draw_debug(world_to_view, view_to_clip);
    draw_ui();
}

void handle_event(void* /*context*/, App::Event const& event)
{
    camera_handle_mouse_event(event, state.camera);
    camera_handle_touch_event(event, state.camera);

    constexpr auto toggle_projection = []() {
        auto& cam = state.camera;
        if (cam.projection == OrbitCamera::Projection_Perspective)
            cam.projection = OrbitCamera::Projection_Orthographic;
        else
            cam.projection = OrbitCamera::Projection_Perspective;
    };

    switch (event.type)
    {
        case SAPP_EVENTTYPE_KEY_DOWN:
        {
            switch (event.key_code)
            {
                case SAPP_KEYCODE_F:
                {
                    if (is_mouse_over(event))
                        state.camera.frame_target();

                    break;
                };
                case SAPP_KEYCODE_P:
                {
                    if (is_mouse_over(event))
                        toggle_projection();

                    break;
                };
                case SAPP_KEYCODE_R:
                {
                    if (is_mouse_over(event))
                        reload_default_shaders();

                    break;
                };
                default:
                {
                    // ...
                }
            }
            break;
        }
        default:
        {
            // ...
        }
    }
}

} // namespace

App::Scene scene() { return {scene_info.name, open, close, update, draw, handle_event, nullptr}; }

} // namespace dr