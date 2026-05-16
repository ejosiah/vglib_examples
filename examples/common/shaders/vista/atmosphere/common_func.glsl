#ifndef ATMOSPHERE_COMMON_FUNC_GLSL
#define ATMOSPHERE_COMMON_FUNC_GLSL

//const float PI = 3.1415926535897932384626433832795;
const float PLANET_RADIUS_OFFSET = 0.01;

float saturate(float x) {
	return clamp(x, 0, 1);
}

vec2 saturate(vec2 x) {
	return clamp(x, vec2(0), vec2(1));
}

float ClampCosine(float mu) {
	return clamp(mu, float(-1.0), float(1.0));
}

float ClampDistance(float d) {
	return max(d, 0.0 * m);
}

float ClampRadius(AtmosphereParameters atmosphere, float r) {
	return clamp(r, atmosphere.bottom_radius, atmosphere.top_radius);
}

float SafeSqrt(float a) {
	return sqrt(max(a, 0.0 * m2));
}

/* Return sqrt clamped to 0 */
float safeSqrt(float x)
{
    return sqrt(max(0, x));
}

float fromSubUvsToUnit(float u, float resolution) {
	return (u - 0.5 / resolution) * (resolution / (resolution - 1.0)); 
}

float fromUnitToSubUvs(float u, float resolution) {
	 return (u + 0.5f / resolution) * (resolution / (resolution + 1.0));
}

/**
 * Get parameters used for skyViewLUT computation for texel with provided uv coords
 * @param uv - texel uv in the range [0,1]
 * @param atmosphereBoundaries - x is atmosphere bottom radius y is top
 * @param skyViewDimensions - skyViewLUT dimensions
 * @param viewHeight - viewHeight in world coordinates -> distance from planet center 
 * @return - viewZenithAngle in x, lightViewAngle in y
 */
vec2 UvToSkyViewLUTParams(vec2 uv, vec2 atmosphereBoundaries, vec2 skyViewDimensions,
	float viewHeight)
{
	/* Constrain uvs to valid sub texel range 
	(avoid zenith derivative issue making LUT usage visible) */
	uv = vec2(fromSubUvsToUnit(uv.x, skyViewDimensions.x),
			  fromSubUvsToUnit(uv.y, skyViewDimensions.y));
			
	float beta = asin(atmosphereBoundaries.x / viewHeight);
	float zenithHorizonAngle = PI - beta;

	float viewZenithAngle;
	float lightViewAngle;
	/* Nonuniform mapping near the horizon to avoid artefacts */
	if(uv.y < 0.5)
	{
		float coord = 1.0 - (1.0 - 2.0 * uv.y) * (1.0 - 2.0 * uv.y);
		viewZenithAngle = zenithHorizonAngle * coord;
	} else {
		float coord = (uv.y * 2.0 - 1.0) * (uv.y * 2.0 - 1.0);
		viewZenithAngle = zenithHorizonAngle + beta * coord;
	}
	lightViewAngle = (uv.x * uv.x) * PI;
	return vec2(viewZenithAngle, lightViewAngle);
}

/**
 * Get parameters used for skyViewLUT computation for texel with provided uv coords
 * @param intersectGround - true if ray intersects ground false otherwise
 * @param LUTParams - viewZenithAngle in x, lightViewAngle in y
 * @param viewHeight - viewHeight in world coordinates -> distance from planet center 
 * @param atmosphereBoundaries - x is atmosphere bottom radius y is top
 * @param skyViewDimensions - skyViewLUT dimensions
 * @return - uv for the skyViewLUT sampling
 */
vec2 SkyViewLutParamsToUv(bool intersectGround, vec2 LUTParams, float viewHeight,
	vec2 atmosphereBoundaries, vec2 skyViewDimensions)
{
	vec2 uv;
	float beta = asin(atmosphereBoundaries.x / viewHeight);
	float zenithHorizonAngle = PI - beta;

	if(!intersectGround)
	{
		float coord = LUTParams.x / zenithHorizonAngle;
		coord = (1.0 - safeSqrt(1.0 - coord)) / 2.0;
		uv.y = coord;
	} else {
		float coord = (LUTParams.x - zenithHorizonAngle) / beta;
		coord = (safeSqrt(coord) + 1.0) / 2.0;
		uv.y = coord;
	}
	uv.x = safeSqrt(LUTParams.y / PI);
	uv = vec2(fromUnitToSubUvs(uv.x, skyViewDimensions.x),
			  fromUnitToSubUvs(uv.y, skyViewDimensions.y));
	return uv;
}

/**
 * Transmittance LUT uses not uniform mapping -> transfer from uv to this mapping
 * @param uv - uv in the range [0,1]
 * @param atmosphereBoundaries - x is atmoBottom radius, y is top radius
 * @return - height in x, zenith cos angle in y
 */
vec2 UvToTransmittanceLUTParams(vec2 uv, vec2 atmosphereBoundaries)
{
	/* Params.x stores height, Params.y stores ZenithCosAngle */
	vec2 Params;
	float H = safeSqrt(
		  atmosphereBoundaries.y * atmosphereBoundaries.y 
		- atmosphereBoundaries.x * atmosphereBoundaries.x);

	float rho = H * uv.y;
	Params.x = safeSqrt( rho * rho + atmosphereBoundaries.x * atmosphereBoundaries.x);

	float d_min = atmosphereBoundaries.y - Params.x;
	float d_max = rho + H;
	float d = d_min + uv.x * (d_max - d_min);
	
	Params.y = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * Params.x * d);
	Params.y = clamp(Params.y, -1.0, 1.0);

	return Params;
}

