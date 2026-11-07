// Reference source for the shader embedded in src/core/gfx/HdrToneMapper.cpp.
// The embedded copy must stay in sync with this file; it is compiled at runtime
// with D3DCompile.
//
// Converts an HDR source (FP16 scRGB or R10G10B10A2 PQ) to a BGRA8 SDR image:
//   - pq_mode == 0: samples are linear scRGB, scaled by an 80 nit SDR reference.
//   - pq_mode == 1: samples are BT.2100 PQ encoded; decoded to linear nits.
// The linear BT.2020 values are mapped to BT.709 (PQ only; scRGB is already
// BT.709), normalized to the SDR white point, tone mapped with the ACES
// approximation, and encoded to sRGB.

Texture2D<float4> src : register(t0);
SamplerState samp : register(s0);
cbuffer Params : register(b0) {
    float sdr_white_nits;
    float max_nits;  // reserved for a future highlight-rolloff extension
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
float3 AcesToneMap(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
float4 PSMain(VSOut input) : SV_TARGET {
    float4 sample = src.Sample(samp, input.uv);
    // BT.2087 BT.2020 -> BT.709. scRGB already uses BT.709 primaries, so the
    // matrix is only applied on the PQ path. Rows sum to 1.0.
    const float3x3 m = float3x3(
         1.660491, -0.587641, -0.072850,
        -0.124550,  1.132900, -0.008349,
        -0.018151, -0.100579,  1.118730);
    float3 nits = pq_mode ? mul(m, PqToLinear(sample.rgb)) : sample.rgb * 80.0;
    // Out-of-gamut conversion can produce negative components; clamp them to
    // black before the tone curve so saturated colors stay saturated.
    float3 x = max(nits, 0.0) / max(sdr_white_nits, 1.0);
    return float4(LinearToSrgb(AcesToneMap(x)), 1.0);
}
