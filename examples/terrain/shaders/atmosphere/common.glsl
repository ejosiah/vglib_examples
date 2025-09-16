#ifndef ATMOSPHERE_COMMON_GLSL
#define ATMOSPHERE_COMMON_GLSL

#extension GL_EXT_debug_printf : require

#include "atm_uniforms.glsl"
#include "common_defs.glsl"
#include "common_func.glsl"

#define SYNC_THREADS groupMemoryBarrier(); barrier();
const float T_MAX_MAX = 9000000;

float hgPhase(float g, float cosTheta)
{
    #ifdef USE_CornetteShanks
    return CornetteShanksMiePhaseFunction(g, cosTheta);
    #else
    // Reference implementation (i.e. not schlick approximation).
    // See http://www.pbr-book.org/3ed-2018/Volume_Scattering/Phase_Functions.html
    float numer = 1.0f - g * g;
    float denom = 1.0f + g * g + 2.0f * g * cosTheta;
    return numer / (4.0f * PI * denom * sqrt(denom));
    #endif
}

void UvToLutTransmittanceParams(AtmosphereParameters Atmosphere, out float viewHeight, out float viewZenithCosAngle, in vec2 uv)
{
//    uv = vec2(fromSubUvsToUnit(uv.x, TRANSMITTANCE_TEXTURE_SIZE.y), fromSubUvsToUnit(uv.y, TRANSMITTANCE_TEXTURE_SIZE.y)); // No real impact so off
    float x_mu = uv.x;
    float x_r = uv.y;

    float H = sqrt(Atmosphere.top_radius * Atmosphere.top_radius - Atmosphere.bottom_radius * Atmosphere.bottom_radius);
    float rho = H * x_r;
    viewHeight = sqrt(rho * rho + Atmosphere.bottom_radius * Atmosphere.bottom_radius);

    float d_min = Atmosphere.top_radius - viewHeight;
    float d_max = rho + H;
    float d = d_min + x_mu * (d_max - d_min);
    viewZenithCosAngle = d == 0.0 ? 1.0f : (H * H - rho * rho - d * d) / (2.0 * viewHeight * d);
    viewZenithCosAngle = clamp(viewZenithCosAngle, -1.0, 1.0);
}


float RayleighPhase(float cosTheta)
{
    float factor = 3.0f / (16.0f * PI);
    return factor * (1.0f + cosTheta * cosTheta);
}

struct MediumSampleRGB
{
    vec3 scattering;
    vec3 absorption;
    vec3 extinction;

    vec3 scatteringMie;
    vec3 absorptionMie;
    vec3 extinctionMie;

    vec3 scatteringRay;
    vec3 absorptionRay;
    vec3 extinctionRay;

    vec3 scatteringOzo;
    vec3 absorptionOzo;
    vec3 extinctionOzo;

    vec3 albedo;
};

float getAlbedo(float scattering, float extinction)
{
    return scattering / max(0.001, extinction);
}
vec3 getAlbedo(vec3 scattering, vec3 extinction)
{
    return scattering / max(vec3(0.001), extinction);
}


float GetLayerDensity(DensityProfileLayer layer, float altitude) {
    float density = layer.exp_term * exp(layer.exp_scale * altitude) + layer.linear_term * altitude + layer.constant_term;
    return clamp(density, float(0.0), float(1.0));
}

float GetProfileDensity(DensityProfile profile, float altitude) {
    return altitude < profile.layers[0].width ?
    GetLayerDensity(profile.layers[0], altitude) :
    GetLayerDensity(profile.layers[1], altitude);
}


MediumSampleRGB sampleMediumRGB(in vec3 WorldPos, in AtmosphereParameters Atmosphere)
{
    const float viewHeight = length(WorldPos) - Atmosphere.bottom_radius;

    const float densityMie = GetProfileDensity(ATMOSPHERE.mie_density, viewHeight);
    const float densityRay = GetProfileDensity(ATMOSPHERE.rayleigh_density, viewHeight);
    const float densityOzo = GetProfileDensity(ATMOSPHERE.ozone_density, viewHeight);

    MediumSampleRGB s;

    s.scatteringMie = densityMie * Atmosphere.mie_scattering;
    s.absorptionMie = densityMie * (Atmosphere.mie_extinction - Atmosphere.mie_scattering);
    s.extinctionMie = densityMie * Atmosphere.mie_extinction;

    s.scatteringRay = densityRay * Atmosphere.rayleigh_scattering;
    s.absorptionRay = vec3(0.0f);
    s.extinctionRay = s.scatteringRay + s.absorptionRay;

    s.scatteringOzo = vec3(0.0);
    s.absorptionOzo = densityOzo * Atmosphere.ozone_extinction;
    s.extinctionOzo = s.scatteringOzo + s.absorptionOzo;

    s.scattering = s.scatteringMie + s.scatteringRay + s.scatteringOzo;
    s.absorption = s.absorptionMie + s.absorptionRay + s.absorptionOzo;
    s.extinction = s.extinctionMie + s.extinctionRay + s.extinctionOzo;
    s.albedo = getAlbedo(s.scattering, s.extinction);

    return s;
}

