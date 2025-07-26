#ifndef GLTF_IMPL_GLSL
#define GLTF_IMPL_GLSL

float saturate(float x) {
    return clamp(x, 0, 1);
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
    if (gl_FrontFacing == false && MATERIAL.doubleSided == 1)
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

vec3 getEmission(){
    vec3 emission = MATERIAL.emission * MATERIAL.emissiveStrength;
    if(EMISSION_TEX_INFO.index != -1) {
        vec2 uv = transformUV(EMISSION_TEX_INFO);
        emission *= pow(texture(EMISSION_TEXTURE, uv).rgb, vec3(2.2));
    }
    return emission;
}

float getTransmissionFactor() {
    float transmission = MATERIAL.transmission;
    if(TRANSMISSION_TEX_INFO.index == -1) return transmission;

    vec2 uv = transformUV(TRANSMISSION_TEX_INFO);
    transmission *= texture(TRANSMISSION_TEXTURE, uv).r;

    return transmission;
}

float getThickness() {
    float thickness = MATERIAL.thickness;
    if(THICKNESS_TEX_INFO.index != -1){
        vec2 uv = transformUV(THICKNESS_TEX_INFO);
        thickness *= texture(THICKNESS_TEXTURE, uv).g;
    }
    return thickness;
}

bool isNull(Material material) {
    return any(isnan(material.baseColor));
}

ClearCoat getClearCoat() {
    ClearCoat cc = newClearCoatInstance();
    vec2 uv;

    if(MATERIAL.clearCoatFactor == 0) return cc;
    cc.factor = MATERIAL.clearCoatFactor;
    cc.roughness = MATERIAL.clearCoatRoughnessFactor;
    cc.f0 = vec3(pow((MATERIAL.ior - 1.0) / (MATERIAL.ior + 1.0), 2.0));
    cc.f90 = vec3(1);
    cc.normal = ni.N;

    if(CLEAR_COAT_TEX_INFO.index != -1) {
        uv = transformUV(CLEAR_COAT_TEX_INFO);
        cc.factor *= texture(CLEAR_COAT_TEXTURE, uv).r;
    }

    if(CLEAR_COAT_ROUGHNESS_TEX_INFO.index != -1) {
        uv = transformUV(CLEAR_COAT_ROUGHNESS_TEX_INFO);
        cc.roughness *= texture(CLEAR_COAT_ROUGHNESS_TEXTURE, uv).g;
    }


    if(CLEAR_COAT_NORMAL_TEX_INFO.index != -1) {
        uv = transformUV(CLEAR_COAT_NORMAL_TEX_INFO);
        mat3 TBN = mat3(ni.T, ni.B, ni.N);
        cc.normal = 2 * texture(CLEAR_COAT_NORMAL_TEXTURE, uv).xyz - 1;
        cc.normal = normalize(TBN * cc.normal);
    }

    cc.normal = normalize(cc.normal);
    cc.roughness = clamp(cc.roughness, 0, 1);
    cc.enabled = cc.factor != 0;

    return cc;
}

Sheen getSheen() {
    Sheen sheen = newSheenInstance();
    sheen.color = MATERIAL.sheenColorFactor;
    sheen.roughness = MATERIAL.sheenRoughnessFactor;

    if(SHEEN_COLOR_TEX_INFO.index != -1) {
        vec2 uv = transformUV(SHEEN_COLOR_TEX_INFO);
        sheen.color *= pow(texture(SHEEN_COLOR_TEXTURE, uv).rgb, vec3(2.2));
    }

    if(SHEEN_ROUGHNESS_TEX_INFO.index != -1) {
        vec2 uv = transformUV(SHEEN_ROUGHNESS_TEX_INFO);
        sheen.roughness *= texture(SHEEN_ROUGHNESS_TEXTURE, uv).a;
    }

    sheen.enabled = all(notEqual(sheen.color, vec3(0))) || sheen.roughness != 0;

    return sheen;
}

Anisotropy getAnisotropy() {
    Anisotropy anisotropy = newAnisotropyInstance();

    float strength = MATERIAL.anisotropyStrength;
    vec2 rotationDirection = MATERIAL.anisotropyRotation;

    vec2 direction = vec2(1, 0);

    if(ANISOTROPY_TEX_INFO.index != -1){
        vec2 uv = transformUV(ANISOTROPY_TEX_INFO);
        vec3 anisotropySample = texture(ANISOTROPY_TEXTURE, uv).xyz;
        direction = 2 * anisotropySample.xy - 1;
        strength *= anisotropySample.z;
    }

    mat2 rotator = mat2(rotationDirection.x, rotationDirection.y, -rotationDirection.y, rotationDirection.x);
    direction = rotator * normalize(direction);

    anisotropy.tangent = normalize(mat3(ni.T, ni.B, ni.Ng) * vec3(direction, 0.0));
    anisotropy.bitangent = normalize(cross(ni.Ng, anisotropy.tangent));
    anisotropy.strength = clamp(strength, 0, 1);

    anisotropy.enabled = strength > 0;
    return anisotropy;
}

Specular getSpecular() {
    Specular specular = newSpecluarInstance();

    specular.color = MATERIAL.specularColor;
    specular.factor = MATERIAL.specularFactor;

    if(SPECULAR_STRENGTH_TEX_INFO.index != -1) {
        vec2 uv = transformUV(SPECULAR_STRENGTH_TEX_INFO);
        specular.factor *= texture(SPECULAR_STRENGTH_TEXTURE, uv).a;
    }

    if(SPECULAR_COLOR_TEX_INFO.index != -1){
        vec2 uv = transformUV(SPECULAR_COLOR_TEX_INFO);
        specular.color *= texture(SPECULAR_COLOR_TEXTURE, uv).rgb;
    }
    return specular;
}

Iridescence getIridescence() {
    Iridescence iri = newIridescencInstance();

    iri.factor = MATERIAL.iridescenceFactor;
    iri.ior = MATERIAL.iridescenceIor;
    iri.thickness = MATERIAL.iridescenceThicknessMaximum;

    if(IRIDESCENCE_TEX_INFO.index != -1) {
        vec2 uv = transformUV(IRIDESCENCE_TEX_INFO);
        iri.factor *= texture(IRIDESCENCE_TEXTURE, uv).r;
    }

    if(IRIDESCENCE_THICKNESS_TEX_INFO.index != -1){
        vec2 uv = transformUV(IRIDESCENCE_THICKNESS_TEX_INFO);
        float g = texture(IRIDESCENCE_THICKNESS_TEXTURE, uv).g;
        iri.thickness = mix(MATERIAL.iridescenceThicknessMinimum, iri.thickness, g);
    }

    iri.enabled = iri.factor > 0;

    return iri;
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

#endif // GLTF_IMPL_GLSL