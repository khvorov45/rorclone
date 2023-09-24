// generated by build.c, do not edit by hand

typedef enum EntityID {
    EntityID_Commando,
    EntityID_Lemurian,
    EntityID_Count,
} EntityID;

typedef enum AnimationID {
    AnimationID_Commando_Idle,
    AnimationID_Commando_Walk,
    AnimationID_Lemurian_Idle,
    AnimationID_Count,
} AnimationID;

typedef enum AtlasID {
    AtlasID_Whitepx,
    AtlasID_Font,
    AtlasID_Commando_Idle_frame1,
    AtlasID_Commando_Walk_frame1,
    AtlasID_Commando_Walk_frame2,
    AtlasID_Lemurian_Idle_frame1,
    AtlasID_Count,
} AtlasID;

static const int globalFirstAtlasID[EntityID_Count] = {
    [EntityID_Commando] = AtlasID_Commando_Idle_frame1,
    [EntityID_Lemurian] = AtlasID_Lemurian_Idle_frame1,
};

static const int globalAnimationCumulativeFrameCounts[AnimationID_Count] = {
    [AnimationID_Commando_Idle] = 0,
    [AnimationID_Commando_Walk] = 1,
    [AnimationID_Lemurian_Idle] = 0,
};

typedef enum ShaderID {
    ShaderID_screen_vs,
    ShaderID_screen_ps,
    ShaderID_sprite_vs,
    ShaderID_sprite_ps,
    ShaderID_Count,
} ShaderID;

#pragma pack(push)
#pragma pack(1)
typedef struct AssetData {
    struct {
        int glyphW;
    } font;
    struct {
        int w;
        int h;
        unsigned int pixels[23940];
        AtlasLocation locations[6];
    } atlas;
    struct {
        f32 allData[4];
        f32arr elements[3];
    } animations;
    struct {
        u8 allData[95520];
        u8arr elements[4];
    } shaders;
} AssetData;
#pragma pack(pop)

static void assetDataAfterLoad(AssetData* adata) {
    for (u32 ind = 0; ind < 3; ind++) {adata->animations.elements[ind].ptr = adata->animations.allData + (u64)adata->animations.elements[ind].ptr;}
    for (u32 ind = 0; ind < 4; ind++) {adata->shaders.elements[ind].ptr = adata->shaders.allData + (u64)adata->shaders.elements[ind].ptr;}
}
