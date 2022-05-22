#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vPosition;
layout(location = 2) in vec2 vTexcoord;
layout(location = 0) out vec3 fNormal;

out gl_PerVertex {
	vec4 gl_Position;
};

layout(push_constant) uniform Constants {
	mat4 mvp;
};

void main() {
	gl_Position = vec4(vPosition, 1.0);
	fNormal = vNormal;
}
