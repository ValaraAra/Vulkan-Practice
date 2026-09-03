#version 460

// Counter-clockwise (top, bottom left, bottom right)
const vec3 positions[3] = vec3[]
(
	vec3( 0.0, -0.5, 0.0),
	vec3(-0.5,  0.5, 0.0),
	vec3( 0.5,  0.5, 0.0)
);

// Classic RGB
const vec3 colors[3] = vec3[]
(
	vec3(1.0, 0.0, 0.0),
	vec3(0.0, 1.0, 0.0),
	vec3(0.0, 0.0, 1.0)
);

layout (location = 0) out vec3 outColor;

void main()
{
	gl_Position = vec4(positions[gl_VertexIndex], 1.0);
	outColor = colors[gl_VertexIndex];
}