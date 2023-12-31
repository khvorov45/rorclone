struct VS_INPUT {
    uint index : SV_VertexID;
    float2 topleft : TOPLEFT;
    float2 dim : DIM;
    float2 texInAtlasTopleft : TEX_POS;
    float2 texInAtlasDim : TEX_DIM;
    float4 color : COLOR;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : UV;
    float4 color : COLOR;
};

cbuffer cbuffer0 : register(b0) {
    float2 windowDim;
    float2 atlasDim;
}

sampler sampler0 : register(s0);

Texture2D<float4> texture0 : register(t0);

PS_INPUT vs(VS_INPUT input) {
    float2 offsetForCenter = input.dim / 2;
    float2 offsetPos = input.topleft + offsetForCenter;
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

    PS_INPUT output;
    output.pos = float4(thisVertex, 0, 1);
    output.uv = thisUV;
    output.color = input.color;
    return output;
}

float4 ps(PS_INPUT input) : SV_TARGET {
    float4 texColor = texture0.Sample(sampler0, input.uv);
    float4 result = input.color * texColor;
    return result;
}
