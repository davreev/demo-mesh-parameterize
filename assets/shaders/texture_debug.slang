import "modules/common.slang";

struct PassParams
{
    float4x4 world_to_view;
    float4x4 world_to_clip;
}

struct MaterialParams
{
    float tex_scale;
}

struct ObjectParams
{
    float4x4 local_to_world;
    int flatten;
}

ConstantBuffer<PassParams> pass;
ConstantBuffer<MaterialParams> material;
ConstantBuffer<ObjectParams> object;

static float4x4 local_to_view = mul(pass.world_to_view, object.local_to_world);
static float4x4 local_to_clip = mul(pass.world_to_clip, object.local_to_world);

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
    const float3 pos = select(bool(object.flatten), float3(v.tex_coord, 0.0), v.position);

    // NOTE(dr): This assumes local_to_view has uniform scaling
    view_normal = normalize(mul(local_to_view, float4(v.normal, 0.0)).xyz);
    tex_coord = v.tex_coord;

    return mul(local_to_clip, float4(pos, 1.0));
}

float checker_aa(float2 p)
{
    let x = abs(fract(p.x) - 0.5) - 0.25;
    let sd_x = x / fwidth(p.x);

    let y = abs(fract(p.y) - 0.5) - 0.25;
    let sd_y = y / fwidth(p.y);

    static const float w = 1.0;
    let a = smoothstep(-w, w, sd_x);
    let b = smoothstep(-w, w, sd_y);
    return max(min(a, b), min(1.0 - a, 1.0 - b));
}

float3 debug_tex(float2 p)
{
    static const float3 col_a = float3(0.8);
    static const float3 col_b = float3(1.0);
    return lerp(col_a, col_b, checker_aa(p));
}

[shader("fragment")]
float4 fragment_main(float3 view_normal, float2 tex_coord, bool is_front_face: SV_IsFrontFace) :
    SV_Target
{
    float4 col = { debug_tex(tex_coord / material.tex_scale), 1.0 };
    apply_matcap_shading(matcap, normalize(view_normal), 1.2, 0.95, col);

    if (is_front_face)
        col.rgb *= 0.5;

    return col;
}