vec3 GetMultipleScattering(AtmosphereParameters Atmosphere, vec3 scattering, vec3 extinction, vec3 worlPos, float viewZenithCosAngle)
{
    vec2 uv = saturate(vec2(viewZenithCosAngle*0.5f + 0.5f, (length(worlPos) - Atmosphere.bottom_radius) / (Atmosphere.top_radius - Atmosphere.bottom_radius)));
    uv = vec2(fromUnitToSubUvs(uv.x, float(TRANSMITTANCE_TEXTURE_SIZE.x)), fromUnitToSubUvs(uv.y, float(TRANSMITTANCE_TEXTURE_SIZE.y)));

    vec3 multiScatteredLuminance = texture(transmittanceTexture, uv).rgb;
    return multiScatteredLuminance;
}

struct SingleScatteringResult
{
    vec3 L;// Scattered light (luminance)
    vec3 OpticalDepth;// Optical depth (1/m)
    vec3 Transmittance;// Transmittance in [0,1] (unitless)
    vec3 MultiScatAs1;

    vec3 NewMultiScatStep0Out;
    vec3 NewMultiScatStep1Out;
};

SingleScatteringResult SingleScatteringResult_init() {
    return SingleScatteringResult(vec3(0), vec3(0), vec3(0), vec3(0), vec3(0), vec3(0));
}

float interspectAtmosphere(AtmosphereParameters Atmosphere, vec3 WorldPos, vec3 WorldDir) {
    vec3 earthO = vec3(0.0f, 0.0f, 0.0f);
    float tBottom = raySphereIntersectNearest(WorldPos, WorldDir, vec3(0), Atmosphere.bottom_radius);
    float tTop = raySphereIntersectNearest(WorldPos, WorldDir, vec3(0), Atmosphere.top_radius);
    float tMin = 0.0f;
    if (tBottom < 0.0f){
        if (tTop < 0.0f){
            tMin = 0.0f;
        }
        else {
            tMin = tTop;
        }
    }
    else {
        if (tTop > 0.0f){
            tMin = min(tTop, tBottom);
        }
    }
    return tMin;
}


