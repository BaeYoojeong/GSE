#version 330 core
in vec2 uv;
out vec4 result;
uniform sampler2D sceneImage;
uniform sampler2D bloomImages[4];
uniform sampler2D softImage;
uniform int enabled;
uniform float exposure;
uniform float bloomStrength;
uniform float vignetteStrength;
uniform vec2 vignetteRange;
uniform float edgeStrength;
uniform vec2 edgeRange;
uniform float fade;
vec3 filmic(vec3 x)
{
    return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
vec3 linearToSRGB(vec3 x)
{
    x = max(x,vec3(0.0));
    return mix(1.055*pow(x,vec3(1.0/2.4))-0.055,12.92*x,lessThanEqual(x,vec3(0.0031308)));
}
void main()
{
    // Preserve pixel-art edges; blur and bloom are sampled smoothly and independently.
    ivec2 dimensions=textureSize(sceneImage,0);
    ivec2 pixel=clamp(ivec2(uv*vec2(dimensions)),ivec2(0),dimensions-ivec2(1));
    vec3 c = texelFetch(sceneImage,pixel,0).rgb;
    float radial = length(uv*2.0-1.0);
    if(enabled!=0)
    {
        // Elliptical falloff in viewport coordinates, excluding letterbox bars.
        float edge = smoothstep(edgeRange.x,edgeRange.y,radial);
        if(edgeStrength>0.0)
            c = mix(c,texture(softImage,uv).rgb,clamp(edge*edgeStrength,0.0,1.0));
        if(bloomStrength>0.0)
        {
            // Favor a tight halo over the broad levels that wash out dark silhouettes.
            // Constant sampler indices are compatible with GLSL 3.30.
            vec3 bloom = texture(bloomImages[0],uv).rgb * 0.68;
            bloom += texture(bloomImages[1],uv).rgb * 0.23;
            bloom += texture(bloomImages[2],uv).rgb * 0.07;
            bloom += texture(bloomImages[3],uv).rgb * 0.02;
            c += bloom * bloomStrength;
        }
    }
    // HDR always needs a display transform, even with the optional cinematic effects disabled.
    c = filmic(max(c, vec3(0.0)) * exposure);
    if(enabled!=0)
    {
        float vignette = smoothstep(vignetteRange.x, vignetteRange.y, radial);
        c *= 1.0-clamp(vignetteStrength,0.0,0.9)*vignette;
    }
    // Explicit SDR output transform; GL_FRAMEBUFFER_SRGB is disabled.
    result = vec4(clamp(linearToSRGB(c),0.0,1.0)*fade,1.0);
}
