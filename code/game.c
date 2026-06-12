#include "common.c"
#include "generated.c"

//
// SECTION Input
//

typedef enum InputKeyID {
    InputKeyID_None,

    InputKeyID_Left,
    InputKeyID_Right,
    InputKeyID_Up,
    InputKeyID_Down,
    InputKeyID_Accelerate,

    InputKeyID_Count,
} InputKeyID;

typedef struct InputKey {
    bool down;
    i32 halfTransitionCount;
} InputKey;

typedef struct Input {
    InputKey keys[InputKeyID_Count];
} Input;

static bool wasPressed(Input* input, InputKeyID id) {
    InputKey* key = input->keys + id;
    bool result = (key->halfTransitionCount > 1) || (key->down && key->halfTransitionCount > 0);
    return result;
}

//
// SECTION Misc
//

static AtlasID getAtlasID(EntityID entity, AnimationID animation, i32 animationFrame) {
    i32 firstAtlasID = globalFirstAtlasIDEntities[entity];
    i32 animationOffset = globalAnimationCumulativeFrameCounts[animation];
    AtlasID id = (AtlasID)(firstAtlasID + animationOffset + animationFrame);
    return id;
}

typedef struct SpriteCommon {
    // NOTE(khvorov) This is the visual top-left of the sprite in the first frame of any animation
    // (first frames of all animations for a given sprite are aligned with each other)
    V2 topleft;
    i32 mirrorX;
} SpriteCommon;

typedef struct Sprite {
    SpriteCommon common;
    EntityID entity;
    AnimationID animationID;
    i32 animationFrame;
    i32 currentAnimationCounterMS;
    bool isGrounded;
} Sprite;

typedef struct ScreenRect {
    Rect scr;
    Rect texInAtlas;
    V4 color;
} ScreenRect;

typedef struct GameSprites {Sprite* ptr; i64 len; i64 cap;} GameSprites;
typedef struct GameScreenRects {ScreenRect* ptr; i64 len; i64 cap;} GameScreenRects;
typedef struct Game {
    AssetData* assets;
    GameSprites sprites;
    GameScreenRects screenRects;
    f32 spriteScaleMultiplier;
    V2 cameraPos;
    struct {i32 w, h;} window;
} Game;

static void drawGlyph(Game* game, char glyph, V2 topleft, V4 color) {
    i32 glyphXOffset = (i32)glyph * (game->assets->font.glyphW + 2);
    Rect atlas = game->assets->atlas.locations[AtlasID_Font].rect;
    Rect glyphRect = {.topleft = {atlas.topleft.x + glyphXOffset, atlas.topleft.y}, .dim = {game->assets->font.glyphW + 2, atlas.dim.y}};
    dynarrpush(&game->screenRects, ((ScreenRect) {.scr.topleft = topleft, .scr.dim = glyphRect.dim, .texInAtlas = glyphRect, .color = color}));
}

static void drawStr(Game* game, Str str, V2 topleft, V4 color) {
    V2 currentTopleft = topleft;
    for (i64 charIndex = 0; charIndex < str.len; charIndex++) {
        char ch = str.ptr[charIndex];
        drawGlyph(game, ch, currentTopleft, color);
        currentTopleft.x += game->assets->font.glyphW;
    }
}

static V2 worldToScreenV2(V2 pos, V2 cameraPos, f32 spriteScaleMultiplier, i32 windowW, i32 windowH) {
    V2 posCamera = v2sub(pos, cameraPos);
    V2 posPxFromCenter = v2scale(posCamera, spriteScaleMultiplier);
    posPxFromCenter.y *= -1;
    V2 windowHalfDim = {(f32)windowW / 2.0f, (f32)windowH / 2.0f};
    V2 posScr = v2add(posPxFromCenter, windowHalfDim);
    return posScr;
}

static Rect worldToScreenRect(Rect rect, V2 cameraPos, f32 spriteScaleMultiplier, i32 windowW, i32 windowH) {
    Rect result = {
        .topleft = worldToScreenV2(rect.topleft, cameraPos, spriteScaleMultiplier, windowW, windowH),
        .dim = v2scale(rect.dim, spriteScaleMultiplier),
    };
    return result;
}

static void drawRect(Game* game, Rect rectWorld, V4 color) {
    Rect topleftRectScr = worldToScreenRect(rectWorld, game->cameraPos, game->spriteScaleMultiplier, game->window.w, game->window.h);
    Rect whitePx = rectShrink(game->assets->atlas.locations[AtlasID_Whitepx].rect, 1);
    ScreenRect pointRect = {.scr = topleftRectScr, .texInAtlas = whitePx, .color = color};
    dynarrpush(&game->screenRects, pointRect);
}
static void drawPoint(Game* game, V2 posWorld, V4 color) { drawRect(game, (Rect) {posWorld, v2fromf32(1)}, color);}