/**
 * Transmittance LUT uses not uniform mapping -> transfer from mapping to texture uv
 * @param parameters - height in x, zenith cos angle in y
 * @param atmosphereBoundaries - x is bottom radius, y is top radius
 * @return - uv of the corresponding texel
 */
vec2 TransmittanceLUTParamsToUv(vec2 parameters, vec2 atmosphereBoundaries)
{
	float H = safeSqrt(
		  atmosphereBoundaries.y * atmosphereBoundaries.y 
		- atmosphereBoundaries.x * atmosphereBoundaries.x);
	
	float rho = safeSqrt(parameters.x * parameters.x - 
		atmosphereBoundaries.x * atmosphereBoundaries.x);
	
	float discriminant = parameters.x * parameters.x * (parameters.y * parameters.y - 1.0) +
		atmosphereBoundaries.y * atmosphereBoundaries.y;
	/* Distance to top atmosphere boundary */
	float d = max(0.0, (-parameters.x * parameters.y + safeSqrt(discriminant)));

	float d_min = atmosphereBoundaries.y - parameters.x;
	float d_max = rho + H;
	float mu = (d - d_min) / (d_max - d_min);
	float r = rho / H;

	return vec2(mu, r);
}

/**
 * Return distance of the first intersection between ray and sphere
 * @param r0 - ray origin
 * @param rd - normalized ray direction
 * @param s0 - sphere center
 * @param sR - sphere radius
 * @return return distance of intersection or -1.0 if there is no intersection
 */
float raySphereIntersectNearest(vec3 r0, vec3 rd, vec3 s0, float sR)
{
	float a = dot(rd, rd);
	vec3 s0_r0 = r0 - s0;
	float b = 2.0 * dot(rd, s0_r0);
	float c = dot(s0_r0, s0_r0) - (sR * sR);
	float delta = b * b - 4.0*a*c;
	if (delta < 0.0 || a == 0.0)
	{
		return -1.0;
	}
	float sol0 = (-b - safeSqrt(delta)) / (2.0*a);
	float sol1 = (-b + safeSqrt(delta)) / (2.0*a);
	if (sol0 < 0.0 && sol1 < 0.0)
	{
		return -1.0;
	}
	if (sol0 < 0.0)
	{
		return max(0.0, sol1);
	}
	else if (sol1 < 0.0)
	{
		return max(0.0, sol0);
	}
	return max(0.0, min(sol0, sol1));
}

/** 
 * Moves to the nearest intersection with top of the atmosphere in the direction specified in 
 * worldDirection
 * @param worldPosition - current world position -> will be changed to new pos at the top of
 * 		the atmosphere if there exists such intersection
 * @param worldDirecion - the direction in which the shift will be done
 * @param atmosphereBoundaries - x is bottom radius, y is top radius
 */
bool MoveToTopAtmosphere(inout vec3 WorldPos, in vec3 WorldDir, in float AtmosphereTopRadius) {
	float viewHeight = length(WorldPos);
	if (viewHeight > AtmosphereTopRadius)
	{
		float tTop = raySphereIntersectNearest(WorldPos, WorldDir, vec3(0.0f), AtmosphereTopRadius);
		if (tTop >= 0.0f)
		{
			vec3 UpVector = WorldPos / viewHeight;
			vec3 UpOffset = UpVector * -PLANET_RADIUS_OFFSET;
			WorldPos = WorldPos + WorldDir * tTop + UpOffset;
		}
		else
		{
			// Ray is not intersecting the atmosphere
			return false;
		}
	}
	return true;// ok to start tracing
}

void LutTransmittanceParamsToUv(AtmosphereParameters Atmosphere, in float viewHeight, in float viewZenithCosAngle, out vec2 uv) {
	float H = sqrt(max(0.0f, Atmosphere.top_radius * Atmosphere.top_radius - Atmosphere.bottom_radius * Atmosphere.bottom_radius));
	float rho = sqrt(max(0.0f, viewHeight * viewHeight - Atmosphere.bottom_radius * Atmosphere.bottom_radius));

	float discriminant = viewHeight * viewHeight * (viewZenithCosAngle * viewZenithCosAngle - 1.0) + Atmosphere.top_radius * Atmosphere.top_radius;
	float d = max(0.0, (-viewHeight * viewZenithCosAngle + sqrt(discriminant)));// Distance to atmosphere boundary

	float d_min = Atmosphere.top_radius - viewHeight;
	float d_max = rho + H;
	float x_mu = (d - d_min) / (d_max - d_min);
	float x_r = rho / H;

	uv = vec2(x_mu, x_r);
	//uv = vec2(fromUnitToSubUvs(uv.x, TRANSMITTANCE_TEXTURE_WIDTH), fromUnitToSubUvs(uv.y, TRANSMITTANCE_TEXTURE_HEIGHT)); // No real impact so off
}

#endif // ATMOSPHERE_COMMON_FUNC_GLSL