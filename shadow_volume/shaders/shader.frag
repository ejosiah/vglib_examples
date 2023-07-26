#version 460 core

layout(set = 0, binding = 0) uniform UBO {
    vec3 lightPosition;
    vec3 cameraPosition;
};

layout(location = 0) in struct {
    vec4 color;
    vec3 position;
    vec3 normal;
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fracColor;

vec3 GetColorFromPositionAndNormal( in vec3 worldPosition, in vec3 normal ) {
    const float pi = 3.141519;

    vec3 scaledPos = 2 * worldPosition.xyz * pi * 2.0;
    vec3 scaledPos2 = 2 * worldPosition.xyz * pi * 2.0 / 10.0 + vec3( pi / 4.0 );
    float s = cos( scaledPos2.x ) * cos( scaledPos2.y ) * cos( scaledPos2.z );  // [-1,1] range
    float t = cos( scaledPos.x ) * cos( scaledPos.y ) * cos( scaledPos.z );     // [-1,1] range


    t = ceil( t * 0.9 );
    s = ( ceil( s * 0.9 ) + 3.0 ) * 0.25;
    vec3 colorB = vec3( 0.85, 0.85, 0.85 );
    vec3 colorA = vec3( 1, 1, 1 );
    vec3 finalColor = mix( colorA, colorB, t ) * s;

    return vec3(0.8) * finalColor;
}


void main(){
//    vec2 id = floor(fs_in.uv * 8);
//    vec2 id = floor(fs_in.position.xy);
//    float c = step(1, mod(id.x + id.y, 2));
//    vec3 color = mix(vec3(0), vec3(0.352, 0.335, 0.277), c);
    vec3 diffuse = GetColorFromPositionAndNormal(fs_in.position, fs_in.normal);


    vec3 N = normalize(fs_in.normal);
    vec3 L = normalize(lightPosition - fs_in.position);
    vec3 E = normalize(cameraPosition - fs_in.position);
    vec3 H = normalize(E + L);


    vec3 lightColor = vec3(1);
    vec3 color = lightColor * diffuse * max(0, dot(N, L));
    color += lightColor * pow(max(dot(H, N), 0), 250);

    fracColor = vec4(color, 1);
}