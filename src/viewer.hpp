#pragma once

#include <dr/basic_types.hpp>
#include <dr/math_types.hpp>

#include <dr/app/app.hpp>
#include <dr/app/camera.hpp>
#include <dr/app/gfx_resource.hpp>

namespace dr
{

struct Viewer
{
    struct TextureDebugMaterial
    {

        GfxPipeline::Handle pipeline;
        struct
        {
            GfxImage::Handle image;
            GfxSampler::Handle sampler;
        } matcap;
        f32 tex_scale;

        static GfxPipeline make_custom_pipeline(GfxShader::Handle shader);
    };

    template <isize stride_>
    struct Buffer
    {
        static constexpr isize stride = stride_;

        GfxBuffer buffer;
        isize capacity;
        isize count;

        isize size() const { return count * stride; }
    };

    struct MeshGeometry
    {
        Buffer<sizeof(f32[6])> vertices;
        Buffer<sizeof(i32)> indices;

        void set_vertices(
            Span<Vec3<f32> const> const& positions,
            Span<Vec3<f32> const> const& normals);

        void set_indices(Span<Vec3<i32> const> const& faces);
    };

    struct TexturedMeshGeometry
    {
        Buffer<sizeof(f32[2])> tex_coords;
        MeshGeometry const* mesh;

        void set_tex_coords(Span<Vec2<f32> const> const& values);
    };

    struct TexturedMeshInstance
    {
        Conformal3<f32> transform;
        TexturedMeshGeometry const* geometry;
        TextureDebugMaterial const* material;
        bool flatten;
    };

    struct View
    {
        Camera camera;
        struct
        {
            f32 fov_y{deg_to_rad(60.0f)};
            f32 clip_near{0.01f};
            f32 clip_far{1000.0f};
        } frustum;
        struct
        {
            EasedOrbit orbit{{pi<f32> * 0.3f, pi<f32> * 0.5f}};
            EasedZoom zoom{{2.0f, 1.0f, 0.01, 1000.0}};
            EasedPan pan{};
            f32 sensitivity{5.0f};
        } controls;
        struct
        {
            Vec3<f32> position{};
            f32 radius{1.0f};
        } target;

        View();
        void frame_target();
    };

    /// Transient data associated with the current frame. Refreshed by call to Viewer::update.
    struct Frame
    {
        Mat4<f32> view_to_clip;
        Mat4<f32> world_to_view;
        Mat4<f32> world_to_clip;
    };

    struct Input
    {
        Vec2<f32> last_touch_points[2];
        i8 last_num_touches;
        bool mouse_down[3];
    };

    View view;
    Frame frame;
    Input input;

    static void init_default_resources();

    static void reload_default_shaders();

    void update();

    template <typename Material, typename Geometry, typename Instance>
    void draw(Span<Instance const> const& instances) const;

    void handle_event(App::Event const& event);
};

} // namespace dr