#version 330 core
in vec2 uv;
out vec4 result;
uniform sampler2D source;
uniform float threshold;
uniform float knee;
vec3 bright(vec3 c)
{
    // A channel maximum keeps saturated amber/red embers from disappearing at the threshold.
    float luminance = max(c.r, max(c.g, c.b));
    float k = max(knee, 0.001);
    float soft = clamp(luminance - threshold + k, 0.0, 2.0*k);
    soft = soft*soft / (4.0*k);
    float contribution = max(luminance-threshold, soft) / max(luminance, 0.0001);
    return c * contribution;
}
void main()
{
    // Extract BEFORE downsampling so small emissive particles retain energy.
    // Exact texels avoid bilinear averaging before the nonlinear threshold, including odd sizes.
    ivec2 last = textureSize(source, 0) - ivec2(1);
    ivec2 p = ivec2(gl_FragCoord.xy) * 2;
    vec3 c = bright(texelFetch(source, min(p, last), 0).rgb);
    c += bright(texelFetch(source, min(p + ivec2(1, 0), last), 0).rgb);
    c += bright(texelFetch(source, min(p + ivec2(0, 1), last), 0).rgb);
    c += bright(texelFetch(source, min(p + ivec2(1, 1), last), 0).rgb);
    result = vec4(c*0.25, 1.0);
}
