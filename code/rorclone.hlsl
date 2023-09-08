struct VS_INPUT_SPRITE {
    uint index : SV_VertexID;
    float2 pos : POS;
    float2 offset : OFFSET;
    float2 texInAtlasTopleft : TEX_POS;
    float2 texInAtlasDim : TEX_DIM;
    int mirrorX : MIRRORX;
};

struct VS_INPUT_SCREEN {
    uint index : SV_VertexID;
    float2 pos : POS;
    float2 dim : DIM;
    float2 texInAtlasTopleft : TEX_POS;
    float2 texInAtlasDim : TEX_DIM;
    float4 color : COLOR;
};

struct PS_INPUT_SPRITE {
    float4 pos : SV_POSITION;
    float2 uv : UV;
    float uvleft : UVLEFT;
    float uvright : UVRIGHT;
    float uvtop : UVTOP;
    float uvbottom : UVBOTTOM;
};

struct PS_INPUT_SCREEN {
    float4 pos : SV_POSITION;
    float2 uv : UV;
    float4 color : COLOR;
};

cbuffer cbuffer0 : register(b0) {
    float2 windowDim;
    float2 atlasDim;
    float2 cameraPos;
    float spriteScaleMultiplier;
}

sampler sampler0 : register(s0);

Texture2D<float4> texture0 : register(t0);

PS_INPUT_SCREEN vs_screen(VS_INPUT_SCREEN input) {
    float2 offsetForCenter = input.dim / 2;
    float2 offsetPos = input.pos + offsetForCenter;
    float2 posInClip = offsetPos / windowDim * 2 - 1;
    posInClip.y *= -1;

    float2 dimInClip = input.dim / windowDim * 2;

    float2 scaleInClip = dimInClip * 0.5;
    float2 posInUV = input.texInAtlasTopleft / atlasDim;
    float2 scaleInUV = input.texInAtlasDim / atlasDim;

    float2 vertices[] = {{-1,  1},{ 1,  1},{-1, -1},{ 1, -1}};
    float2 thisVertex = vertices[input.index] * scaleInClip + posInClip;

    float2 uvs[] = {{0, 0},{1, 0},{0, 1},{1, 1}};
    float2 thisUV = uvs[input.index] * scaleInUV + posInUV;

    PS_INPUT_SCREEN output;
    output.pos = float4(thisVertex, 0, 1);
    output.uv = thisUV;
    output.color = input.color;
    return output;
}

PS_INPUT_SPRITE vs_sprite(VS_INPUT_SPRITE input) {
    float2 cameraHalfSpan = windowDim / spriteScaleMultiplier / 2;

    float2 scrdim = input.texInAtlasDim * spriteScaleMultiplier;
    float2 wudim = input.texInAtlasDim;
    float2 offsetForCenter = wudim / 2 - input.offset;
    offsetForCenter.y *= -1;
    if (input.mirrorX) offsetForCenter.x *= -1;

    float2 offsetPos = input.pos + offsetForCenter;
    float2 posInCamera = offsetPos - cameraPos;
    float2 posInClip = posInCamera / cameraHalfSpan;
    float2 dimInClip = scrdim / windowDim * 2;

    float2 scaleInClip = dimInClip * 0.5;
    float2 posInUV = input.texInAtlasTopleft / atlasDim;
    float2 scaleInUV = input.texInAtlasDim / atlasDim;

    float2 vertices[] = {{-1,  1},{ 1,  1},{-1, -1},{ 1, -1}};
    float2 thisVertex = vertices[input.index] * scaleInClip + posInClip;

    float2 uvs[] = {{0, 0},{1, 0},{0, 1},{1, 1}};
    float2 thisUV = uvs[input.index];
    if (input.mirrorX) thisUV.x = (thisUV.x - 1) * -1;
    thisUV = thisUV * scaleInUV + posInUV;

    float2 halfPxInUV = float2(0.5, 0.5) / scrdim * scaleInUV;

    PS_INPUT_SPRITE output;
    output.pos = float4(thisVertex, 0, 1);
    output.uv = thisUV;
    output.uvleft = thisUV.x - halfPxInUV.x;
    output.uvright = thisUV.x + halfPxInUV.x;
    output.uvtop = thisUV.y - halfPxInUV.y;
    output.uvbottom = thisUV.y + halfPxInUV.y;
    return output;
}

float4 ps_sprite(PS_INPUT_SPRITE input) : SV_TARGET {
    float texLeft = input.uvleft * atlasDim.x;
    float texRight = input.uvright * atlasDim.x;
    float texTop = input.uvtop * atlasDim.y;
    float texBottom = input.uvbottom * atlasDim.y;

    float leftCeil = ceil(texLeft);
    float rightFloor = floor(texRight);
    float topCeil = ceil(texTop);
    float bottomFloor = floor(texBottom);

    bool hblend = leftCeil == rightFloor;
    bool vblend = topCeil == bottomFloor;
    float4 result;
    if (hblend && vblend) {
        float4 texColorTopleft = texture0.Sample(sampler0, float2(input.uvleft, input.uvtop));
        float4 texColorTopright = texture0.Sample(sampler0, float2(input.uvright, input.uvtop));
        float4 texColorBottomleft = texture0.Sample(sampler0, float2(input.uvleft, input.uvbottom));
        float4 texColorBottomright = texture0.Sample(sampler0, float2(input.uvright, input.uvbottom));

        float2 blendFactor = float2(leftCeil - texLeft, topCeil - texTop) * spriteScaleMultiplier;
        float4 texColorTop = lerp(texColorTopright, texColorTopleft, blendFactor.x);
        float4 texColorBottom = lerp(texColorBottomright, texColorBottomleft, blendFactor.x);
        result = lerp(texColorBottom, texColorTop, blendFactor.y);
    } else if (hblend) {
        float4 texColorLeft = texture0.Sample(sampler0, float2(input.uvleft, input.uv.y));
        float4 texColorRight = texture0.Sample(sampler0, float2(input.uvright, input.uv.y));
        float blendFactor = (leftCeil - texLeft) * spriteScaleMultiplier;
        result = lerp(texColorRight, texColorLeft, blendFactor);
    } else if (vblend) {
        float4 texColorTop = texture0.Sample(sampler0, float2(input.uv.x, input.uvtop));
        float4 texColorBottom = texture0.Sample(sampler0, float2(input.uv.x, input.uvbottom));
        float blendFactor = (topCeil - texTop) * spriteScaleMultiplier;
        result = lerp(texColorBottom, texColorTop, blendFactor);
    } else {
        result = texture0.Sample(sampler0, input.uv);
    }

    return result;
}

float4 ps_screen(PS_INPUT_SCREEN input) : SV_TARGET {
    float4 texColor = texture0.Sample(sampler0, input.uv);
    float4 result = input.color * texColor;
    return result;
}
