#ifndef LTC_COMMON_GLSL
#define LTC_COMMON_GLSL

const float LUT_SIZE  = 64.0;
const float LUT_SCALE = (LUT_SIZE - 1.0)/LUT_SIZE;
const float LUT_BIAS  = 0.5/LUT_SIZE;

const float pi = 3.14159265;

vec3 FetchDiffuseFilteredTexture(sampler2D texLightFiltered, vec3 p1_, vec3 p2_, vec3 p3_, vec3 p4_)
{
    // area light plane basis
    vec3 V1 = p2_ - p1_;
    vec3 V2 = p4_ - p1_;
    vec3 planeOrtho = (cross(V1, V2));
    float planeAreaSquared = dot(planeOrtho, planeOrtho);
    float planeDistxPlaneArea = dot(planeOrtho, p1_);
    // orthonormal projection of (0,0,0) in area light space
    vec3 P = planeDistxPlaneArea * planeOrtho / planeAreaSquared - p1_;

    // find tex coords of P
    float dot_V1_V2 = dot(V1,V2);
    float inv_dot_V1_V1 = 1.0 / dot(V1, V1);
    vec3 V2_ = V2 - V1 * dot_V1_V2 * inv_dot_V1_V1;
    vec2 Puv;
    Puv.y = dot(V2_, P) / dot(V2_, V2_);
    Puv.x = dot(V1, P)*inv_dot_V1_V1 - dot_V1_V2*inv_dot_V1_V1*Puv.y ;

    // LOD
    float d = abs(planeDistxPlaneArea) / pow(planeAreaSquared, 0.75);

    return textureLod(texLightFiltered, vec2(0.125, 0.125) + 0.75 * Puv, log(2048.0*d)/log(3.0) ).rgb;
}

vec2 LTC_Coords(float cosTheta, float roughness){
    float theta = acos(cosTheta);
    vec2 coords = vec2(roughness, theta/(0.5*3.14159));

    const float LUT_SIZE = 32.0;
    // scale and bias coordinates, for correct filtered lookup
    coords = coords*(LUT_SIZE - 1.0)/LUT_SIZE + 0.5/LUT_SIZE;

    return coords;
}

mat3 LTC_Matrix(sampler2D texLSDMat, vec2 uv){
    vec4 t = texture(texLSDMat, uv);
    mat3 Minv = mat3(
        vec3(1, 0, t.y),
        vec3(0, t.z, 0),
        vec3(t.w, 0, t.x)
    );

    return Minv;
}

vec3 mul(mat3 m, vec3 v){
    return m * v;
}

mat3 mul(mat3 m1, mat3 m2){
    return m1 * m2;
}

float IntegrateEdge(vec3 v1, vec3 v2){
    float cosTheta = dot(v1, v2);
    float theta = acos(cosTheta);
    float res = cross(v1, v2).z * ((theta > 0.001) ? theta/sin(theta) : 1.0);

    return res;
}

void ClipQuadToHorizon(inout vec3 L[5], out int n){
    // detect clipping config
    int config = 0;
    if (L[0].z > 0.0) config += 1;
    if (L[1].z > 0.0) config += 2;
    if (L[2].z > 0.0) config += 4;
    if (L[3].z > 0.0) config += 8;

    // clip
    n = 0;

    if (config == 0)
    {
        // clip all
    }
    else if (config == 1)// V1 clip V2 V3 V4
    {
        n = 3;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 2)// V2 clip V1 V3 V4
    {
        n = 3;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 3)// V1 V2 clip V3 V4
    {
        n = 4;
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
        L[3] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 4)// V3 clip V1 V2 V4
    {
        n = 3;
        L[0] = -L[3].z * L[2] + L[2].z * L[3];
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
    }
    else if (config == 5)// V1 V3 clip V2 V4) impossible
    {
        n = 0;
    }
    else if (config == 6)// V2 V3 clip V1 V4
    {
        n = 4;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 7)// V1 V2 V3 clip V4
    {
        n = 5;
        L[4] = -L[3].z * L[0] + L[0].z * L[3];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 8)// V4 clip V1 V2 V3
    {
        n = 3;
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
        L[1] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] =  L[3];
    }
    else if (config == 9)// V1 V4 clip V2 V3
    {
        n = 4;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[2].z * L[3] + L[3].z * L[2];
    }
    else if (config == 10)// V2 V4 clip V1 V3) impossible
    {
        n = 0;
    }
    else if (config == 11)// V1 V2 V4 clip V3
    {
        n = 5;
        L[4] = L[3];
        L[3] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 12)// V3 V4 clip V1 V2
    {
        n = 4;
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
    }
    else if (config == 13)// V1 V3 V4 clip V2
    {
        n = 5;
        L[4] = L[3];
        L[3] = L[2];
        L[2] = -L[1].z * L[2] + L[2].z * L[1];
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
    }
    else if (config == 14)// V2 V3 V4 clip V1
    {
        n = 5;
        L[4] = -L[0].z * L[3] + L[3].z * L[0];
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
    }
    else if (config == 15)// V1 V2 V3 V4
    {
        n = 4;
    }

    if (n == 3)
    L[3] = L[0];
    if (n == 4)
    L[4] = L[0];
}

