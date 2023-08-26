struct VS_INPUT {
    uint   index : SV_VertexID;
    float2 pos   : POSITION;
    float2 dim   : DIM;
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
    float2 cameraPos;
    float cameraHalfSpanX;
    float cameraHeightOverWidth;
}

sampler sampler0 : register(s0);

Texture2D<float4> texture0 : register(t0);

PS_INPUT vs(VS_INPUT input) {
    float2 windowDim = float2(2560, 1440); // TODO(khvorov) Pass in

    float2 cameraHalfSpan = float2(cameraHalfSpanX, cameraHalfSpanX * cameraHeightOverWidth);
    float2 posInCamera = input.pos - cameraPos;
    float2 posInClip = posInCamera / cameraHalfSpan;
    float2 dimInClip = input.dim / cameraHalfSpan;
    float2 scaleInClip = dimInClip * 0.5;

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

    float2 dimInPx = scaleInClip * windowDim;
    float2 pxHalfUV = 1 / dimInPx * 0.5;

    PS_INPUT output;
    output.pos = float4(thisVertex, 0, 1);
    output.uvtop = thisUV.y - pxHalfUV.y;
    output.uvleft = thisUV.x - pxHalfUV.x;
    output.uvbottom = thisUV.y + pxHalfUV.y;
    output.uvright = thisUV.x + pxHalfUV.x;
    output.color = float4(1, 1, 1, 1);
    return output;
}

float4 ps(PS_INPUT input) : SV_TARGET {
    // TODO(khvorov) Gamma
    float2 texDim = float2(32, 32); // TODO(khvorov) Pass in

    float4 texColortopleft = texture0.Sample(sampler0, float2(input.uvleft, input.uvtop));
    float4 texColortopright = texture0.Sample(sampler0, float2(input.uvright, input.uvtop));
    float4 texColorbottomleft = texture0.Sample(sampler0, float2(input.uvleft, input.uvbottom));
    float4 texColorbottomright = texture0.Sample(sampler0, float2(input.uvright, input.uvbottom));

    float2 texcoordtopleft = float2(input.uvleft, input.uvtop) * texDim;
    float2 blendfactorTexcoord = ceil(texcoordtopleft) - texcoordtopleft;

    float2 pxDimTexcoord = float2(input.uvright - input.uvleft, input.uvbottom - input.uvtop) * texDim;
    float2 blendfactor = clamp(float2(blendfactorTexcoord) / pxDimTexcoord, 0, 1);

    float4 blendedtop = blendfactor.x * texColortopleft + (1 - blendfactor.x) * texColortopright;
    float4 blendedbottom = blendfactor.x * texColorbottomleft + (1 - blendfactor.x) * texColorbottomright;
    float4 blended = blendfactor.y * blendedtop + (1 - blendfactor.y) * blendedbottom;

    float4 result = input.color * blended;
    return result;
}
