#version 330 core

in vec2 uv;
out vec4 result;
uniform sampler2D source;

void main()
{
    // Normalized tent filter: continuous, symmetric diffusion with no added energy per level.
    vec2 d = 1.0 / vec2(textureSize(source, 0));
    vec3 c = texture(source, uv).rgb * 4.0;
    c += texture(source, uv + vec2(d.x, 0.0)).rgb * 2.0;
    c += texture(source, uv - vec2(d.x, 0.0)).rgb * 2.0;
    c += texture(source, uv + vec2(0.0, d.y)).rgb * 2.0;
    c += texture(source, uv - vec2(0.0, d.y)).rgb * 2.0;
    c += texture(source, uv + vec2(d.x, d.y)).rgb;
    c += texture(source, uv + vec2(-d.x, d.y)).rgb;
    c += texture(source, uv + vec2(d.x, -d.y)).rgb;
    c += texture(source, uv - d).rgb;
    result = vec4(c * (1.0 / 16.0), 1.0);
}
