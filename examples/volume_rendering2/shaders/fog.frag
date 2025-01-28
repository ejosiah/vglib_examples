#version 460
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "common.glsl"

layout(location = 0) in struct {
    vec3 direction;
} fs_in;

layout(location = 0) out vec4 fragColor;

vec3 extinction = -scene.scattering - scene.absorption;
vec3 albedo = scene.lightColor * scene.scattering/(scene.scattering + scene.absorption);
float isoValue = scene.isoLevel;
float pStepSize = scene.primaryStepSize;
float sStepSize = scene.shadowStepSize;
float cutoff = scene.cutoff;
vec3 lightDir = normalize(scene.lightDirection);



void main() {
    vec3 o = scene.cameraPosition;
    vec3 rd = normalize(fs_in.direction);

    vec3 pTrans = vec3(1);
    vec3 pLumi = vec3(0);
    bool rayTerminated = false;

    TimeSpan pTS;
    if(test(o, rd, info.bmin, info.bmax, pTS)) {
        vec3 wPos = o + rd * pTS.t0;
        ivec3 voxelDim = textureSize(DENSITY_TEXTURE0, 0);
//        int maxDim = max(voxelDim.x, max(voxelDim.y, voxelDim.z));
        int maxDim = scene.numSteps;
        float delta = pStepSize/float(maxDim);

        int pMaxSteps = int(1/delta);

        vec3 start = worldToVoxel(wPos, rd);
        vec3 pos = worldToVoxel(wPos + rd, rd);

        rd = normalize(pos - start) * delta;
        for(int t = 0; t < pMaxSteps; ++t) { // march along ray
            pos = start + rd * t;

            if(outOfBounds(pos)) break;

            float density = sampleDensity(pos);
            if (density < cutoff ) continue;

            pLumi += vec3(0.9, 0.5, 0.01) * sampleEmission(pos);

            vec3 dT = exp(extinction * density * delta);

            // TODO shadow ray
            vec3 sTrans = vec3(1);
            TimeSpan sTS;

            vec3 so = voxelToWorld(pos);
            vec3 sRd = lightDir;
            if(scene.shadow == 1 && test(so, sRd, info.bmin, info.bmax, sTS)) {
                pos = so + sRd * sTS.t0;
                pos = so;

                float sDelta = sStepSize/float(maxDim);
                int sMaxSteps = int(1/sDelta);

                start = worldToVoxel(pos, sRd);
                pos = worldToVoxel(pos + sRd, sRd);
                sRd = normalize(pos - start) * sDelta;

                for(int st = 0; st < sMaxSteps; ++st) {
                    pos = start + sRd * st;
                    density = sampleDensity(pos);

                    if(density < cutoff) continue;

                    sTrans *= exp(extinction * density * sDelta);
//                    if(dot(sTrans, sTrans) < cutoff) {
//                        rayTerminated = true;
//                        break;
//                    }
                }
            }

            pLumi += albedo * sTrans * pTrans * (1 - dT);
            pTrans *= dT;
            if(dot(pTrans, pTrans) < cutoff || rayTerminated) break;
        }

        float alpha = 1 - dot(vec3(1), pTrans/3);
        fragColor = vec4(pLumi, alpha);

        vec4 clipPos = proj * view * model * vec4(wPos, 1);
        clipPos /= clipPos.w;
        gl_FragDepth = clipPos.z;
    } else {
        discard;
    }
}