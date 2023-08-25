struct VS_INPUT {
    uint   index : SV_VertexID;
    float2 pos   : POSITION;
    float2 dim   : DIM;
};

struct PS_INPUT {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD;
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

    PS_INPUT output;
    output.pos = float4(thisVertex, 0, 1);
    output.uv = thisUV;
    output.color = float4(1, 1, 1, 1);
    return output;
}

float4 ps(PS_INPUT input) : SV_TARGET {
    // TODO(khvorov) Blend the pixels that are in-between the texels
    float4 tex = texture0.Sample(sampler0, input.uv);
    float4 result = input.color * tex;
    return result;
}
