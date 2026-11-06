@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
    mat4 model;
};

in vec3 position;
in vec3 normal;
in vec4 color0;

out vec3 v_normal;
out vec4 v_color;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    v_normal = (model * vec4(normal, 0.0)).xyz;
    v_color = color0;
}
@end

@fs fs
in vec3 v_normal;
in vec4 v_color;
out vec4 frag_color;

void main() {
    vec3 n = normalize(v_normal);
    vec3 l = normalize(vec3(0.4, 1.0, 0.6));
    float diffuse = max(dot(n, l), 0.0);
    frag_color = vec4(v_color.rgb * (0.35 + 0.65 * diffuse), 1.0);
}
@end

@program model vs fs
