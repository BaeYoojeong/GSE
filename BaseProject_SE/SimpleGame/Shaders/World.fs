#version 330 core
in vec4 color;
in vec2 texUV;
in float lightEnergy;
layout(location = 0) out vec4 result;
layout(location = 1) out vec4 emissive;
uniform sampler2D image;
uniform int useTexture;
uniform int linearOutput;
uniform float fade;

vec3 srgbToLinear(vec3 c)
{
    return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThanEqual(c, vec3(0.04045)));
}

void main()
{
    float a = color.a;
    if (useTexture != 0)
    {
        a *= texture(image, texUV).r;
    }
    vec3 base = srgbToLinear(color.rgb);
    vec3 energy = base * max(lightEnergy - 1.0, 0.0);
    vec3 rgb = linearOutput != 0 ? base + energy : color.rgb;
    result = vec4(rgb * fade, a);
    // Zero-emission opaque objects also occlude lights behind them in this attachment.
    emissive = vec4(linearOutput != 0 ? energy * fade : vec3(0.0), a);
}
