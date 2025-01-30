#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <sokol_gfx.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    UniformBlock_Material = 0,
    UniformBlock_Geometry,
    UniformBlock_Instance,
    _UniformBlock_Count,
};

sg_shader_desc texture_debug_shader_desc(char const* vs_src, char const* fs_src);
sg_pipeline_desc texture_debug_pipeline_desc(sg_shader shader);
sg_image_desc texture_debug_matcap_image_desc(void const* data, int width, int height);
sg_sampler_desc texture_debug_matcap_sampler_desc(void);

sg_buffer_desc mesh_vertex_buffer_desc(size_t size);
sg_buffer_desc mesh_index_buffer_desc(size_t size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // GRAPHICS_H
