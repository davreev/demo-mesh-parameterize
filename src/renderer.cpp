#include "renderer.hpp"

#include <dr/linalg_reshape.hpp>

#include "assets.hpp"
#include "utils.hpp"

namespace dr
{
namespace
{

// NOTE(dr): The assigned shader stage doesn't appear to matter when using OpenGL backends
static sg_shader_stage const shader_stage_any = SG_SHADERSTAGE_VERTEX;

struct PassParams
{
    f32 world_to_view[16];
    f32 world_to_clip[16];

    static sg_shader_uniform_block uniform_block()
    {
        return {
            .stage = shader_stage_any,
            .size = sizeof(PassParams),
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
        };
    }
};

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

    struct MaterialParams
    {
        f32 tex_scale;

        static sg_shader_uniform_block uniform_block()
        {
            return {
                .stage = shader_stage_any,
                .size = sizeof(MaterialParams),
                .glsl_uniforms{
                    {
                        .type = SG_UNIFORMTYPE_FLOAT,
                        .glsl_name = "material.tex_scale",
                    },
                },
            };
        }
    };

    struct ObjectParams
    {
        f32 local_to_world[16];
        i32 flatten;

        static sg_shader_uniform_block uniform_block()
        {
            return {
                .stage = shader_stage_any,
                .size = sizeof(ObjectParams),
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
            };
        }
    };

    static GfxShader::Desc shader_desc(char const* const vs_src, char const* const fs_src)
    {
        return {
            .vertex_func{.source = vs_src},
            .fragment_func{.source = fs_src},
            .uniform_blocks{
                PassParams::uniform_block(),
                MaterialParams::uniform_block(),
                {}, // Geometry block (unused)
                ObjectParams::uniform_block(),
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

} // namespace

void Renderer::init_default_resources()
{
    Impl<TextureDebugMaterial>::init_default_resources();
    // ...
}

void Renderer::reload_default_shaders()
{
    Impl<TextureDebugMaterial>::init_default_shader();
    // ...
}

GfxPipeline::Handle TextureDebugMaterial::pipeline() const
{
    return Impl<TextureDebugMaterial>::default_pipeline;
}

Span<u8 const> TextureDebugMaterial::uniform_data() const
{
    auto& first = tex_scale;
    auto& last = tex_scale;
    
    auto begin = as<u8>(&first);
    return {begin, (as<u8>(&last) + sizeof(last)) - begin};
}

template <>
void Renderer::render(SceneDesc const& scene)
{
    draw_cmds_.clear();
    uniform_data_.clear();

    // Pass uniforms are assumed to be the first slice
    PassParams params{};
    as_mat<4, 4>(params.world_to_view) = scene.camera.world_to_view;
    as_mat<4, 4>(params.world_to_clip) = scene.camera.view_to_clip * scene.camera.world_to_view;
    uniform_data_.push_back(as_bytes(params));

    for (auto const& obj : scene.meshes)
        emit_draw_cmds<TextureDebugMaterial>(obj, draw_cmds_, uniform_data_);

    order_draw_cmds(as_span(draw_cmds_));
    submit_draw_cmds(as_span(draw_cmds_), uniform_data_);
}

template <>
void emit_draw_cmds<TextureDebugMaterial>(
    TexturedMesh const& src,
    DynamicArray<DrawCommand>& draw_cmds,
    SlicedArray<u8>& uniform_data)
{
    using Material = TextureDebugMaterial;
    using Geometry = TexturedMeshGeometry;

    auto set_bindings = [](DrawCommand const& cmd, GfxBindings& b) {
        auto const geom = static_cast<Geometry const*>(cmd.geometry);
        b.vertex_buffers[0] = geom->vertex;
        b.vertex_buffers[1] = geom->vertex;
        b.vertex_buffers[2] = geom->tex_map;
        b.vertex_buffer_offsets[0] = 0;
        b.vertex_buffer_offsets[1] = int(geom->vertex_count * sizeof(f32[3]));
        b.vertex_buffer_offsets[2] = 0;
        b.index_buffer = geom->index;

        auto const mat = static_cast<Material const*>(cmd.material);
        b.images[0] = valid_or(mat->matcap.image, Impl<Material>::default_matcap.image.handle());
        b.samplers[0] = valid_or(
            mat->matcap.sampler,
            Impl<Material>::default_matcap.sampler.handle());
    };

    auto const mat = src.materials.texture_debug;

    // Skip if material isn't assigned
    if (mat == nullptr)
        return;

    // Append draw cmd
    draw_cmds.push_back({
        .pipeline = mat->pipeline(),
        .material = mat,
        .geometry = src.geometry,
        .set_bindings = set_bindings,
        .material_uniform_data = mat->uniform_data(),
        .uniform_slice = uniform_data.num_slices(),
        .num_elements = int(src.geometry->index_count),
        .num_instances = 1,
    });

    // Append uniform data
    Impl<Material>::ObjectParams params{};
    as_mat<4, 4>(params.local_to_world) = src.transform.to_matrix();
    params.flatten = src.flatten;
    uniform_data.push_back(as_bytes(params));
}

} // namespace dr