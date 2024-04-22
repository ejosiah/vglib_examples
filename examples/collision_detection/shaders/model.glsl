#ifndef MODEL_GLSL
#define MODEL_GLSL

layout(set = 0, binding = 0) buffer CELLIDS {
    int cellID[];
};

layout(set = 0, binding = 1) buffer OBJECT_ATTRIBUTES {
    int id;
    int controlBits;
} object[];

#endif // MODLE_GLSL