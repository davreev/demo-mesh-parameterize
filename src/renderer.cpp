#include "renderer.hpp"

#include <algorithm>

#include <dr/linalg_reshape.hpp>

#include "assets.hpp"
#include "utils.hpp"

namespace dr
{
namespace
{

enum struct UniformBlock : u8
{
    Pass = 0,
    Material,
    Geometry,
    Object,
};

// NOTE(dr): The assigned shader stage doesn't appear to matter when using OpenGL backends
static sg_shader_stage const shader_stage_any = SG_SHADERSTAGE_VERTEX;

template <typename T>
struct Impl;

template <>
struct Impl<TextureDebugMaterial>
{
    static inline GfxPipeline default_pipeline;
    static inline GfxShader default_shader;
    struct
    {
        GfxImage image;
        GfxSampler sampler;
    } inline static default_matcap;

    static GfxShader::Desc shader_desc(char const* const vs_src, char const* const fs_src)
    {
        return {
            .vertex_func{.source = vs_src},
            .fragment_func{.source = fs_src},
            .uniform_blocks{
                {
                    // Pass block
                    .stage = shader_stage_any,
                    .size = sizeof(f32[16 * 2]),
                    .glsl_uniforms{
                        {
                            .type = SG_UNIFORMTYPE_FLOAT4,
                            .array_count = 4,
                            .glsl_name = "pass.world_to_view.data",
                        },
                        {
                            .type = SG_UNIFORMTYPE_FLOAT4,
                            .array_count = 4,
                            .glsl_name = "pass.world_to_clip.data",
                        },
                    },
                },
                {
                    // Material block
                    .stage = shader_stage_any,
                    .size = sizeof(f32),
                    .glsl_uniforms{
                        {
                            .type = SG_UNIFORMTYPE_FLOAT,
                            .glsl_name = "material.tex_scale",
                        },
                    },
                },
                {
                    // Geometry block
                    // ...
                },
                {
                    // Object block
                    .stage = shader_stage_any,
                    .size = sizeof(f32[16 + 1]),
                    .glsl_uniforms{
                        {
                            .type = SG_UNIFORMTYPE_FLOAT4,
                            .array_count = 4,
                            .glsl_name = "object.local_to_world.data",
                        },
                        {
                            .type = SG_UNIFORMTYPE_INT,
                            .glsl_name = "object.flatten",
                        },
                    },
                },
            },
            .images{
                {.stage = shader_stage_any},
            },
            .samplers{
                {.stage = shader_stage_any},
            },
            .image_sampler_pairs{
                {
                    .stage = shader_stage_any,
                    .image_slot = 0,
                    .sampler_slot = 0,
                    .glsl_name = "matcap",
                },
            },
        };
    }

    static GfxPipeline::Desc pipeline_desc(GfxShader::Handle const shader)
    {
        return {
            .shader = shader,
            .layout{
                .attrs{
                    {.buffer_index = 0, .format = SG_VERTEXFORMAT_FLOAT3},
                    {.buffer_index = 1, .format = SG_VERTEXFORMAT_FLOAT3},
                    {.buffer_index = 2, .format = SG_VERTEXFORMAT_FLOAT2},
                },
            },
            .depth{
                .compare = SG_COMPAREFUNC_LESS,
                .write_enabled = true,
            },
            .index_type = SG_INDEXTYPE_UINT32,
            .face_winding = SG_FACEWINDING_CCW,
        };
    }

    static GfxImage::Desc matcap_image_desc(
        void const* const data,
        int const width,
        int const height)
    {
        return {
            .width = width,
            .height = height,
            .usage = SG_USAGE_IMMUTABLE,
            .pixel_format = SG_PIXELFORMAT_RGBA8,
            .data{
                .subimage{
                    {
                        {.ptr = data, .size = usize(width * height * 4)},
                    },
                },
            },
        };
    }

    static GfxSampler::Desc matcap_sampler_desc(void)
    {
        return {
            .min_filter = SG_FILTER_LINEAR,
            .mag_filter = SG_FILTER_LINEAR,
        };
    }

    static void init_default_shader()
    {
        ShaderAsset const* vs = get_asset(AssetHandle::Shader_TextureDebugVert, true);
        assert(vs);

        ShaderAsset const* fs = get_asset(AssetHandle::Shader_TextureDebugFrag, true);
        assert(fs);

        default_shader.init(shader_desc(vs->src.c_str(), fs->src.c_str()));
        assert(default_shader.is_valid());
    };