SingleScatteringResult IntegrateScatteredLuminance(
in vec2 pixPos, in vec3 WorldPos, in vec3 WorldDir, in vec3 SunDir, in AtmosphereParameters Atmosphere,
in bool ground, in float SampleCountIni, in float DepthBufferValue, in bool VariableSampleCount,
in bool MieRayPhase, in float tMaxMax)
{
    //    const bool debugEnabled = all(ivec2(pixPos.xx) == gMouseLastDownPos.xx) && uint(pixPos.y) % 10 == 0 && DepthBufferValue != -1.0f;
    const bool debugEnabled = false;
    SingleScatteringResult result = SingleScatteringResult_init();

    //    vec3 ClipSpace = vec3((pixPos / vec2(gResolution))*vec2(2.0, -2.0) - vec2(1.0, -1.0), 1.0);
    vec3 ClipSpace = vec3(0);

    // Compute next intersection with atmosphere or ground
    vec3 earthO = vec3(0.0f, 0.0f, 0.0f);
    float tBottom = raySphereIntersectNearest(WorldPos, WorldDir, earthO, Atmosphere.bottom_radius);
    float tTop = raySphereIntersectNearest(WorldPos, WorldDir, earthO, Atmosphere.top_radius);
    float tMax = 0.0f;
    if (tBottom < 0.0f){
        if (tTop < 0.0f){
            tMax = 0.0f;// No intersection with earth nor atmosphere: stop right away
            return result;
        }
        else {
            tMax = tTop;
        }
    }
    else {
        if (tTop > 0.0f){
            tMax = min(tTop, tBottom);
        }
    }

    //    if (DepthBufferValue >= 0.0f)
    //    {
    //        ClipSpace.z = DepthBufferValue;
    //        if (ClipSpace.z < 1.0f)
    //        {
    //            vec4 DepthBufferWorldPos = mul(gSkyInvViewProjMat, vec4(ClipSpace, 1.0));
    //            DepthBufferWorldPos /= DepthBufferWorldPos.w;
    //
    //            float tDepth = length(DepthBufferWorldPos.xyz - (WorldPos + vec3(0.0, 0.0, -Atmosphere.bottom_radius))); // apply earth offset to go back to origin as top of earth mode.
    //            if (tDepth < tMax)
    //            {
    //                tMax = tDepth;
    //            }
    //        }
    //        //		if (VariableSampleCount && ClipSpace.z == 1.0f)
    //        //			return result;
    //    }
    tMax = min(tMax, tMaxMax);

    // Sample count
    float SampleCount = SampleCountIni;
    float SampleCountFloor = SampleCountIni;
    float tMaxFloor = tMax;
    //    if (VariableSampleCount)
    //    {
    //        SampleCount = lerp(RayMarchMinMaxSPP.x, RayMarchMinMaxSPP.y, saturate(tMax*0.01));
    //        SampleCountFloor = floor(SampleCount);
    //        tMaxFloor = tMax * SampleCountFloor / SampleCount;	// rescale tMax to map to the last entire step segment.
    //    }
    float dt = tMax / SampleCount;

    // Phase functions
    const float uniformPhase = 1.0 / (4.0 * PI);
    const vec3 wi = SunDir;
    const vec3 wo = WorldDir;
    float cosTheta = dot(wi, wo);
    float MiePhaseValue = hgPhase(Atmosphere.mie_phase_function_g, -cosTheta);// mnegate cosTheta because due to WorldDir being a "in" direction.
    float RayleighPhaseValue = RayleighPhase(cosTheta);

    //    #ifdef ILLUMINANCE_IS_ONE
    // When building the scattering factor, we assume light illuminance is 1 to compute a transfert function relative to identity illuminance of 1.
    // This make the scattering factor independent of the light. It is now only linked to the atmosphere properties.
    vec3 globalL = vec3(10.0);
    //    #else
    //    vec3 globalL = gSunIlluminance;
    //    #endif

    // Ray march the atmosphere to integrate optical depth
    vec3 L = vec3(0.0);
    vec3 throughput = vec3(1.0);
    vec3 OpticalDepth = vec3(0.0);
    float t = 0.0f;
    float tPrev = 0.0;
    const float SampleSegmentT = 0.3f;
    for (float s = 0.0f; s < SampleCount; s += 1.0f){
        if (VariableSampleCount){
            // More expenssive but artefact free
            float t0 = (s) / SampleCountFloor;
            float t1 = (s + 1.0f) / SampleCountFloor;
            // Non linear distribution of sample within the range.
            t0 = t0 * t0;
            t1 = t1 * t1;
            // Make t0 and t1 world space distances.
            t0 = tMaxFloor * t0;
            if (t1 > 1.0) {
                t1 = tMax;
                //	t1 = tMaxFloor;	// this reveal depth slices
            }
            else {
                t1 = tMaxFloor * t1;
            }
            //t = t0 + (t1 - t0) * (whangHashNoise(pixPos.x, pixPos.y, gFrameId * 1920 * 1080)); // With dithering required to hide some sampling artefact relying on TAA later? This may even allow volumetric shadow?
            t = t0 + (t1 - t0)*SampleSegmentT;
            dt = t1 - t0;
        }
        else {
            //t = tMax * (s + SampleSegmentT) / SampleCount;
            // Exact difference, important for accuracy of multiple scattering
            float NewT = tMax * (s + SampleSegmentT) / SampleCount;
            dt = NewT - t;
            t = NewT;
        }
        vec3 P = WorldPos + t * WorldDir;

        MediumSampleRGB medium = sampleMediumRGB(P, Atmosphere);
        const vec3 SampleOpticalDepth = medium.extinction * dt;
        const vec3 SampleTransmittance = exp(-SampleOpticalDepth);
        OpticalDepth += SampleOpticalDepth;

        float pHeight = length(P);
        const vec3 UpVector = P / pHeight;
        float SunZenithCosAngle = dot(SunDir, UpVector);
        vec2 uv;
        LutTransmittanceParamsToUv(Atmosphere, pHeight, SunZenithCosAngle, uv);
        vec3 TransmittanceToSun = texture(transmittanceTexture, uv).rgb;


        vec3 PhaseTimesScattering;
        if (MieRayPhase){
            PhaseTimesScattering = medium.scatteringMie * MiePhaseValue + medium.scatteringRay * RayleighPhaseValue;
        }
        else {
            PhaseTimesScattering = medium.scattering * uniformPhase;
        }

        // Earth shadow
        float tEarth = raySphereIntersectNearest(P, SunDir, earthO + PLANET_RADIUS_OFFSET * UpVector, Atmosphere.bottom_radius);
        float earthShadow = tEarth >= 0.0f ? 0.0f : 1.0f;

        // Dual scattering for multi scattering

        vec3 multiScatteredLuminance = vec3(0.0f);
        //        #if MULTISCATAPPROX_ENABLED
        //        multiScatteredLuminance = GetMultipleScattering(Atmosphere, medium.scattering, medium.extinction, P, SunZenithCosAngle);
        //        #endif

        float shadow = 1.0f;
        //        #if SHADOWMAP_ENABLED
        //        // First evaluate opaque shadow
        //        shadow = getShadow(Atmosphere, P);
        //        #endif

        vec3 S = globalL * (earthShadow * shadow * TransmittanceToSun * PhaseTimesScattering + multiScatteredLuminance * medium.scattering);

        // When using the power serie to accumulate all sattering order, serie r must be <1 for a serie to converge.
        // Under extreme coefficient, MultiScatAs1 can grow larger and thus result in broken visuals.
        // The way to fix that is to use a proper analytical integration as proposed in slide 28 of http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
        // However, it is possible to disable as it can also work using simple power serie sum unroll up to 5th order. The rest of the orders has a really low contribution.
        #define MULTI_SCATTERING_POWER_SERIE 1

        #if MULTI_SCATTERING_POWER_SERIE==0
        // 1 is the integration of luminance over the 4pi of a sphere, and assuming an isotropic phase function of 1.0/(4*PI)
        result.MultiScatAs1 += throughput * medium.scattering * 1 * dt;
        #else
        vec3 MS = medium.scattering * 1;
        vec3 MSint = (MS - MS * SampleTransmittance) / medium.extinction;
        result.MultiScatAs1 += throughput * MSint;
        #endif

        // Evaluate input to multi scattering
        {
            vec3 newMS;

            newMS = earthShadow * TransmittanceToSun * medium.scattering * uniformPhase * 1;
            result.NewMultiScatStep0Out += throughput * (newMS - newMS * SampleTransmittance) / medium.extinction;
            //	result.NewMultiScatStep0Out += SampleTransmittance * throughput * newMS * dt;

            newMS = medium.scattering * uniformPhase * multiScatteredLuminance;
            result.NewMultiScatStep1Out += throughput * (newMS - newMS * SampleTransmittance) / medium.extinction;
            //	result.NewMultiScatStep1Out += SampleTransmittance * throughput * newMS * dt;
        }

        #if 0
        L += throughput * S * dt;
        throughput *= SampleTransmittance;
        #else
        // See slide 28 at http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
        vec3 Sint = (S - S * SampleTransmittance) / medium.extinction;// integrate along the current step segment
        L += throughput * Sint;// accumulate and also take into account the transmittance from previous steps
        throughput *= SampleTransmittance;
        #endif

        tPrev = t;
    }

    if (ground && tMax == tBottom && tBottom > 0.0){
        // Account for bounced light off the earth
        vec3 P = WorldPos + tBottom * WorldDir;
        float pHeight = length(P);

        const vec3 UpVector = P / pHeight;
        float SunZenithCosAngle = dot(SunDir, UpVector);
        vec2 uv;
        LutTransmittanceParamsToUv(Atmosphere, pHeight, SunZenithCosAngle, uv);
        vec3 TransmittanceToSun = texture(transmittanceTexture, uv).rgb;

        const float NdotL = saturate(dot(normalize(UpVector), normalize(SunDir)));
        L += globalL * TransmittanceToSun * throughput * NdotL * Atmosphere.ground_albedo / PI;
    }

    result.L = L;
    result.OpticalDepth = OpticalDepth;
    result.Transmittance = throughput;
    return result;
}


