#include "scene.hpp"

#include <sokol_gl.h>

#include <dr/math_ctors.hpp>
#include <dr/span.hpp>

#include <dr/app/camera.hpp>
#include <dr/app/debug_draw.hpp>
#include <dr/app/event_handlers.hpp>
#include <dr/app/gfx_utils.hpp>
#include <dr/app/shim/imgui.hpp>
#include <dr/app/task_queue.hpp>
#include <dr/app/thread_pool.hpp>

#include "assets.hpp"
#include "graphics.hpp"
#include "tasks.hpp"

namespace dr
{
namespace
{

template <typename Scalar>
struct Param
{
    Scalar value{};
    Scalar min{};
    Scalar max{};
};

// clang-format off
struct {
    char const* name = "Mesh Parameterize";
    char const* author = "David Reeves";
    struct {
        u16 major{0};
        u16 minor{4};
        u16 patch{0};
    } version;
} constexpr scene_info{};

struct {
    struct {
        RenderMesh mesh;
        struct {
            MatcapDebug matcap_debug;
        } materials;
    } gfx;

    struct {
        MeshAsset const* mesh;
        DynamicArray<Vec3<f32>> tex_coords;
        DynamicArray<Vec2<i32>> boundary_edge_verts;
        Vec2<i32> ref_verts;
    } shape;

    TaskQueue task_queue;
    struct {
        LoadMeshAsset load_mesh_asset;
        ExtractMeshBoundary extract_boundary;
        SolveTexCoords solve_tex_coords;
    } tasks;

    struct {
        f32 fov_y{deg_to_rad(60.0f)};
        f32 clip_near{0.01f};
        f32 clip_far{100.0f};
    } view;

    EasedOrbit orbit{{pi<f32> * 0.3f, pi<f32> * 0.5f}};
    EasedZoom zoom{{2.0f, 1.0f, view.clip_near, view.clip_far}};
    EasedPan pan{};
    Camera camera{make_camera(orbit.current, zoom.current)};

    struct {
        Vec2<f32> last_touch_points[2];
        i8 last_num_touches;
        bool mouse_down[3];
    } input;

