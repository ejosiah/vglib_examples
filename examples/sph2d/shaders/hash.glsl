#ifndef HASH_GLSL
#define HASH_GLSL

ivec2 intCoords(vec2 pos) {
    return ivec2(pos/spacing);
}

int hash(ivec2 pid) {
    return pid.y * int(20/spacing) + pid.x;
}

//int hash(ivec2 pid) {
//    return 512 * pid.x + 79 * pid.y;
//}

//int hash(ivec2 pid) {
//    return (pid.x * 92837111) ^ (pid.y * 689287499);
//}

int computeHash(vec2 pos) {
    ivec2 pid = intCoords(pos);
    return abs(hash(pid)) % hashMapSize;
}

//int computeHash(vec2 pos) {
//    ivec2 pid = intCoords(pos);
//    return hash(pid);
//}

#endif // HASH_GLSL