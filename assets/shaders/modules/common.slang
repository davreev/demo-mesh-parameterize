
float luminance(float3 col)
{
    // https://en.wikipedia.org/wiki/Relative_luminance
    static const float3 coeffs = { 0.2126, 0.7152, 0.0722 };
    return dot(col, coeffs);
}

float4 sample_matcap(Sampler2D sampler, float3 view_normal)
{
    const float2 uv = view_normal.xy * 0.5 + 0.5;
    return sampler.SampleLevel(uv, 0.0);
}

void apply_matcap_shading(
    Sampler2D sampler,
    float3 view_normal,
    float intensity,
    float neutral,
    inout float4 col)
{
    const float bright = luminance(sample_matcap(sampler, view_normal).rgb);
    col += (bright - neutral) * intensity;
}

float checker(float2 p)
{
    p = floor(p);
    return float(int(p.x + p.y) & 1);
}
