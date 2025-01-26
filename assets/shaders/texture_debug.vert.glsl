#version 330 core

uniform mat4 u_local_to_clip;
uniform mat4 u_local_to_view;
uniform int u_flatten;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_tex_coord;

out vec3 v_view_normal;
out vec2 v_tex_coord;

void main()
{
    vec3 pos = (u_flatten == 1) ? vec3(a_tex_coord, 0.0) : a_position;
    gl_Position = u_local_to_clip * vec4(pos, 1.0);

    // NOTE(dr): This assumes local_to_view has uniform scaling
    v_view_normal = normalize(mat3(u_local_to_view) * a_normal);
    v_tex_coord = a_tex_coord;
}
