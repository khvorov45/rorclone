struct VS_INPUT {
    uint   index : SV_VertexID;
    float2 pos   : POSITION;
    float2 scale : SCALE;
};

struct PS_INPUT {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD;
    float4 color : COLOR;
};

sampler sampler0 : register(s0);

Texture2D<float4> texture0 : register(t0);

PS_INPUT vs(VS_INPUT input) {
    float2 vertices[] = {
        {-1,  1},
        { 1,  1},
        {-1, -1},
        { 1, -1},
    };
    float2 thisVertex = vertices[input.index] * input.scale + input.pos;
    
    float2 uvs[] = {
        {0, 1},
        {1, 1},
        {0, 0},
        {1, 0},
    };
    float2 thisUV = uvs[input.index];

    PS_INPUT output;
    output.pos = float4(thisVertex, 0, 1);
    output.uv = thisUV;
    output.color = float4(1, 1, 1, 1);
    return output;
}

float4 ps(PS_INPUT input) : SV_TARGET {
    float4 tex = texture0.Sample(sampler0, input.uv);
    float4 result = input.color * tex;
    return result;
}
