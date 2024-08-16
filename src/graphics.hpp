#pragma once

#include <dr/basic_types.hpp>
#include <dr/math_types.hpp>
#include <dr/span.hpp>

#include <dr/app/gfx_resource.hpp>

namespace dr
{

void init_graphics();

void reload_shaders();

struct RenderPass
{
    template <typename Material>
    void set_material(Material&& mat)
    {
        GfxPipeline::Handle const pipeline = mat.pipeline();
        if (pipeline.id != prev_pipeline_.id)
        {
            sg_apply_pipeline(pipeline);
            prev_pipeline_ = pipeline;
        }
        bindings_ = {};
        mat.bind_resources(bindings_);
        mat.apply_uniforms();
    }

    template <typename Geometry>
    void draw_geometry(Geometry&& geom)
    {
        geom.bind_resources(bindings_);
        sg_apply_bindings(bindings_);
        geom.dispatch_draw();
    }

  private:
    GfxPipeline::Handle prev_pipeline_{};
    sg_bindings bindings_{};
};

////////////////////////////////////////////////////////////////////////////////
// Geometry

struct RenderMesh
{
    GfxBuffer vertices[2];
    isize vertex_capacity{};
    isize vertex_count{};

    GfxBuffer indices{};
    isize index_capacity{};
    isize index_count{};

    void set_vertices(Span<Vec3<f32> const> const& positions, Span<Vec3<f32> const> const& normals);
    void set_vertices(Span<Vec3<f32> const> const& tex_coords);
    void set_indices(Span<Vec3<i32> const> const& faces);

    void bind_resources(sg_bindings& dst) const;
    void dispatch_draw() const { sg_draw(0, index_count, 1); }

  private:
    void set_vertex_capacity(isize value);
    void set_index_capacity(isize value);
};

struct FlattenedRenderMesh
{
    RenderMesh const* src;

    void bind_resources(sg_bindings& dst) const
    {
        src->bind_resources(dst);
        // Use tex coords in position slot
        dst.vertex_buffers[0] = src->vertices[1];
    }

    void dispatch_draw() const { src->dispatch_draw(); }
};

////////////////////////////////////////////////////////////////////////////////
// Materials

struct MatcapDebug
{
    // clang-format off
    struct {
        struct {
            f32 local_to_clip[16];
            f32 local_to_view[16];
        } vertex;
        struct {
            f32 tex_scale;
        } fragment;
    } params{};
    // clang-format on

    static GfxPipeline::Handle pipeline();

    void bind_resources(sg_bindings& dst) const;
    void apply_uniforms() const;
};

} // namespace dr