#define NONLINEARSKYVIEWLUT 1
void UvToSkyViewLutParams(AtmosphereParameters Atmosphere, out float viewZenithCosAngle, out float lightViewCosAngle, in float viewHeight, in vec2 uv)
{
    // Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible)
    uv = vec2(fromSubUvsToUnit(uv.x, SKY_VIEW_TEXTURE_SIZE_F.x), fromSubUvsToUnit(uv.y, SKY_VIEW_TEXTURE_SIZE_F.y));

    float Vhorizon = sqrt(viewHeight * viewHeight - Atmosphere.bottom_radius * Atmosphere.bottom_radius);
    float CosBeta = Vhorizon / viewHeight;// GroundToHorizonCos
    float Beta = acos(CosBeta);
    float ZenithHorizonAngle = PI - Beta;

    if (uv.y < 0.5f)
    {
        float coord = 2.0*uv.y;
        coord = 1.0 - coord;
        #if NONLINEARSKYVIEWLUT
        coord *= coord;
        #endif
        coord = 1.0 - coord;
        viewZenithCosAngle = cos(ZenithHorizonAngle * coord);
    }
    else
    {
        float coord = uv.y*2.0 - 1.0;
        #if NONLINEARSKYVIEWLUT
        coord *= coord;
        #endif
        viewZenithCosAngle = cos(ZenithHorizonAngle + Beta * coord);
    }

    float coord = uv.x;
    coord *= coord;
    lightViewCosAngle = -(coord*2.0 - 1.0);
}

