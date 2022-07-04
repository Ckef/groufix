#version 450

layout(location = 0) in vec2 fTexcoord;
layout(location = 0) out vec4 oColor;
layout(set = 0, binding = 0) uniform sampler2D albedo;

void main() {
	oColor = vec4(texture(albedo, fTexcoord).rgb, 1.0);
}
