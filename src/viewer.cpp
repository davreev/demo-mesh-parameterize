#include "viewer.hpp"

#include <dr/linalg_reshape.hpp>

#include "assets.hpp"

namespace dr
{
namespace
{

enum UniformBlock : u8
{
    UniformBlock_Material = 0,
    UniformBlock_Geometry,
    UniformBlock_Object,
    _UniformBlock_Count,
};

// NOTE(dr): The assigned shader stage doesn't appear to matter when using OpenGL backends
static sg_shader_stage const shader_stage_any = SG_SHADERSTAGE_VERTEX;

template <typename T>
struct DefaultResources;

template <>
struct DefaultResources<Viewer::TextureDebugMaterial>
{
    static inline GfxPipeline pipeline;
    static inline GfxShader shader;
    struct
    {
        GfxImage image;
        GfxSampler sampler;
    } inline static matcap;

    static GfxShader::Desc shader_desc(char const* const vs_src, char const* const fs_src)
    {
        return {
            .vertex_func{.source = vs_src},
            .fragment_func{.source = fs_src},
            .uniform_blocks{
                {
                    // Material block
                    .stage = shader_stage_any,
                    .size = sizeof(f32),
                    .glsl_uniforms{
                        {.type = SG_UNIFORMTYPE_FLOAT, .glsl_name = "material.tex_scale"},
                    },
                },
                {
                    // Geometry block
                },
                {
                    // Object block
                    .stage = shader_stage_any,
                    .size = sizeof(f32[16 * 2 + 1]),
                    .glsl_uniforms{
                        {
                            .type = SG_UNIFORMTYPE_FLOAT4,
                            .array_count = 4,
                            .glsl_name = "object.local_to_clip.data",

                        },
                        {
                            .type = SG_UNIFORMTYPE_FLOAT4,
                            .array_count = 4,
                            .glsl_name = "object.local_to_view.data",

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

    static GfxSampler::Desc matcap_sampler_desc()
    {
        return {
            .min_filter = SG_FILTER_LINEAR,
            .mag_filter = SG_FILTER_LINEAR,
        };
    }

    static void init_shader()
    {
        ShaderAsset const* vs = get_asset(AssetHandle::Shader_TextureDebugVert, true);
        assert(vs);

        ShaderAsset const* fs = get_asset(AssetHandle::Shader_TextureDebugFrag, true);
        assert(fs);

        shader.init(shader_desc(vs->src.c_str(), fs->src.c_str()));
        assert(shader.is_valid());
    };

    static void init()
    {
        assert(!pipeline.is_valid());

        shader = GfxShader::alloc();
        init_shader();

        pipeline = GfxPipeline::make(pipeline_desc(shader));
        assert(pipeline.is_valid());

        {
            ImageAsset const* image = get_asset(AssetHandle::Image_Matcap);
            assert(image);

            matcap.image = GfxImage::make(
                matcap_image_desc(image->data.get(), image->width, image->height));
            assert(matcap.image.is_valid());

            matcap.sampler = GfxSampler::make(matcap_sampler_desc());
            assert(matcap.sampler.is_valid());
        }
    };
};

template <>
struct DefaultResources<Viewer::MeshGeometry>
{
    static GfxBuffer::Desc vertex_buffer_desc(usize const size)
    {
        return {
            .size = size,
            .type = SG_BUFFERTYPE_VERTEXBUFFER,
            .usage = SG_USAGE_DYNAMIC,
        };
    }

    static GfxBuffer::Desc index_buffer_desc(usize const size)
    {
        return {
            .size = size,
            .type = SG_BUFFERTYPE_INDEXBUFFER,
            .usage = SG_USAGE_DYNAMIC,
        };
    }
};

// Returns the given handle if it's valid. Otherwise, returns the given default.
template <typename Handle>
Handle const valid_or(Handle const handle, Handle const other)
{
    return (handle.id == SG_INVALID_ID) ? other : handle;
}

template <typename T>
sg_range to_range(Span<T> const& span)
{
    return {span.data(), span.size() * sizeof(T)};
}

template <typename Resource>
void init_resource(Resource& buf, typename Resource::Desc const& desc)
{
    if (buf.is_valid())
        buf.init(desc);
    else
        buf = Resource::make(desc);
}

void set_pipeline(Viewer::DrawContext& ctx, GfxPipeline::Handle const pipeline)
{
    // Avoid unecessary pipeline change
    if (pipeline.id == ctx.pipeline.id)
        return;

    sg_apply_pipeline(pipeline);
    ctx.pipeline = pipeline;
    ctx.bindings = {};
    ctx.material = nullptr;
    ctx.geometry = nullptr;
}

void set_material(Viewer::DrawContext& ctx, Viewer::TextureDebugMaterial const* material)
{
    using Default = DefaultResources<Viewer::TextureDebugMaterial>;
    set_pipeline(ctx, valid_or(material->pipeline, Default::pipeline.handle()));

    ctx.bindings.images[0] = valid_or(material->matcap.image, Default::matcap.image.handle());
    ctx.bindings.samplers[0] = valid_or(material->matcap.sampler, Default::matcap.sampler.handle());

    struct
    {
        f32 tex_scale;
    } u;

    u.tex_scale = material->tex_scale;
    sg_apply_uniforms(UniformBlock_Material, {&u, sizeof(u)});

    ctx.material = material;
}

template <typename Material>
void set_geometry(Viewer::DrawContext& ctx, Viewer::TexturedMeshGeometry const* geometry)
{
    using OkMaterials = TypePack<Viewer::TextureDebugMaterial>;

    // NOTE(dr): Can static dispatch based on bound material type
    static_assert(
        OkMaterials::includes<Material>,
        "Geometry isn't compatible with bound material type");

    ctx.bindings.vertex_buffers[0] = geometry->mesh->vertices.buffer;
    ctx.bindings.vertex_buffers[1] = geometry->mesh->vertices.buffer;
    ctx.bindings.vertex_buffer_offsets[1] = geometry->mesh->vertices.count * sizeof(f32[3]);
    ctx.bindings.vertex_buffers[2] = geometry->tex_coords.buffer;
    ctx.bindings.index_buffer = geometry->mesh->indices.buffer;

    ctx.geometry = geometry;
}

template <typename Material>
void submit_draw(Viewer::DrawContext const& ctx, Viewer::TexturedMesh const& object)
{
    using OkMaterials = TypePack<Viewer::TextureDebugMaterial>;

    // NOTE(dr): Can static dispatch based on bound material and geometry types
    static_assert(
        OkMaterials::includes<Material>,
        "Object isn't compatible with bound material type");

    struct
    {
        f32 local_to_clip[16];
        f32 local_to_view[16];
        int flatten;
    } u;

    Mat4<f32> const local_to_world = object.transform.to_matrix();
    as_mat<4, 4>(u.local_to_clip) = ctx.transforms.world_to_clip * local_to_world;
    as_mat<4, 4>(u.local_to_view) = ctx.transforms.world_to_view * local_to_world;
    u.flatten = object.flatten;
    sg_apply_uniforms(UniformBlock_Object, {&u, sizeof(u)});

    const isize num_indices = object.geometry->mesh->indices.count;
    sg_draw(0, num_indices, 1);
}

template <int material_id, typename Object>
void draw_impl(Viewer::DrawContext& ctx, Object const& object)
{
    using Material = typename Object::Materials::template At<material_id>;

    auto mat = std::get<material_id>(object.materials);
    if (mat == nullptr)
        return;

    auto geom = object.geometry;
    if (geom == nullptr)
        return;

    bool bindings_dirty = false;

    // Update material
    if (mat != ctx.material)
    {
        set_material(ctx, mat);
        bindings_dirty = true;
    }

    // Update geometry
    if (geom != ctx.geometry)
    {
        set_geometry<Material>(ctx, geom);
        bindings_dirty = true;
    }

    // Commit bound resources
    if (bindings_dirty)
        sg_apply_bindings(ctx.bindings);

    // Submit draw call
    submit_draw<Material>(ctx, object);
}

} // namespace

void Viewer::init_default_resources() { DefaultResources<Viewer::TextureDebugMaterial>::init(); }

void Viewer::reload_default_shaders()
{
    DefaultResources<Viewer::TextureDebugMaterial>::init_shader();
}

template <int material_id, typename Object_>
void Viewer::DrawContext::draw(Object_ const& object)
{
    draw_impl<material_id>(*this, object);
}

template void Viewer::DrawContext::draw<0>(Viewer::TexturedMesh const&);
// ...
// ...
// ...

Viewer::DrawContext Viewer::make_draw_context(
    Mat4<f32> const& world_to_view,
    Mat4<f32> const& view_to_clip)
{
    DrawContext ctx{};
    ctx.transforms.view_to_clip = view_to_clip;
    ctx.transforms.world_to_view = world_to_view;
    ctx.transforms.world_to_clip = view_to_clip * world_to_view;
    return ctx;
}

GfxPipeline Viewer::TextureDebugMaterial::make_pipeline(GfxShader::Handle shader)
{
    using Default = DefaultResources<TextureDebugMaterial>;
    return GfxPipeline::make(Default::pipeline_desc(shader));
}

void Viewer::MeshGeometry::set_vertices(
    Span<Vec3<f32> const> const& positions,
    Span<Vec3<f32> const> const& normals)
{
    using Default = DefaultResources<MeshGeometry>;

    assert(positions.size() == normals.size());

    vertices.count = positions.size();
    if (vertices.count > vertices.capacity)
    {
        init_resource(vertices.buffer, Default::vertex_buffer_desc(vertices.size()));
        vertices.capacity = vertices.count;
    }

    sg_append_buffer(vertices.buffer, to_range(positions));
    sg_append_buffer(vertices.buffer, to_range(normals));
}

void Viewer::MeshGeometry::set_indices(Span<Vec3<i32> const> const& faces)
{
    using Default = DefaultResources<MeshGeometry>;

    indices.count = faces.size() * 3;
    if (indices.count > indices.capacity)
    {
        init_resource(indices.buffer, Default::index_buffer_desc(indices.size()));
        indices.capacity = indices.count;
    }

    sg_update_buffer(indices.buffer, to_range(faces));
}

void Viewer::TexturedMeshGeometry::set_tex_coords(Span<Vec2<f32> const> const& values)
{
    using Default = DefaultResources<MeshGeometry>;

    assert(mesh != nullptr);
    assert(values.size() == mesh->vertices.count);

    tex_coords.count = values.size();
    if (tex_coords.count > tex_coords.capacity)
    {
        init_resource(tex_coords.buffer, Default::vertex_buffer_desc(tex_coords.size()));
        tex_coords.capacity = tex_coords.count;
    }

    sg_update_buffer(tex_coords.buffer, to_range(values));
}

} // namespace dr