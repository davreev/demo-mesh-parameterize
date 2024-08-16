#version 330 core

uniform sampler2D u_matcap;

uniform float u_tex_scale;

in vec3 v_view_normal;
in vec2 v_tex_coord;

out vec4 f_color;

float checker(vec2 p)
{
    p = floor(p);
    return float(int(p.x + p.y) & 1);
}

vec3 proc_tex(vec2 p)
{
    const vec3 col_a = vec3(1.0);
    const vec3 col_b = vec3(0.8);
    return mix(col_a, col_b, checker(p));
}

vec3 proc_tex_aa(vec2 p)
{
    // NOTE(dr): Filter procedural texture using screen space stencil centered at p
    vec2 dp_x = 0.25 * dFdx(p);
    vec2 dp_y = 0.25 * dFdy(p);
    vec3 col = proc_tex(p);
    col += proc_tex(p - dp_x);
    col += proc_tex(p + dp_x);
    col += proc_tex(p - dp_y);
    col += proc_tex(p + dp_y);
    return col * 0.2;
}

float luminance(vec3 col)
{
    // https://en.wikipedia.org/wiki/Relative_luminance
    const vec3 coeffs = vec3(0.2126, 0.7152, 0.0722);
    return dot(col, coeffs);
}

vec3 matcap_shade(vec3 base_color, vec3 view_normal, float intensity, float neutral)
{
    vec2 uv = view_normal.xy * 0.5 + 0.5;
    float offset = luminance(textureLod(u_matcap, uv, 0.0).rgb) - neutral;
    return base_color + offset * intensity;
}

void main() 
{
    vec3 base_col = proc_tex_aa(v_tex_coord / u_tex_scale);
    vec3 col = matcap_shade(base_col, normalize(v_view_normal), 1.3, 0.95);

    if(gl_FrontFacing)
        col *= 0.5;

    f_color = vec4(col, 1.0);
}
