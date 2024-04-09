#ifndef HASH_GLSL
#define HASH_GLSL

//int hash(ivec2 pid) {
//    return (pid.x * 92837111) ^ (pid.y * 689287499);
//}

//int computeHash(vec2 pos) {
//    ivec2 pid = ivec2(pos/spacing);
//    return abs(hash(pid)) % hashMapSize;
//}

ivec2 intCoords(vec2 pos) {
    return ivec2(pos/spacing);
}

int hash(ivec2 pid) {
    return pid.y * int(20/spacing) + pid.x;
}

int computeHash(vec2 pos) {
    ivec2 pid = intCoords(pos);
    return hash(pid);
}

#endif // HASH_GLSL