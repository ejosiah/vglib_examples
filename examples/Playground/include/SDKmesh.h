#pragma once

#include <glm/glm.hpp>

#define SDKMESH_FILE_VERSION 101
#define MAX_VERTEX_ELEMENTS 32
#define MAX_VERTEX_STREAMS 16
#define MAX_FRAME_NAME 100
#define MAX_MESH_NAME 100
#define MAX_SUBSET_NAME 100
#define MAX_MATERIAL_NAME 100
#define MAX_TEXTURE_NAME MAX_PATH
#define MAX_MATERIAL_PATH MAX_PATH
#define INVALID_FRAME ((UINT)-1)
#define INVALID_MESH ((UINT)-1)
#define INVALID_MATERIAL ((UINT)-1)
#define INVALID_SUBSET ((UINT)-1)
#define INVALID_ANIMATION_DATA ((UINT)-1)
#define ERROR_RESOURCE_VALUE 1
#define INVALID_SAMPLER_SLOT ((UINT)-1)

typedef enum D3DDECLUSAGE {
    D3DDECLUSAGE_POSITION      = 0,
    D3DDECLUSAGE_BLENDWEIGHT   = 1,
    D3DDECLUSAGE_BLENDINDICES  = 2,
    D3DDECLUSAGE_NORMAL        = 3,
    D3DDECLUSAGE_PSIZE         = 4,
    D3DDECLUSAGE_TEXCOORD      = 5,
    D3DDECLUSAGE_TANGENT       = 6,
    D3DDECLUSAGE_BINORMAL      = 7,
    D3DDECLUSAGE_TESSFACTOR    = 8,
    D3DDECLUSAGE_POSITIONT     = 9,
    D3DDECLUSAGE_COLOR         = 10,
    D3DDECLUSAGE_FOG           = 11,
    D3DDECLUSAGE_DEPTH         = 12,
    D3DDECLUSAGE_SAMPLE        = 13
} D3DDECLUSAGE, *LPD3DDECLUSAGE;

typedef enum D3DDECLTYPE {
    D3DDECLTYPE_FLOAT1     = 0,
    D3DDECLTYPE_FLOAT2     = 1,
    D3DDECLTYPE_FLOAT3     = 2,
    D3DDECLTYPE_FLOAT4     = 3,
    D3DDECLTYPE_D3DCOLOR   = 4,
    D3DDECLTYPE_UBYTE4     = 5,
    D3DDECLTYPE_SHORT2     = 6,
    D3DDECLTYPE_SHORT4     = 7,
    D3DDECLTYPE_UBYTE4N    = 8,
    D3DDECLTYPE_SHORT2N    = 9,
    D3DDECLTYPE_SHORT4N    = 10,
    D3DDECLTYPE_USHORT2N   = 11,
    D3DDECLTYPE_USHORT4N   = 12,
    D3DDECLTYPE_UDEC3      = 13,
    D3DDECLTYPE_DEC3N      = 14,
    D3DDECLTYPE_FLOAT16_2  = 15,
    D3DDECLTYPE_FLOAT16_4  = 16,
    D3DDECLTYPE_UNUSED     = 17
} D3DDECLTYPE, *LPD3DDECLTYPE;

typedef struct D3DVERTEXELEMENT9 {
    WORD Stream;
    WORD Offset;
    uint8_t Type;
    uint8_t Method;
    uint8_t Usage;
    uint8_t UsageIndex;
} D3DVERTEXELEMENT9, *LPD3DVERTEXELEMENT9;

enum SDKMESH_INDEX_TYPE
{
    IT_16BIT = 0,
    IT_32BIT,
};


struct SDKMESH_HEADER
{
    //Basic Info and sizes
    uint32_t Version;
    uint8_t IsBigEndian;
    uint64_t HeaderSize;
    uint64_t NonBufferDataSize;
    uint64_t BufferDataSize;

    //Stats
    uint32_t NumVertexBuffers;
    uint32_t NumIndexBuffers;
    uint32_t NumMeshes;
    uint32_t NumTotalSubsets;
    uint32_t NumFrames;
    uint32_t NumMaterials;

    //Offsets to Data
    uint64_t VertexStreamHeadersOffset;
    uint64_t IndexStreamHeadersOffset;
    uint64_t MeshDataOffset;
    uint64_t SubsetDataOffset;
    uint64_t FrameDataOffset;
    uint64_t MaterialDataOffset;
};

struct SDKMESH_INDEX_BUFFER_HEADER
{
    uint64_t NumIndices;
    uint64_t SizeBytes;
    uint32_t IndexType;
    uint64_t DataOffset;

};


struct SDKMESH_VERTEX_BUFFER_HEADER
{
    UINT64 NumVertices;
    UINT64 SizeBytes;
    UINT64 StrideBytes;
    D3DVERTEXELEMENT9 Decl[MAX_VERTEX_ELEMENTS];
    UINT64 DataOffset;

};

struct SDKMESH_MATERIAL
{
    char    Name[MAX_MATERIAL_NAME];

    // Use MaterialInstancePath
    char    MaterialInstancePath[MAX_MATERIAL_PATH];

    // Or fall back to d3d8-type materials
    char    DiffuseTexture[MAX_TEXTURE_NAME];
    char    NormalTexture[MAX_TEXTURE_NAME];
    char    SpecularTexture[MAX_TEXTURE_NAME];

    glm::vec4 Diffuse;
    glm::vec4 Ambient;
    glm::vec4 Specular;
    glm::vec4 Emissive;
    FLOAT Power;

    UINT64 Force64_1;			//Force the union to 64bits
    UINT64 Force64_2;			//Force the union to 64bits
    UINT64 Force64_3;			//Force the union to 64bits
    UINT64 Force64_4;			//Force the union to 64bits
    UINT64 Force64_5;		    //Force the union to 64bits
    UINT64 Force64_6;			//Force the union to 64bits

};

struct SDKMESH_MESH
{
    char    Name[MAX_MESH_NAME];
    BYTE NumVertexBuffers;
    UINT    VertexBuffers[MAX_VERTEX_STREAMS];
    UINT IndexBuffer;
    UINT NumSubsets;
    UINT NumFrameInfluences; //aka bones

    glm::vec3 BoundingBoxCenter;
    glm::vec3 BoundingBoxExtents;

    union
    {
        UINT64 SubsetOffset;	//Offset to list of subsets (This also forces the union to 64bits)
        UINT* pSubsets;	    //Pointer to list of subsets
    };
    union
    {
        UINT64 FrameInfluenceOffset;  //Offset to list of frame influences (This also forces the union to 64bits)
        UINT* pFrameInfluences;      //Pointer to list of frame influences
    };
};

struct SDKMESH_FRAME
{
    char Name[MAX_FRAME_NAME];
    UINT Mesh;
    UINT ParentFrame;
    UINT ChildFrame;
    UINT SiblingFrame;
    glm::mat4 Matrix;
    UINT AnimationDataIndex;		//Used to index which set of keyframes transforms this frame
};

struct SDKMESH_VERTEX {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec3 tangent;
};