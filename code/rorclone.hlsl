struct VS_INPUT {
    uint index : SV_VertexID;
    float2 pos : POS;
    float2 dimOrOffset : DIM_OR_OFFSET;
    float2 texInAtlasTopleft : TEX_POS;
    float2 texInAtlasDim : TEX_DIM;
    float4 color : COLOR;
    int mirrorX : MIRRORX;
    int posIsWorld : POS_IS_WORLD;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float uvtop : UVTOP;
    float uvleft : UVLEFT;
    float uvbottom : UVBOTTOM;
    float uvright : UVRIGHT;
    float4 color : COLOR;
};

cbuffer cbuffer0 : register(b0) {
    float2 windowDim;
    float2 atlasDim;
    float2 cameraPos;
    float cameraHalfSpanX;
    float cameraHeightOverWidth;
}

sampler sampler0 : register(s0);

Texture2D<float4> texture0 : register(t0);

PS_INPUT vs(VS_INPUT input) {

    float2 posInClip;
    float2 dimInClip;
    if (input.posIsWorld) {
        float2 inputOffset = input.dimOrOffset;
        float2 cameraHalfSpan = float2(cameraHalfSpanX, cameraHalfSpanX * cameraHeightOverWidth);

        float2 offsetForCenter = input.texInAtlasDim / 2 - inputOffset;
        offsetForCenter.y *= -1;
        if (input.mirrorX) offsetForCenter.x *= -1;

        float2 offsetPos = input.pos + offsetForCenter;
        float2 posInCamera = offsetPos - cameraPos;
        posInClip = posInCamera / cameraHalfSpan;
        dimInClip = input.texInAtlasDim / cameraHalfSpan;
    } else {
        float2 inputDim = input.dimOrOffset;

        float2 offsetForCenter = inputDim / 2;
        float2 offsetPos = input.pos + offsetForCenter;
        posInClip = offsetPos / windowDim * 2 - 1;
        posInClip.y *= -1;

        dimInClip = inputDim / windowDim * 2;
    }

    float2 scaleInClip = dimInClip * 0.5;
    float2 posInUV = input.texInAtlasTopleft / atlasDim;
    float2 scaleInUV = input.texInAtlasDim / atlasDim;

    float2 vertices[] = {
        {-1,  1},
        { 1,  1},
        {-1, -1},
        { 1, -1},
    };
    float2 thisVertex = vertices[input.index] * scaleInClip + posInClip;

    float2 uvs[] = {
        {0, 0},
        {1, 0},
        {0, 1},
        {1, 1},
    };
    float2 thisUV = uvs[input.index];
    if (input.mirrorX) thisUV.x = (thisUV.x - 1) * -1;
    thisUV = thisUV * scaleInUV + posInUV;

    float2 dimInPx = scaleInClip * windowDim;
    float2 pxHalfUV = 0.5 / dimInPx * scaleInUV;

    PS_INPUT output;
    output.pos = float4(thisVertex, 0, 1);
    output.uvtop = thisUV.y - pxHalfUV.y;
    output.uvleft = thisUV.x - pxHalfUV.x;
    output.uvbottom = thisUV.y + pxHalfUV.y;
    output.uvright = thisUV.x + pxHalfUV.x;
    output.color = input.color;
    return output;
}

float4 ps(PS_INPUT input) : SV_TARGET {
    float4 texColortopleft = texture0.Sample(sampler0, float2(input.uvleft, input.uvtop));
    float4 texColortopright = texture0.Sample(sampler0, float2(input.uvright, input.uvtop));
    float4 texColorbottomleft = texture0.Sample(sampler0, float2(input.uvleft, input.uvbottom));
    float4 texColorbottomright = texture0.Sample(sampler0, float2(input.uvright, input.uvbottom));

    float2 texcoordtopleft = float2(input.uvleft, input.uvtop) * atlasDim;
    float2 blendfactorTexcoord = ceil(texcoordtopleft) - texcoordtopleft;

    float2 pxDimTexcoord = float2(input.uvright - input.uvleft, input.uvbottom - input.uvtop) * atlasDim;
    float2 blendfactor = clamp(float2(blendfactorTexcoord) / pxDimTexcoord, 0, 1);

    float4 blendedtop = blendfactor.x * texColortopleft + (1 - blendfactor.x) * texColortopright;
    float4 blendedbottom = blendfactor.x * texColorbottomleft + (1 - blendfactor.x) * texColorbottomright;
    float4 blended = blendfactor.y * blendedtop + (1 - blendfactor.y) * blendedbottom;

    float4 result = input.color * blended;
    return result;
}
