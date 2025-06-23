#ifndef GLSL_GLTF_IMPL
#define GLSL_GLTF_IMPL

NormalInfo getNormalInfo() {

    vec2 uv = transformUV(NORMAL_TEX_INFO);
    vec2 uv_dx = dFdx(uv);
    vec2 uv_dy = dFdy(uv);

    if (length(uv_dx) <= 1e-2) {
        uv_dx = vec2(1.0, 0.0);
    }

    if (length(uv_dy) <= 1e-2) {
        uv_dy = vec2(0.0, 1.0);
    }

    vec3 t_ = (uv_dy.t * dFdx(fs_in.position) - uv_dx.t * dFdy(fs_in.position)) /
    (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);

    vec3 n, t, b, ng;

    // Compute geometrical TBN:
    if(hasNormal()){
        if (hasTanget()){
            // Trivial TBN computation, present as vertex attribute.
            // Normalize eigenvectors as matrix is linearly interpolated.
            t = normalize(fs_in.tangent);
            b = normalize(fs_in.bitangent);
            ng = normalize(fs_in.normal);
        } else {
            // Normals are either present as vertex attributes or approximated.
            ng = normalize(fs_in.normal);
            t = normalize(t_ - ng * dot(ng, t_));
            b = cross(ng, t);
        }
    } else {
        ng = normalize(cross(dFdx(fs_in.position), dFdy(fs_in.position)));
        t = normalize(t_ - ng * dot(ng, t_));
        b = cross(ng, t);
    }


    // For a back-facing surface, the tangential basis vectors are negated.
    if (gl_FrontFacing == false)
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
    return !all(equal(fs_in.tangent, vec3(0)));
}

bool hasNormal() {
    return !all(equal(fs_in.normal, vec3(0)));
}

vec2 transformUV(TextureInfo ti) {
    if(ti.index == -1) return fs_in.uv[0];

    mat3 translation = mat3(1,0,0, 0,1,0, ti.offset.x, ti.offset.y, 1);
    mat3 rotation = mat3(
    cos(ti.rotation), -sin(ti.rotation), 0,
    sin(ti.rotation), cos(ti.rotation), 0,
    0,             0, 1
    );
    mat3 scale = mat3(ti.scale.x,0,0, 0,ti.scale.y,0, 0,0,1);

    mat3 matrix = translation * rotation * scale;
    return ( matrix * vec3(fs_in.uv[ti.texCoord], 1) ).xy;
}

vec4 getBaseColor() {
    vec4 color = fs_in.color * MATERIAL.baseColor;
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


#endif // GLSL_GLTF_IMPL