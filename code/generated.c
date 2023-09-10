// generated by build.c, do not edit by hand

typedef enum EntityID {
    EntityID_None,
    EntityID_Commando,
    EntityID_Lemurian,
    EntityID_Count,
} EntityID;

typedef enum AnimationID {
    AnimationID_None,
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

