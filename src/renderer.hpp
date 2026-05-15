#pragma once

#include <dr/basic_types.hpp>
#include <dr/dynamic_array.hpp>
#include <dr/sliced_array.hpp>
#include <dr/span.hpp>
#include <dr/transform.hpp>

#include <dr/app/draw_context.hpp>
#include <dr/app/gfx_resource.hpp>

namespace dr
{

struct TextureDebugMaterial
{
    struct
    {
        GfxView::Handle view;
        GfxSampler::Handle sampler;
    } matcap;
    f32 tex_scale;

    GfxPipeline::Handle pipeline() const;
};

struct TexturedMeshGeometry
{
    GfxBuffer::Handle vertex{};
    GfxBuffer::Handle tex_map{};
    GfxBuffer::Handle index{};
    isize vertex_count{};
    isize index_count{};
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

struct SceneView
{
    Span<TexturedMesh const> meshes{};
    // ...

    struct
    {
        Mat4<f32> world_to_view;
        Mat4<f32> view_to_clip;
    } camera;
};

struct Renderer
{
    static void init_default_resources();
    static void reload_default_shaders();
    static void render(SceneView const& scene, DrawContext& draw_ctx);
};

/// Specialize for different source/material combinations
template <typename Material, typename Source>
void emit_draw_cmds(Source const& src, DrawContext& draw_ctx);

/*
    Specializations
*/

template <>
void emit_draw_cmds<TextureDebugMaterial>(TexturedMesh const& src, DrawContext& draw_ctx);

} // namespace dr