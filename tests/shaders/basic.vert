#version 450

layout(location = 0) in vec3 vPosition;
layout(location = 1) in vec2 vTexcoord;
layout(location = 0) out vec2 fTexcoord;

out gl_PerVertex {
	vec4 gl_Position;
};

layout(row_major, push_constant) uniform Constants {
	mat4 model;
	mat4 projection;
};

void main() {
	gl_Position = projection * model * vec4(vPosition, 1.0);
	fTexcoord = vTexcoord;
}
