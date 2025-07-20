import "modules/common.slang";

struct Material
{
    float tex_scale;
}
ConstantBuffer<Material> material;

struct Object
{
    float4x4 local_to_clip;
    float4x4 local_to_view;
    int flatten;
}
ConstantBuffer<Object> object;

Sampler2D matcap;

struct Vertex
{
    float3 position;
    float3 normal;
    float2 tex_coord;
}

[shader("vertex")]
float4 vertex_main(Vertex v, out float3 view_normal, out float2 tex_coord) : SV_Position
{
    float3 pos = select(bool(object.flatten), float3(v.tex_coord, 0.0), v.position);

    // NOTE(dr): This assumes local_to_view has uniform scaling
    view_normal = normalize(mul(float3x3(object.local_to_view), v.normal));
    tex_coord = v.tex_coord;

    return mul(object.local_to_clip, { pos, 1.0 });
}

float3 proc_tex(float2 p)
{
    static const float3 col_a = float3(1.0);
    static const float3 col_b = float3(0.8);
    return lerp(col_a, col_b, checker(p));
}

float3 proc_tex_aa(float2 p)
{
    // TODO(dr): Could approximate integral of proc_tex over pixel in screenspace?

    // NOTE(dr): Filter procedural texture using screen space stencil centered at p
    float2 dp_x = 0.25 * ddx(p);
    float2 dp_y = 0.25 * ddy(p);
    float3 col = proc_tex(p);
    col += proc_tex(p - dp_x);
    col += proc_tex(p + dp_x);
    col += proc_tex(p - dp_y);
    col += proc_tex(p + dp_y);
    return col * 0.2;
}

float3 matcap_shade(float3 base_color, float3 view_normal, float intensity, float neutral)
{
    float2 uv = view_normal.xy * 0.5 + 0.5;
    float offset = luminance(matcap.SampleLevel(uv, 0.0).rgb) - neutral;
    return base_color + offset * intensity;
}

[shader("fragment")]
float4 fragment_main(float3 view_normal, float2 tex_coord, bool is_front_face: SV_IsFrontFace) :
    SV_Target
{
    float3 base_col = proc_tex_aa(tex_coord / material.tex_scale);
    float3 col = matcap_shade(base_col, normalize(view_normal), 1.2, 0.95);

    if (is_front_face)
        col *= 0.5;

    return { col, 1.0 };
}