    struct {
        Param<f32> tex_scale{0.01f, 0.001f, 0.1f};
        AssetHandle::Mesh mesh_handle;
        SolveTexCoords::Method solve_method{SolveTexCoords::Method_LeastSquaresConformal};
        bool flatten;
    } params;
} state{};
// clang-format on

void center_camera(Vec3<f32> const& point, f32 const radius)
{
    constexpr f32 pad_scale{1.2f};
    state.camera.pivot.position = point;
    state.zoom.target.distance = radius * pad_scale / std::sin(state.view.fov_y * 0.5);
    state.pan.target.offset = {};
}

void set_mesh(MeshAsset const* mesh)
{
    state.shape.mesh = mesh;
    state.shape.tex_coords.assign(mesh->vertices.count(), {});
    state.shape.boundary_edge_verts.clear();

    // Update the render mesh
    {
        auto& render_mesh = state.gfx.mesh;
        render_mesh.set_indices(as_span(mesh->faces.vertex_ids));
        render_mesh.set_vertices(
            as_span(mesh->vertices.positions),
            as_span(mesh->vertices.normals));

        // Set default texture coords from asset
        render_mesh.set_vertices(as_span(state.shape.tex_coords));
    }
}

void set_mesh_boundary(Span<Vec2<i32> const> const& boundary_edge_verts)
{
    auto const& src = boundary_edge_verts;
    state.shape.boundary_edge_verts.assign(begin(src), end(src));

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
        state.shape.ref_verts = table[state.params.mesh_handle];
    }
};

void set_tex_coords(Span<Vec2<f32> const> const& tex_coords)
{
    auto const dst = as_span(state.shape.tex_coords);
    as_mat(dst)({0, 1}, Eigen::all) = as_mat(tex_coords);
    state.gfx.mesh.set_vertices(dst);
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
                set_mesh(task->output.mesh);
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
                task->input.mesh = state.shape.mesh;
                return true;
            };
            case Event::AfterComplete:
            {
                set_mesh_boundary(task->output.boundary_edge_verts);
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
                task->input.mesh = state.shape.mesh;
                task->input.boundary_edge_verts = as_span(state.shape.boundary_edge_verts);
                task->input.ref_verts = state.shape.ref_verts;
                task->input.method = state.params.solve_method;
                return true;
            };
            case Event::AfterComplete:
            {
                set_tex_coords(task->output.tex_coords);
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

            static constexpr char const* mesh_names[] = {
                "Human head",
                "Pig head",
                "Camel head",
                "Ogre face",
                "VW Bug",
            };

            AssetHandle::Mesh const handle = state.params.mesh_handle;
            if (ImGui::BeginCombo("Shape", mesh_names[handle]))
            {
                for (u8 i = 0; i < AssetHandle::_Mesh_Count; ++i)
                {
                    bool const is_selected = (i == handle);
                    if (ImGui::Selectable(mesh_names[i], is_selected))
                    {
                        if (!is_selected)
                        {
                            state.params.mesh_handle = AssetHandle::Mesh{i};
                            on_mesh_asset_change();
                        }
                    }

                    if (is_selected)
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
        ImGui::TextLinkOpenURL("Armadillo", "http://graphics.stanford.edu/data/3Dscanrep/");
        ImGui::TextLinkOpenURL(
            "Human head",
            "https://www.sidefx.com/docs/houdini/nodes/sop/testgeometry_templatehead.html");
        ImGui::TextLinkOpenURL(
            "Pig head",
            "https://www.sidefx.com/docs/houdini/nodes/sop/testgeometry_pighead.html");
        ImGui::TextLinkOpenURL(
            "Camel head",
            "https://igl.ethz.ch/projects/Laplacian-mesh-processing/ls-meshes/");
        ImGui::TextLinkOpenURL(
            "Ogre face",
            "https://www.cs.cmu.edu/~kmcrane/Projects/ModelRepository/");
        ImGui::TextLinkOpenURL("VW Bug", "https://www.cs.utah.edu/docs/misc/Uteapot03.pdf");
        ImGui::Spacing();

        ImGui::EndTabItem();
    }
}

void draw_main_window()
{
    ImGui::SetNextWindowPos({20.0f, 20.0f}, ImGuiCond_FirstUseEver);
    constexpr int window_flags = ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin(scene_info.name, nullptr, window_flags);
    ImGui::PushItemWidth(200.0f);

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

void debug_draw_mesh_boundary(Mat4<f32> const& local_to_view)
{
    sgl_matrix_mode_modelview();
    sgl_load_matrix(local_to_view.data());

    sgl_begin_lines();
    sgl_c3f(1.0f, 1.0f, 1.0f);

    auto const v_p = state.params.flatten //
        ? as_span(state.shape.tex_coords)
        : as_span(state.shape.mesh->vertices.positions);

    for (auto const& e_v : state.shape.boundary_edge_verts)
    {
        auto const& p0 = v_p[e_v[0]];
        sgl_v3f(p0.x(), p0.y(), p0.z());

        auto const& p1 = v_p[e_v[1]];
        sgl_v3f(p1.x(), p1.y(), p1.z());
    }

    sgl_end();
}

void draw_debug(
    Mat4<f32> const& world_to_view,
    Mat4<f32> const& local_to_view,
    Mat4<f32> const& view_to_clip)
{
    sgl_defaults();

    sgl_matrix_mode_projection();
    sgl_load_matrix(view_to_clip.data());

    debug_draw_axes(world_to_view, 0.1f);

    if (state.shape.mesh)
        debug_draw_mesh_boundary(local_to_view);

    sgl_draw();
}

void open(void* /*context*/)
{
    thread_pool_start(1);
    init_graphics();

    // Load default mesh asset and solve
    on_mesh_asset_change();
}

void close(void* /*context*/)
{
    release_all_assets();
    thread_pool_stop();
}

void update(void* /*context*/)
{
    f32 const t = saturate(5.0 * App::delta_time_s());

    state.orbit.update(t);
    state.orbit.apply(state.camera);

    state.zoom.update(t);
    state.zoom.apply(state.camera);

    state.pan.update(t);
    state.pan.apply(state.camera);

    state.task_queue.poll();
}

void draw(void* /*context*/)
{
    constexpr auto make_local_to_world = []() -> Mat4<f32> {
        if (state.shape.mesh)
        {
            if (state.params.flatten)
            {
                if (state.params.solve_method == SolveTexCoords::Method_None)
                {
                    static auto const r = mat(
                        vec(0.0f, 1.0f, 0.0f),
                        vec(0.0f, 0.0f, 1.0f),
                        vec(1.0f, 0.0f, 0.0f));

                    auto const [cen, rad] = state.shape.mesh->bounds;
                    f32 const s = 1.0f / rad;
                    return make_affine(s * r, -cen * s);
                }
                else
                {
                    static auto const r = mat(
                        vec(0.0f, 0.0f, 1.0f),
                        vec(0.0f, 1.0f, 0.0f),
                        vec(-1.0f, 0.0f, 0.0f));

                    return make_affine(r);
                }
            }
            else
            {
                // Fit to unit sphere
                auto const [cen, rad] = state.shape.mesh->bounds;
                f32 const s = 1.0f / rad;
                return make_scale_translate(vec<3>(s), -cen * s);
            }
        }
        else
        {
            return Mat4<f32>::Identity();
        }
    };

    Mat4<f32> const local_to_world = make_local_to_world();
    Mat4<f32> const world_to_view = state.camera.transform().inverse_to_matrix();
    Mat4<f32> const local_to_view = world_to_view * local_to_world;
    Mat4<f32> const view_to_clip = make_perspective(
        state.view.fov_y,
        App::aspect(),
        state.view.clip_near,
        state.view.clip_far);

    if (state.shape.mesh)
    {
        sg_bindings bindings{};

        auto& mat = state.gfx.materials.matcap_debug;
        sg_apply_pipeline(mat.pipeline());
        mat.bind_resources(bindings);

        // Update uniforms
        as_mat<4, 4>(mat.uniforms.vertex.local_to_clip) = view_to_clip * local_to_view;
        as_mat<4, 4>(mat.uniforms.vertex.local_to_view) = local_to_view;
        mat.uniforms.fragment.tex_scale = state.params.tex_scale.value;
        mat.apply_uniforms();

        auto const bind_and_draw = [&](auto&& geom) {
            geom.bind_resources(bindings);
            sg_apply_bindings(bindings);
            geom.dispatch_draw();
        };

        if (state.params.flatten)
            bind_and_draw(FlattenedRenderMesh{&state.gfx.mesh});
        else
            bind_and_draw(state.gfx.mesh);
    }

    draw_debug(world_to_view, local_to_view, view_to_clip);
    draw_ui();
}

void handle_event(void* /*context*/, App::Event const& event)
{
    f32 const screen_to_view = dr::screen_to_view(state.view.fov_y, sapp_heightf());

    camera_handle_mouse_event(
        event,
        state.zoom.target,
        &state.orbit.target,
        &state.pan.target,
        screen_to_view,
        state.input.mouse_down);

    camera_handle_touch_event(
        event,
        state.zoom.target,
        &state.orbit.target,
        &state.pan.target,
        screen_to_view,
        state.input.last_touch_points,
        state.input.last_num_touches);

    switch (event.type)
    {
        case SAPP_EVENTTYPE_KEY_DOWN:
        {
            switch (event.key_code)
            {
                case SAPP_KEYCODE_F:
                {
                    if (is_mouse_over(event))
                        center_camera({}, 1.0f);

                    break;
                }
                case SAPP_KEYCODE_R:
                {
                    reload_shaders();
                    break;
                }
                default:
                {
                }
            }
            break;
        }
        default:
        {
        }
    }
}

} // namespace

App::Scene scene() { return {scene_info.name, open, close, update, draw, handle_event, nullptr}; }

} // namespace dr