#version 330 core
in vec2 uv;
out vec4 result;
uniform sampler2D source;
uniform vec2 direction;
void main()
{
    vec3 c = texture(source,uv).rgb * 0.2270270270;
    c += texture(source,uv+direction*1.3846153846).rgb * 0.3162162162;
    c += texture(source,uv-direction*1.3846153846).rgb * 0.3162162162;
    c += texture(source,uv+direction*3.2307692308).rgb * 0.0702702703;
    c += texture(source,uv-direction*3.2307692308).rgb * 0.0702702703;
    result = vec4(c,1.0);
}
