#version 460

struct TextureInfo {
    vec2 offset;
    vec2 scale;
    int index;
    int texCoord;
    float rotation;
};


layout(set = 0, binding = 0) buffer TextureInfos {
    TextureInfo textureInfos[];
};

void main(){

}