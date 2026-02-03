#pragma once

#include <dr/basic_types.hpp>
#include <dr/dynamic_array.hpp>
#include <dr/sliced_array.hpp>
#include <dr/span.hpp>
#include <dr/transform.hpp>

#include <dr/app/draw_command.hpp>
#include <dr/app/gfx_resource.hpp>

namespace dr
{

using GfxBindings = sg_bindings;

/// Simple forward renderer
struct Renderer
{
    enum struct Pass : u8
    {
        Undefined = 0,
        UnlitOpaque,
        // ...
    };

    static void init_default_resources();
    static void reload_default_shaders();

    /// Specialize for different scene types
    template <typename Scene>
    void render(Scene const& scene);

    /// Specialize for different scene object types
    template <Pass pass, typename Source>
    static void emit_draw_cmds(
        Source const& src,
        DynamicArray<DrawCommand>& draw_cmds,
        SlicedArray<u8>& uniform_data);

  private:
    DynamicArray<DrawCommand> draw_cmds_;
    SlicedArray<u8> uniform_data_;
};

struct TextureDebugMaterial
{
    struct
    {
        GfxImage::Handle image;
        GfxSampler::Handle sampler;
    } matcap;
    f32 tex_scale;

    GfxPipeline::Handle pipeline() const;
    Span<u8 const> uniform_data() const;
};

struct TexturedMeshGeometry
{
    GfxBuffer::Handle index{};
    GfxBuffer::Handle vertex{};
    GfxBuffer::Handle tex_map{};
    isize index_count{};
    isize vertex_count{};
};

struct TexturedMesh
{
    TexturedMeshGeometry const* geometry;
    struct
    {
        TextureDebugMaterial const* texture_debug;
        // ...
    } materials;
    Conformal3<f32> transform;
    bool flatten;
};

struct SceneDesc
{
    Span<TexturedMesh const> meshes{};
    // ...

    struct
    {
        Mat4<f32> world_to_view;
        Mat4<f32> view_to_clip;
    } camera;
};

/*
    Specializations
*/

template <>
void Renderer::render(SceneDesc const& scene);

template <>
void Renderer::emit_draw_cmds<Renderer::Pass::UnlitOpaque>(
    TexturedMesh const& src,
    DynamicArray<DrawCommand>& draw_cmds,
    SlicedArray<u8>& uniform_data);

} // namespace dr