    static void init_default_resources()
    {
        assert(!default_pipeline.is_valid());

        default_shader = GfxShader::alloc();
        init_default_shader();

        default_pipeline = GfxPipeline::make(pipeline_desc(default_shader));
        assert(default_pipeline.is_valid());

        {
            ImageAsset const* image = get_asset(AssetHandle::Image_Matcap);
            assert(image);

            default_matcap.image = GfxImage::make(
                matcap_image_desc(image->data.get(), image->width, image->height));
            assert(default_matcap.image.is_valid());

            default_matcap.sampler = GfxSampler::make(matcap_sampler_desc());
            assert(default_matcap.sampler.is_valid());
        }
    };
};

template <>
struct Impl<SceneDesc>
{
    // NOTE(dr): Can be specialized for different passes (e.g. lit vs unlit)
    template <Renderer::Pass pass>
    static void emit_draw_cmds(
        SceneDesc const& src,
        DynamicArray<Renderer::DrawCommand>& draw_cmds,
        SlicedArray<u8>& uniform_data)
    {
        draw_cmds.clear();
        uniform_data.clear();

        // Pass uniforms are assumed to be the first slice
        struct
        {
            f32 world_to_view[16];
            f32 world_to_clip[16];
        } u;
        as_mat<4, 4>(u.world_to_view) = src.camera.world_to_view;
        as_mat<4, 4>(u.world_to_clip) = src.camera.view_to_clip * src.camera.world_to_view;
        uniform_data.push_back(as_bytes(u));

        for (auto const& obj : src.meshes)
            Renderer::emit_draw_cmds<pass>(obj, draw_cmds, uniform_data);

        // Emit any other scene objects included in this pass
        // ...
    }
};

void apply_uniforms(UniformBlock const block, Span<u8 const> const data)
{
    sg_apply_uniforms(int(block), to_range(data));
}

} // namespace

void Renderer::submit_draw_cmds(
    Span<DrawCommand> const& draw_cmds,
    SlicedArray<u8> const& uniform_data)
{
    // Order draw commands by pipeline, then material, then geometry
    std::sort(begin(draw_cmds), end(draw_cmds), [](DrawCommand const& a, DrawCommand const& b) {
        if (a.pipeline.id != b.pipeline.id)
            return a.pipeline.id < b.pipeline.id;
        else if (a.material != b.material)
            return a.material < b.material;
        else
            return a.geometry < b.geometry;
    });

    GfxPipeline::Handle pipeline{};
    void const* geometry = nullptr;
    void const* material = nullptr;

    // Pass uniforms are assumed to be the first slice
    assert(uniform_data.num_slices() > 0);
    Span<u8 const> const pass_uniform_data = uniform_data[0];

    // Submit draw commands
    for (auto const& cmd : draw_cmds)
    {
        if (cmd.pipeline.id != pipeline.id)
        {
            pipeline = cmd.pipeline;
            sg_apply_pipeline(pipeline);

            if (pass_uniform_data.size() > 0)
                apply_uniforms(UniformBlock::Pass, pass_uniform_data);

            geometry = nullptr;
            material = nullptr;
        }

        bool bindings_dirty = false;

        if (cmd.material != material)
        {
            if (cmd.material_uniform_data.size() > 0)
                apply_uniforms(UniformBlock::Material, cmd.material_uniform_data);

            material = cmd.material;
            bindings_dirty = true;
        }

        if (cmd.geometry != geometry)
        {
            if (cmd.geometry_uniform_data.size() > 0)
                apply_uniforms(UniformBlock::Geometry, cmd.geometry_uniform_data);

            geometry = cmd.geometry;
            bindings_dirty = true;
        }

        if (bindings_dirty)
            sg_apply_bindings(cmd.bindings);

        // Slice index of 0 is treated as invalid for object uniforms
        if (cmd.uniform_slice != 0)
        {
            Span<u8 const> const object_uniform_data = uniform_data[cmd.uniform_slice];
            if (object_uniform_data.size() > 0)
                apply_uniforms(UniformBlock::Object, object_uniform_data);
        }

        sg_draw(cmd.base_element, cmd.num_elements, cmd.num_instances);
    }
}

template <>
void Renderer::render(SceneDesc const& scene)
{
    using Impl = Impl<SceneDesc>;

    Impl::emit_draw_cmds<Pass::UnlitOpaque>(scene, draw_cmds_, uniform_data_);
    submit_draw_cmds(as_span(draw_cmds_), uniform_data_);
}

template <>
void Renderer::emit_draw_cmds<Renderer::Pass::UnlitOpaque>(
    TexturedMesh const& src,
    DynamicArray<Renderer::DrawCommand>& draw_cmds,
    SlicedArray<u8>& uniform_data)
{
    using MatImpl = Impl<TextureDebugMaterial>;

    auto const mat = src.materials.texture_debug;
    auto const geom = src.geometry;

    // Skip if material isn't assigned
    if (mat == nullptr)
        return;

    // Append draw cmd
    draw_cmds.push_back({
        .bindings{
            .vertex_buffers{
                geom->vertex,
                geom->vertex,
                geom->tex_map,
            },
            .vertex_buffer_offsets{
                0,
                int(geom->vertex_count * sizeof(f32[3])),
                0,
            },
            .index_buffer = geom->index,
            .images{
                valid_or(mat->matcap.image, MatImpl::default_matcap.image.handle()),
            },
            .samplers{
                valid_or(mat->matcap.sampler, MatImpl::default_matcap.sampler.handle()),
            },
        },
        .pipeline = mat->pipeline(),
        .material = mat,
        .geometry = geom,
        .material_uniform_data = mat->uniform_data(),
        .uniform_slice = uniform_data.num_slices(),
        .num_elements = int(geom->index_count),
        .num_instances = 1,
    });

    // Append uniform data
    struct
    {
        f32 local_to_world[16];
        i32 flatten;
    } u;
    as_mat<4, 4>(u.local_to_world) = src.transform.to_matrix();
    u.flatten = src.flatten;
    uniform_data.push_back(as_bytes(u));
}

GfxPipeline::Handle TextureDebugMaterial::pipeline() const
{
    return Impl<TextureDebugMaterial>::default_pipeline;
}

Span<u8 const> TextureDebugMaterial::uniform_data() const
{
    return {as<u8>(&tex_scale), sizeof(f32[1])};
}

void init_default_gfx_resources()
{
    Impl<TextureDebugMaterial>::init_default_resources();
    // ...
}

void reload_default_shaders()
{
    Impl<TextureDebugMaterial>::init_default_shader();
    // ...
}

} // namespace dr