void SkyViewLutParamsToUv(AtmosphereParameters Atmosphere, in bool IntersectGround, in float viewZenithCosAngle, in float lightViewCosAngle, in float viewHeight, out vec2 uv)
{
    float Vhorizon = sqrt(viewHeight * viewHeight - Atmosphere.bottom_radius * Atmosphere.bottom_radius);
    float CosBeta = Vhorizon / viewHeight;// GroundToHorizonCos
    float Beta = acos(CosBeta);
    float ZenithHorizonAngle = PI - Beta;

    if (!IntersectGround)
    {
        float coord = acos(viewZenithCosAngle) / ZenithHorizonAngle;
        coord = 1.0 - coord;
        #if NONLINEARSKYVIEWLUT
        coord = sqrt(coord);
        #endif
        coord = 1.0 - coord;
        uv.y = coord * 0.5f;
    }
    else
    {
        float coord = (acos(viewZenithCosAngle) - ZenithHorizonAngle) / Beta;
        #if NONLINEARSKYVIEWLUT
        coord = sqrt(coord);
        #endif
        uv.y = coord * 0.5f + 0.5f;
    }

    {
        float coord = -lightViewCosAngle * 0.5f + 0.5f;
        coord = sqrt(coord);
        uv.x = coord;
    }

    // Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible)
    uv = vec2(fromUnitToSubUvs(uv.x, SKY_VIEW_TEXTURE_SIZE_F.x), fromUnitToSubUvs(uv.y, SKY_VIEW_TEXTURE_SIZE_F.y));
}

#define AP_KM_PER_SLICE 4.0f

float AerialPerspectiveDepthToSlice(float depth){
    return depth * (1.0f / AP_KM_PER_SLICE);
}

float AerialPerspectiveSliceToDepth(float slice){
    return slice * AP_KM_PER_SLICE;
}

vec3 GetSolarRadiance() {
    return ATMOSPHERE.solar_irradiance /
    (PI * ATMOSPHERE.sun_angular_radius * ATMOSPHERE.sun_angular_radius);
}


vec3 sunWithBloom(vec3 worldDir, vec3 sunDir)
{
    const float sunSolidAngle = 1.0 * PI / 180.0;
    const float minSunCosTheta = cos(sunSolidAngle);

    float cosTheta = dot(worldDir, sunDir);
    if(cosTheta >= minSunCosTheta) {return vec3(0.5) ;}
    float offset = minSunCosTheta - cosTheta;
    float gaussianBloom = exp(-offset * 50000.0) * 0.5;
    float invBloom = 1.0/(0.02 + offset * 300.0) * 0.01;
    return vec3(gaussianBloom + invBloom);
}

vec3 GetSunLuminance(vec3 WorldPos, vec3 WorldDir, vec3 sunDirection, float PlanetRadius) {
    if (dot(WorldDir, sunDirection) > cos(ATMOSPHERE.sun_angular_radius)) {
        return ATMOSPHERE.solar_irradiance;
    }
    return vec3(0);
}
#endif// ATMOSPHERE_COMMON_GLSL