static Game* gameInit(Arena* arena) {
    Game* game = arenaAllocAndZeroOne(arena, Game);
    game->spriteScaleMultiplier = 5.0f,
    game->sprites = (GameSprites) arenaAllocDynarr(arena, Sprite, 1024);
    game->screenRects = (GameScreenRects) arenaAllocDynarr(arena, ScreenRect, 1024);

    // TODO(khvorov) Verify the following:
    // World space is top-down and world space coordinates correspond to pixel coordinates in source art
    // TODO(khvorov) Placeholder, just to have something
    {
        dynarrpush(&game->sprites, ((Sprite) {.common.topleft = {0, 0}, .entity = EntityID_Commando}));
        // dynarrpush(&game->sprites, ((Sprite) {.common.topleft = {0, -20}, .common.mirrorX = true, .entity = EntityID_Commando}));
        // dynarrpush(&game->sprites, ((Sprite) {.common.topleft = {20, -20}, .entity = EntityID_Lemurian}));
    }

    return game;
}

static void gameUpdateAndRender(Game* game, Input* input, f32 msSinceLastUpdate) {
    // TODO(khvorov) Placeholder, only updating one sprite
    Sprite* sprite = game->sprites.ptr;
    assert(sprite->entity == EntityID_Commando);

    // NOTE(khvorov) Collision
    {
        Rect collision = game->assets->entities.collision[sprite->entity];
        V2 currentPos = v2add(sprite->common.topleft, collision.topleft);
        currentPos.y -= collision.dim.y;
        V2 deltaPos = {};
        {
            f32 deltaX = 0.01f * msSinceLastUpdate;
            if (input->keys[InputKeyID_Accelerate].down) { deltaX *= 10.0f;}
            if (input->keys[InputKeyID_Left].down) { deltaPos.x -= deltaX;}
            if (input->keys[InputKeyID_Right].down) {deltaPos.x += deltaX;}
            if (input->keys[InputKeyID_Up].down) {deltaPos.y += deltaX;}
            if (input->keys[InputKeyID_Down].down) {deltaPos.y -= deltaX;}
        }

        for (;;) {
            bool collided = false;
            f32 collisionProp = 1.0f;
            V2 collisionWallUnitV = {};

            // TODO(khvorov) Get colllision polygons from the correct stage
            V2sliceslice stageCollisionPolygons = game->assets->stages.elements[0];

            for (i64 polyIndex = 0; polyIndex < stageCollisionPolygons.len; polyIndex++) {
                V2slice collisionPolygon = stageCollisionPolygons.ptr[polyIndex];
                for (u32 collisionLineIndex = 0; collisionLineIndex < collisionPolygon.len; collisionLineIndex++) {
                    CollisionLine collisionLine = {collisionPolygon.ptr[collisionLineIndex], collisionPolygon.ptr[(collisionLineIndex + 1) % collisionPolygon.len]};

                    V2 wallVector = v2sub(collisionLine.p2, collisionLine.p1);
                    V2 wallUnitV = v2normalize(wallVector);
                    V2 wallNormal = v2xyquaterturn(wallUnitV);

                    f32 wallShift = v2dot(wallNormal, collision.dim);
                    f32 wallShiftClamped = min(wallShift, 0);
                    V2 wallShiftV = v2scale(wallNormal, absval(wallShiftClamped));

                    V2 wallBound1Og = v2add(collisionLine.p1, wallShiftV);
                    V2 wallBound2Og = v2add(collisionLine.p2, wallShiftV);

                    V2 wallBound1 = {min(wallBound1Og.x, wallBound2Og.x), min(wallBound1Og.y, wallBound2Og.y)};
                    V2 wallBound2 = {max(wallBound1Og.x, wallBound2Og.x), max(wallBound1Og.y, wallBound2Og.y)};

                    f32 wallExt = v2dot(wallUnitV, collision.dim);
                    V2 wallExtV = v2scale(wallUnitV, wallExt);
                    wallBound1 = v2sub(wallBound1, wallExtV);

                    V2 currentToBound1 = v2sub(wallBound1, currentPos);
                    f32 currentDotWallNormal = v2dot(currentToBound1, wallNormal);
                    if (currentDotWallNormal <= 0) {
                        V2 newPos = v2add(currentPos, deltaPos);
                        V2 newToBound1 = v2sub(wallBound1, newPos);
                        f32 newDotWallNormal = v2dot(newToBound1, wallNormal);
                        if (newDotWallNormal > 0) {
                            V2 currentToBound2 = v2sub(wallBound2, currentPos);
                            f32 deltaOuterBound1 = v2outer(deltaPos, currentToBound1);
                            f32 deltaOuterBound2 = v2outer(deltaPos, currentToBound2);
                            if (deltaOuterBound1 * deltaOuterBound2 <= 0) {
                                collided = true;
                                f32 currentDotWallNormalMag = absval(currentDotWallNormal);
                                f32 newDotWallNormalMag = absval(newDotWallNormal);
                                f32 thisCollisionProp = currentDotWallNormalMag / (currentDotWallNormalMag + newDotWallNormalMag);
                                if (thisCollisionProp < collisionProp) {
                                    collisionProp = thisCollisionProp;
                                    collisionWallUnitV = wallUnitV;
                                }
                            }
                        }
                    }
                }
            }

            if (collided) {
                assert(collisionProp >= 0 && collisionProp <= 1);
                V2 clippedDelta = v2scale(deltaPos, collisionProp);
                currentPos = v2add(currentPos, clippedDelta);
                V2 remainingDelta = v2scale(deltaPos, 1 - collisionProp);
                f32 remainingDeltaAlongWall = v2dot(remainingDelta, collisionWallUnitV);
                V2 remainingDeltaModded = v2scale(collisionWallUnitV, remainingDeltaAlongWall);
                deltaPos = remainingDeltaModded;
            } else {
                currentPos = v2add(currentPos, deltaPos);
                break;
            }
        }

        currentPos.y += collision.dim.y;
        sprite->common.topleft = v2sub(currentPos, collision.topleft);

        // TODO(khvorov) Handle grounding
        // if (!sprite->isGrounded) {
        //     testPos.y -= 0.1f * msSinceLastUpdate;
        // }
    }

    if (input->keys[InputKeyID_Left].down) { sprite->common.mirrorX = true; }
    if (input->keys[InputKeyID_Right].down) { sprite->common.mirrorX = false; }

    if ((input->keys[InputKeyID_Left].down || input->keys[InputKeyID_Right].down)) {
        sprite->animationID = AnimationID_Commando_Walk; // TODO(khvorov) Only when grounded
    } else {
        sprite->animationID = AnimationID_Commando_Idle;
    }

    {
        Animation* animation = (Animation*)game->assets->animations.elements + sprite->animationID;
        if (sprite->currentAnimationCounterMS >= animation->frameDurationsInMS[sprite->animationFrame]) {
            sprite->animationFrame = (sprite->animationFrame + 1) % animation->frameCount;
            sprite->currentAnimationCounterMS = 0;
        } else {
            sprite->currentAnimationCounterMS += msSinceLastUpdate;
        }
    }

    // TODO(khvorov) Temp section
    {
        game->screenRects.len = 0;
        drawStr(game, STR("spritestr"), (V2) {200, 300}, (V4) {.g = 100, .a = 100}); // TODO(khvorov) Temp

        // TODO(khvorov) Temp code to draw collision lines
        V2sliceslice stageCollisionPolygons = game->assets->stages.elements[0];
        for (i64 polyIndex = 0; polyIndex < stageCollisionPolygons.len; polyIndex++) {
            V2slice collisionPolygon = stageCollisionPolygons.ptr[polyIndex];
            for (u32 collisionLineIndex = 0; collisionLineIndex < collisionPolygon.len; collisionLineIndex++) {
                CollisionLine collisionLine = {collisionPolygon.ptr[collisionLineIndex], collisionPolygon.ptr[(collisionLineIndex + 1) % collisionPolygon.len]};

                Rect collisionLineRectWorld = {.topleft = (collisionLine.p1.x < collisionLine.p2.x) || (collisionLine.p1.y > collisionLine.p2.y) ? collisionLine.p1 : collisionLine.p2};
                f32 thickness = 1;
                collisionLineRectWorld.dim = (V2) {v2len(v2sub(collisionLine.p2, collisionLine.p1)), thickness};
                bool vertical = collisionLine.p1.x == collisionLine.p2.x;
                if (vertical) {
                    collisionLineRectWorld.dim.y = collisionLineRectWorld.dim.x;
                    collisionLineRectWorld.dim.x = thickness;
                }

                // TODO(khvorov) Reenable
                // if (collisionLine.type == CollisionLineType_BlockFromRight) {
                //     collisionLineRectWorld.topleft.x -= collisionLineRectWorld.dim.x;
                // }

                // if (collisionLine.type == CollisionLineType_BlockFromBottom) {
                //     collisionLineRectWorld.topleft.y += collisionLineRectWorld.dim.y;
                // }

                drawRect(game, collisionLineRectWorld, (V4) {.r = 1, .g = 1, .b = 1, .a = 0.5});
            }
        }
    }

    // TODO(khvorov) Temp collision shape drawing
    Rect collision = game->assets->entities.collision[sprite->entity];
    drawRect(game, (Rect) {v2add(sprite->common.topleft, collision.topleft), collision.dim}, (V4) {.r = 1, .b = 1, .a = 0.25});
}
