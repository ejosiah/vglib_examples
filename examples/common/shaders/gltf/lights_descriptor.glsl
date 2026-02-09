#ifndef LIGHTS_DESCRIPTOR_GLSL
#define LIGHTS_DESCRIPTOR_GLSL

#ifndef MATERIAL_SET
#define MATERIAL_SET 1
#define LIGHT_BINDING_POINT 1
#define LIGHT_INSTANCE_BINDING_POINT 2
#endif

#include "punctual_lights.glsl"

layout(set = MATERIAL_SET, binding = LIGHT_BINDING_POINT, scalar) buffer PunctualLights {
    Light lights[];
};


layout(set = MATERIAL_SET, binding = LIGHT_INSTANCE_BINDING_POINT, scalar) buffer PunctualLightsInstances {
    LightInstance lightInstances[];
};

Light lightAt(int index) {
    LightInstance instance = lightInstances[index];
    Light light = lights[instance.lightId];

    light.direction = (instance.model * vec4(light.direction, 1)).xyz;
    light.position = (instance.model * vec4(0, 0, 0, 1)).xyz;
    return light;
}


#endif // LIGHTS_DESCRIPTOR_GLSL