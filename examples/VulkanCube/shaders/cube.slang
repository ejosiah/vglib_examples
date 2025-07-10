import GLSL.std.450;

struct VSInput {
    float3 position : ATTRIB0;
    float3 normal   : ATTRIB1;
};

struct VSOutput {
    float4 position : SV_Position;
};

parameter float4x4 modelViewProj;

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = mul(modelViewProj, float4(input.position, 1.0));
    return output;
}
