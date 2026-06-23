#version 460

#extension GL_EXT_scalar_block_layout : enable

struct Bounds {
    vec3 min;
    vec3 max;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec3 color;
layout(location = 5) in vec2 uv;

layout(set = 0, binding = 0) uniform sampler3D vector_field_u;
layout(set = 1, binding = 0) uniform sampler3D vector_field_v;
layout(set = 2, binding = 0) uniform sampler3D vector_field_w;
layout(set = 3, binding = 0) uniform sampler3D tempAndDensityField;

layout(set = 4, binding = 0, scalar) buffer Cosntants {
    mat4 worldToVoxel;
    mat4 voxelToWorld;
    Bounds domain;
    Bounds emitterBounds;
    ivec3 resolution;
    vec3 up;
    float ambientTemp;
    float tempSum;
    float minValue;
    float maxValue;
    float tempFactor;
    float densityFactory;
    float smokeDecayFactor;
    float temperatureDecayFactor;
    uint numCells;
};

layout(push_constant) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
};

mat3 rotationFromDirection(vec3 d){
    d = normalize(d);

    float phi   = atan(d.z, d.x);
    float theta = acos(clamp(d.y, -1.0, 1.0));

    float cp = cos(phi);
    float sp = sin(phi);
    float ct = cos(theta);
    float st = sin(theta);

    // Local +Y points along direction
    vec3 xAxis = vec3( sp,      0.0, -cp );
    vec3 yAxis = vec3( cp * st, ct,   sp * st );
    vec3 zAxis = vec3( cp * ct, -st,  sp * ct );

    return mat3(xAxis, yAxis, zAxis);
}

vec3 heatMap(float t) {
    t = clamp(t, 0.0, 0.999999);

    float band = floor(4.0 * t);
    float localT = fract(4.0 * t);

    if(band < 1.0) {
        return vec3(0.0, localT, 1.0);
    }
    if(band < 2.0) {
        return vec3(0.0, 1.0, 1.0 - localT);
    }
    if(band < 3.0) {
        return vec3(localT, 1.0, 0.0);
    }
    return vec3(1.0, 1.0 - localT, 0.0);
}


layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;

void main() {
    ivec3 coord;

    int index = int(gl_InstanceIndex);

    coord.x = int(index) % resolution.x;
    coord.y = (int(index) / resolution.x) % resolution.y;
    coord.z = int(index) / (resolution.x * resolution.y);

    coord;
    vec3 uvw = vec3(coord + 0.5)/vec3(resolution);

    vec4 wPosition = vec4(0);
    vec3 origin = mix(domain.min, domain.max, uvw);
    float density = texelFetch(tempAndDensityField, coord, 0).y;
    if(density > 1e-3) {
        vec3 direction;
        direction.x = texelFetch(vector_field_u, coord, 0).x;
        direction.y = texelFetch(vector_field_v, coord, 0).x;
        direction.z = texelFetch(vector_field_w, coord, 0).x;

        mat3 rotation = rotationFromDirection(direction);

        vec3 invRes = vec3(1)/vec3(resolution);
        float scale = max(invRes.x, max(invRes.y, invRes.z));


        wPosition = model * vec4(origin + (rotation * position) * scale, 1);
    };

    vColor = vec4(heatMap(origin.y/domain.max.y), 1);
    vUv = uv;
    gl_Position = projection * view * wPosition;
}