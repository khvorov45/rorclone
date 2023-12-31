// generated by build.c, do not edit by hand

typedef enum EntityID {
    EntityID_Commando,
    EntityID_Lemurian,
    EntityID_Count,
} EntityID;

typedef enum StageID {
    StageID_Stage1,
    StageID_Stage2,
    StageID_Count,
} StageID;

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
    AtlasID_Stage1_variant1,
    AtlasID_Stage1_variant2,
    AtlasID_Stage2_variant1,
    AtlasID_Stage2_variant2,
    AtlasID_Count,
} AtlasID;

static const int globalFirstAtlasIDEntities[EntityID_Count] = {
    [EntityID_Commando] = AtlasID_Commando_Idle_frame1,
    [EntityID_Lemurian] = AtlasID_Lemurian_Idle_frame1,
};

static const int globalFirstAtlasIDStages[StageID_Count] = {
    [StageID_Stage1] = AtlasID_Stage1_variant1,
    [StageID_Stage2] = AtlasID_Stage2_variant1,
};

static const int globalAnimationCumulativeFrameCounts[AnimationID_Count] = {
    [AnimationID_Commando_Idle] = 0,
    [AnimationID_Commando_Walk] = 1,
    [AnimationID_Lemurian_Idle] = 0,
};

static const int globalStageVariantCounts[StageID_Count] = {
    [StageID_Stage1] = 2,
    [StageID_Stage2] = 2,
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
        Rect collision[2];
    } entities;
    struct {
        int w;
        int h;
        unsigned int pixels[172176];
        AtlasLocation locations[10];
    } atlas;
    struct {
        f32 allData[4];
        f32arr elements[3];
    } animations;
    struct {
        u8 allData[95548];
        u8arr elements[4];
    } shaders;
    struct {
        V2 allData[56];
        V2arr midData[8];
        V2arrarr elements[4];
    } stages;
} AssetData;
#pragma pack(pop)

static void assetDataAfterLoad(AssetData* adata) {
    for (u32 ind = 0; ind < 3; ind++) {adata->animations.elements[ind].ptr = adata->animations.allData + (u64)adata->animations.elements[ind].ptr;}
    for (u32 ind = 0; ind < 4; ind++) {adata->shaders.elements[ind].ptr = adata->shaders.allData + (u64)adata->shaders.elements[ind].ptr;}
    for (u32 ind = 0; ind < 4; ind++) {adata->stages.elements[ind].ptr = adata->stages.midData + (u64)adata->stages.elements[ind].ptr;}
    for (u32 ind = 0; ind < 8; ind++) {adata->stages.midData[ind].ptr = adata->stages.allData + (u64)adata->stages.midData[ind].ptr;}
}
