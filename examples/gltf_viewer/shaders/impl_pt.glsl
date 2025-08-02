#ifndef GLTF_IMPL_PT_GLSL
#define GLTF_IMPL_PT_GLSL

vec4 getBaseColor() {
    vec4 vColor = instance.color0 * u + instance.color0 * v + instance.color0 * w;

    vec4 color = vColor * MATERIAL.baseColor;
    if (BASE_COLOR_TEX_INFO.index != -1){
        vec2 uv = transformUV(BASE_COLOR_TEX_INFO);
        vec4 texColor = texture(BASE_COLOR_TEXTURE, uv);
        texColor.rgb = pow(texColor.rgb, vec3(2.2));
        color *= texColor;
    }

    return color;
}

vec3 getMRO() {
    vec3 mro;
    mro.r = MATERIAL.metalness;
    mro.g = MATERIAL.roughness;
    mro.b = 1;


    if(METAL_ROUGHNESS_TEX_INFO.index != -1) {
        vec2 uv = transformUV(METAL_ROUGHNESS_TEX_INFO);
        vec3 res = texture(METAL_ROUGHNESS_TEXTURE, uv).rgb;
        mro.r *= res.b;
        mro.g *= res.g;
    }

    if(OCCLUSION_TEX_INFO.index != -1) {
        vec2 uv = transformUV(OCCLUSION_TEX_INFO);
        mro.b = texture(OCCLUSION_TEXTURE, uv).r;
    }
    return mro;
}

vec3 getEmission(){
    vec3 emission = MATERIAL.emission * MATERIAL.emissiveStrength;
    if(EMISSION_TEX_INFO.index != -1) {
        vec2 uv = transformUV(EMISSION_TEX_INFO);
        emission *= pow(texture(EMISSION_TEXTURE, uv).rgb, vec3(2.2));
    }
    return emission;
}

vec2 transformUV(TextureInfo ti, vec4 uv) {
    if(ti.index == -1) return uv.xy;

    mat3 translation = mat3(1,0,0, 0,1,0, ti.offset.x, ti.offset.y, 1);
    mat3 rotation = mat3(
    cos(ti.rotation), -sin(ti.rotation), 0,
    sin(ti.rotation), cos(ti.rotation), 0,
    0,             0, 1
    );
    mat3 scale = mat3(ti.scale.x,0,0, 0,ti.scale.y,0, 0,0,1);

    mat3 matrix = translation * rotation * scale;
    return ( matrix * vec3(ti.texCoord == 0 ? uv.xy : uv.zw, 1) ).xy;
}

vec2 transformUV(TextureInfo ti) {
    return transformUV(ti, instance.uv);
}

NormalInfo getNormalInfo() {

    vec2 uv = transformUV(NORMAL_TEX_INFO);
    vec3 n, t, b, ng;

    // Compute geometrical TBN:
    if(hasNormal()){
        if (hasTanget()){
            // Trivial TBN computation, present as vertex attribute.
            // Normalize eigenvectors as matrix is linearly interpolated.
            t = normalize(instance.tangent);
            b = normalize(instance.bitangent);
            ng = normalize(instance.normal);
        } else {
            // Normals are either present as vertex attributes or approximated.
            ng = normalize(instance.normal);
            orthonormalBasis(t, b, n);
        }
    } else {
        vec3 p0 = gl_ObjectToWorld * vec4(gl_HitTriangleVertexPositionsEXT[0], 1);
        vec3 p1 = gl_ObjectToWorld * vec4(gl_HitTriangleVertexPositionsEXT[1], 1);
        vec3 p2 = gl_ObjectToWorld * vec4(gl_HitTriangleVertexPositionsEXT[2], 1);

        vec3 e0 = p1 - p0;
        vec3 e1 = p2 - p0;

        ng = normalize(cross(e0, e1));
        orthonormalBasis(t, b, ng);
    }


    // For a back-facing surface, the tangential basis vectors are negated.
    if (gl_HitKind == gl_HitKindBackFacingTriangle && MATERIAL.doubleSided == 1)
    {
        t *= -1.0;
        b *= -1.0;
        ng *= -1.0;
    }

    // Compute normals:
    NormalInfo info;
    info.Ng = ng;
    if(NORMAL_TEX_INFO.index != -1){
        info.Ntex = texture(NORMAL_TEXTURE, uv).rgb * 2.0 - vec3(1.0);
        info.Ntex *= vec3(NORMAL_TEX_INFO.tScale, NORMAL_TEX_INFO.tScale, 1.0);
        info.Ntex = normalize(info.Ntex);
        info.N = normalize(mat3(t, b, ng) * info.Ntex);
    } else {
        info.N = ng;
    }
    info.T = t;
    info.B = b;
    return info;

}

bool hasTanget() {
    return !all(equal(instance.tangent, vec3(0)));
}

bool hasNormal() {
    return !all(equal(instance.normal, vec3(0)));
}

#endif // GLTF_IMPL_PT_GLSL