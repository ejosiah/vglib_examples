#define LEFT_PLANE 0
#define RIGHT_PLANE 1
#define BOTTOM_PLANE 2
#define TOP_PLANE 3
#define NEAR_PLANE 4
#define FAR_PLANE 5

struct Frustum{
    vec4 planes[6];
    vec4 corners[8];
};

const vec4 corners[8] = {
    vec4(-1, -1, 0, 1),
    vec4( 1, -1, 0, 1),
    vec4( 1,  1, 0, 1),
    vec4(-1,  1, 0, 1),
    vec4(-1, -1, 1, 1),
    vec4( 1, -1, 1, 1),
    vec4( 1,  1, 1, 1),
    vec4(-1,  1, 1, 1)
};

void getFrustumPlanes(mat4 viewProjection, out vec4 planes[6]){

    mat4 vp = transpose(viewProjection);

    planes[LEFT_PLANE]      = vp[3] + vp[0];
    planes[RIGHT_PLANE]     = vp[3] - vp[0];
    planes[BOTTOM_PLANE]    = vp[3] + vp[1];
    planes[TOP_PLANE]       = vp[3] - vp[1];
    planes[NEAR_PLANE]      = vp[2];
    planes[FAR_PLANE]       = vp[3] - vp[2];
}

void getFrustumCorners(mat4 viewProjection, out vec4 points[8]){

    mat4 invVP = inverse(viewProjection);

    for(int i = 0; i < 8; i++){
        const vec4 q = invVP * corners[i];
        points[i] = q / q.w;
    }
}

Frustum createFrustum(mat4 viewProjection){
    Frustum frustum;
    getFrustumPlanes(viewProjection, frustum.planes);
    getFrustumCorners(viewProjection, frustum.corners);

    return frustum;
}

bool isBoxInFrustum(Frustum frustum, vec3 bMin, vec3 bMax){

    for (int i = 0; i < 6; i++) {
        int r = 0;
        r += ( dot( frustum.planes[i], vec4(bMin.x, bMin.y, bMin.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], vec4(bMax.x, bMin.y, bMin.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], vec4(bMin.x, bMax.y, bMin.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], vec4(bMax.x, bMax.y, bMin.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], vec4(bMin.x, bMin.y, bMax.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], vec4(bMax.x, bMin.y, bMax.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], vec4(bMin.x, bMax.y, bMax.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], vec4(bMax.x, bMax.y, bMax.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        if ( r == 8 ) return false;
    }

    int r = 0;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].x > bMax.x) ? 1 : 0 ); if ( r == 8 ) return false;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].x < bMin.x) ? 1 : 0 ); if ( r == 8 ) return false;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].y > bMax.y) ? 1 : 0 ); if ( r == 8 ) return false;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].y < bMin.y) ? 1 : 0 ); if ( r == 8 ) return false;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].z > bMax.z) ? 1 : 0 ); if ( r == 8 ) return false;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].z < bMin.z) ? 1 : 0 ); if ( r == 8 ) return false;

    return true;
}

bool isBoxInFrustum(mat4 viewProjection, vec3 bMin, vec3 bMax){
    Frustum frustum = createFrustum(viewProjection);
    return isBoxInFrustum(frustum, bMin, bMax);
}