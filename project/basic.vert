#version 420

layout(location = 0) in vec4 in_data;

uniform mat4 projectionMatrix;

//out vec4 data;
out float lifetime;

void main()
{
	gl_Position = projectionMatrix * vec4(in_data.xyz, 1.0);
	lifetime = in_data.w;
}
