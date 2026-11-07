// Reference source for the shader embedded in src/core/gfx/HdrToneMapper.cpp.
// The embedded copy must stay in sync with this file; it is compiled at runtime
// with D3DCompile.
//
// Converts an HDR source (FP16 scRGB or R10G10B10A2 PQ) to a BGRA8 SDR image:
//   - pq_mode == 0: samples are linear scRGB, scaled by an 80 nit SDR reference.
//   - pq_mode == 1: samples are BT.2100 PQ encoded; decoded to linear nits.
// The linear BT.2020 values are mapped to BT.709, normalized to SDR white,
// tonemapped with a simple Reinhard curve, and encoded to sRGB.

Texture2D<float4> src : register(t0);
SamplerState samp : register(s0);
cbuffer Params : register(b0) {
    float sdr_white_nits;
    float max_nits;
    int pq_mode;
    int pad0;
};
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = p;
    return o;
}
float3 PqToLinear(float3 pq) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 p = pow(max(pq, 0.0), 1.0 / m2);
    float3 num = max(p - c1, 0.0);
    float3 den = c2 - c3 * p;
    return pow(num / den, 1.0 / m1) * 10000.0;
}
float3 LinearToSrgb(float3 c) {
    c = max(c, 0.0);
    float3 low = c * 12.92;
    float3 high = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(c, 0.0031308));
}
float4 PSMain(VSOut input) : SV_TARGET {
    float4 sample = src.Sample(samp, input.uv);
    float3 nits = pq_mode ? PqToLinear(sample.rgb) : sample.rgb * 80.0;
    const float3x3 m = float3x3(
        1.6605, -0.1246, -0.0182,
       -0.5876,  1.1329, -0.1006,
       -0.0728, -0.0083,  1.1187);
    float3 lin = mul(m, nits);
    lin /= max(sdr_white_nits, 1.0);
    lin = lin / (1.0 + lin * (1.0 / max(max_nits, 1.0)));
    return float4(LinearToSrgb(lin), 1.0);
}
