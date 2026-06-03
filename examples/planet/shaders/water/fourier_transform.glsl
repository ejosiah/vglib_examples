#ifndef WATER_FOURIER_TRANSFORM_GLSL
#define WATER_FOURIER_TRANSFORM_GLSL

#define SIMULATION_RESOLUTION 256
#define BUTTERFLY_PASSES 8u

shared vec3 pingPongArray[4][SIMULATION_RESOLUTION];

void get_butterfly_values(uint passIndex, uint x, out uvec2 indices, out vec2 weights)
{
    uint sectionWidth = 2u << passIndex;
    uint halfSectionWidth = sectionWidth / 2u;

    uint sectionStartOffset = x & ~(sectionWidth - 1u);
    uint halfSectionOffset = x & (halfSectionWidth - 1u);
    uint sectionOffset = x & (sectionWidth - 1u);

    float angle = TWO_PI * float(sectionOffset) / float(sectionWidth);
    weights = vec2(cos(angle), -sin(angle));

    indices.x = sectionStartOffset + halfSectionOffset;
    indices.y = sectionStartOffset + halfSectionOffset + halfSectionWidth;

    if (passIndex == 0u) {
        indices = (reversebits_uvec2(indices) >> (32u - BUTTERFLY_PASSES)) & (SIMULATION_RESOLUTION - 1u);
    }
}

void butterfly_pass(uint passIndex, uint x, uint t0, uint t1, out vec3 resultR, out vec3 resultI)
{
    uvec2 indices;
    vec2 weights;
    get_butterfly_values(passIndex, x, indices, weights);

    vec3 inputR1 = pingPongArray[t0][indices.x];
    vec3 inputI1 = pingPongArray[t1][indices.x];
    vec3 inputR2 = pingPongArray[t0][indices.y];
    vec3 inputI2 = pingPongArray[t1][indices.y];

    resultR = inputR1 + weights.x * inputR2 + weights.y * inputI2;
    resultI = inputI1 - weights.y * inputR2 + weights.x * inputI2;
}

layout(local_size_x = SIMULATION_RESOLUTION, local_size_y = 1, local_size_z = 1) in;

void main()
{
    uvec3 position = gl_GlobalInvocationID;

#ifdef COLUMN_PASS
    ivec2 texturePos = ivec2(position.yx);
    pingPongArray[0][position.x] = imageLoad(FFTRowPassRealBuffer[position.z], texturePos).xyz;
    pingPongArray[1][position.x] = imageLoad(FFTRowPassImaginaryBuffer[position.z], texturePos).xyz;
#else
    ivec2 texturePos = ivec2(position.xy);
    pingPongArray[0][position.x] = imageLoad(DisplacementBuffer[position.z], texturePos).xyz;
    pingPongArray[1][position.x] = imageLoad(HImaginaryBuffer[position.z], texturePos).xyz;
#endif

    uvec4 textureIndices = uvec4(0u, 1u, 2u, 3u);
    for (uint i = 0u; i < BUTTERFLY_PASSES - 1u; ++i) {
        barrier();
        butterfly_pass(i, position.x, textureIndices.x, textureIndices.y,
            pingPongArray[textureIndices.z][position.x], pingPongArray[textureIndices.w][position.x]);
        textureIndices = textureIndices.zwxy;
    }

    barrier();

    vec3 realValue = vec3(0.0);
    vec3 imaginaryValue = vec3(0.0);
    butterfly_pass(BUTTERFLY_PASSES - 1u, position.x, textureIndices.x, textureIndices.y, realValue, imaginaryValue);

#ifdef COLUMN_PASS
    float signCorrectionAndNormalization = ((position.x + position.y) & 0x01u) != 0u ? -1.0 : 1.0;
    imageStore(DisplacementBuffer[position.z], texturePos, vec4(realValue * signCorrectionAndNormalization, 0.0));
#else
    imageStore(FFTRowPassRealBuffer[position.z], texturePos, vec4(realValue, 0.0));
    imageStore(FFTRowPassImaginaryBuffer[position.z], texturePos, vec4(imaginaryValue, 0.0));
#endif
}

#endif // WATER_FOURIER_TRANSFORM_GLSL