vec3 LTC_Evaluate(vec3 N, vec3 V, vec3 P, mat3 Minv, vec3 points[4], bool twoSided){
    // construct orthonormal basis around N
    vec3 T1, T2;
    T1 = normalize(V - N*dot(V, N));
    T2 = cross(N, T1);

    // rotate area light in (T1, T2, N) basis
    Minv = mul(Minv, transpose(mat3(T1, T2, N)));

    // polygon (allocate 5 vertices for clipping)
    vec3 L[5];
    L[0] = mul(Minv, points[0] - P);
    L[1] = mul(Minv, points[1] - P);
    L[2] = mul(Minv, points[2] - P);
    L[3] = mul(Minv, points[3] - P);

    int n;
    ClipQuadToHorizon(L, n);

    if (n == 0)
    return vec3(0, 0, 0);

    // project onto sphere
    L[0] = normalize(L[0]);
    L[1] = normalize(L[1]);
    L[2] = normalize(L[2]);
    L[3] = normalize(L[3]);
    L[4] = normalize(L[4]);

    // integrate
    float sum = 0.0;

    sum += IntegrateEdge(L[0], L[1]);
    sum += IntegrateEdge(L[1], L[2]);
    sum += IntegrateEdge(L[2], L[3]);
    if (n >= 4)
    sum += IntegrateEdge(L[3], L[4]);
    if (n == 5)
    sum += IntegrateEdge(L[4], L[0]);

    sum = twoSided ? abs(sum) : max(0.0, sum);

    vec3 Lo_i = vec3(sum, sum, sum);

    return Lo_i;
}

vec3 LTC_Evaluate(vec3 N, vec3 V, vec3 P, mat3 Minv, vec3 points[4], bool twoSided, sampler2D texFilteredMap){
    // construct orthonormal basis around N
    vec3 T1, T2;
    T1 = normalize(V - N*dot(V, N));
    T2 = cross(N, T1);

    // rotate area light in (T1, T2, R) basis
    Minv = mul(Minv, mat3(T1, T2, N));

    // polygon (allocate 5 vertices for clipping)
    vec3 L[5];
    L[0] = mul(Minv, points[0].xyz - P);
    L[1] = mul(Minv, points[1].xyz - P);
    L[2] = mul(Minv, points[2].xyz - P);
    L[3] = mul(Minv, points[3].xyz - P);
    L[4] = L[3]; // avoid warning

    vec3 textureLight = FetchDiffuseFilteredTexture(texFilteredMap, L[0], L[1], L[2], L[3]);

    int n;
    ClipQuadToHorizon(L, n);

    if (n == 0)
    return vec3(0, 0, 0);

    // project onto sphere
    L[0] = normalize(L[0]);
    L[1] = normalize(L[1]);
    L[2] = normalize(L[2]);
    L[3] = normalize(L[3]);
    L[4] = normalize(L[4]);

    // integrate
    float sum = 0.0;

    sum += IntegrateEdge(L[0], L[1]);
    sum += IntegrateEdge(L[1], L[2]);
    sum += IntegrateEdge(L[2], L[3]);
    if (n >= 4)
    sum += IntegrateEdge(L[3], L[4]);
    if (n == 5)
    sum += IntegrateEdge(L[4], L[0]);

    // note: negated due to winding order
    sum = twoSided ? abs(sum) : max(0.0, -sum);

    vec3 Lo_i = vec3(sum, sum, sum);

    // scale by filtered light color
    Lo_i *= textureLight;

    return Lo_i;
}

#endif // LTC_COMMON_GLSL