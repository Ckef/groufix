#version 450

layout(location = 0) in vec3 fNormal;
layout(location = 0) out vec4 oColor;

void main() {
	oColor = vec4(fNormal, 1.0);
}
