#version 330

in vec3 a_Position;
uniform vec4 u_Trans;

void main()
{
	vec4 newPosition;
	vec2 rectSize = (u_Trans.z == 0.0) ? vec2(u_Trans.w) : u_Trans.zw;
	newPosition.xy = a_Position.xy*rectSize + u_Trans.xy;
	newPosition.z = 0;
	newPosition.w= 1;
	gl_Position = newPosition;
}
