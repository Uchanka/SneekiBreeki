// Copyright (c) 2023 Moore Threads Technology Co. Ltd. All rights reserved.
#pragma warning(error: 3206)

#define mtss_float half
#define mtss_float2 half2
#define mtss_float3 half3
#define mtss_float4 half4

uint2 ZOrder2DMTSS(uint Index, const uint SizeLog2)
{
    uint2 Coord = 0;
    [unroll]
    for (uint i = 0; i < SizeLog2; i++)
    {
        Coord.x |= ((Index >> (2 * i + 0)) & 0x1) << i;
        Coord.y |= ((Index >> (2 * i + 1)) & 0x1) << i;
    }

    return Coord;
}

//#define UNREAL_ENGINE_COORDINATES
#define NVRHI_DONUT_COORDINATES

//#define DEPTH_LESSER_CLOSER
#define DEPTH_GREATER_CLOSER

#define DepthFirst19DigitsMask 0xFFFFE000
#define DepthFirst31DigitsMask 0xFFFFFFFE

#define MaxDepthFirst19Digits 0xFFFFE000
#define MinDepthFirst19Digits 0x00000000

#define IndexLast13DigitsMask 0x00001FFF

#define UnwrittenLast13DigitsMask 0x00000000

#define UnwrittenLast1DigitMT1 0x00000000
#define WrittenLast1DigitMT1 0x00000001

#ifdef DEPTH_LESSER_CLOSER
static uint UnwrittenPackedClearValue = MaxDepthFirst19Digits | UnwrittenLast13DigitsMask;
#endif
#ifdef DEPTH_GREATER_CLOSER
static uint UnwrittenPackedClearValue = MinDepthFirst19Digits | UnwrittenLast13DigitsMask;
#endif
static uint UnwrittenIndexIndicator = UnwrittenLast13DigitsMask;
static uint UnwrittenMTSSIndicator = UnwrittenLast1DigitMT1;
static uint WrittenMTSSIndicator = WrittenLast1DigitMT1;

static float ImpossibleMotionValue = 1.0f; //Have to use this 2's power to prevent floating point gimmicks
static float ImpossibleMotionOffset = 2.0f; //Have to use this 2's power to prevent floating point gimmicks

//static int depthTotalBits = 19;
static int expCustomized = 7;
static int manCustomized = 12;

//Nasha depth: No sig, 7bits exp, 12bits mantissa
uint compressDepth(float incomingDepth)
{
    float incomingAs32F = float(incomingDepth);
    uint incoming32Uint = asuint(incomingAs32F);
	
    int sig32 = (incoming32Uint >> 31) & 0x1;
    int exp32 = (incoming32Uint >> 23) & 0xFF;
    int man32 = incoming32Uint & 0x7FFFFF;
	
    int sig19 = sig32; //Not gonna use it
    int exp19 = exp32 - 127 + ((1 << (expCustomized - 1)) - 1);
    int man19 = man32 >> (23 - manCustomized);

    if (exp19 <= 0)
    {
        int man32Denorm = (man32 | (1 << 24)) >> (1 - exp19);
        man19 = man32Denorm >> (23 - manCustomized);
        if (man32Denorm & (1 << (23 - manCustomized - 1)))
        {
            man19 += 1;
        }
        exp19 = 0;
    }
	
    uint returning19Uint = 0;
	//returning16Uint |= (sig16 << 15);
    returning19Uint |= (exp19 << manCustomized);
    returning19Uint |= man19;
	
    return (returning19Uint << (32 - (expCustomized + manCustomized))) & DepthFirst19DigitsMask;
}

mtss_float SafeRcp(mtss_float x)
{
    return x > 0.0 ? rcp(float(x)) : 0.0;
}

mtss_float3 SafeRcp3(mtss_float3 x)
{
    return float3(SafeRcp(x.r), SafeRcp(x.g), SafeRcp(x.b));
}

mtss_float SafeRcpRet1(mtss_float x)
{
    return x > 0.0 ? rcp(float(x)) : 1.0;
}

mtss_float3 SafeRcp3Ret1(mtss_float3 x)
{
    return float3(SafeRcpRet1(x.r), SafeRcpRet1(x.g), SafeRcpRet1(x.b));
}

mtss_float SafeRcpRetAlot(mtss_float x)
{
    return x > 0.0 ? rcp(float(x)) : 1000000.0;
}

// Some bright pixel can cause HdrWeight to get nullified under fp16 representation. So clamping this to a value close to the minimum float float positive value (0.000061).
#define HDR_WEIGHT_SAFE_MIN_VALUE 0.0001

// Faster but less accurate luma computation. 
// Luma includes a scaling by 4.
mtss_float Luma4(mtss_float3 Color)
{
    return (Color.g * mtss_float(2.0)) + (Color.r + Color.b);
}

mtss_float HdrWeightY(mtss_float Color)
{
	mtss_float Exposure = mtss_float(1.0);

    return max(mtss_float(HDR_WEIGHT_SAFE_MIN_VALUE), rcp(Color * Exposure + mtss_float(4.0)));
}

mtss_float HdrWeightInvY(mtss_float Color)
{
    return mtss_float(4.0) * rcp(mtss_float(1.0) - Color);
}

// Optimized HDR weighting function.
mtss_float HdrWeight4(mtss_float3 Color)
{
    return HdrWeightY(Luma4(Color));
}

float2 ComputeStaticVelocityTipTop(float2 ScreenPos, float DeviceZ, float4x4 TopClipToTipClip)
{
    float3 PosN = float3(ScreenPos, DeviceZ);

    float4 ThisClip = float4(PosN, 1);
    float4 PrevClip = mul(ThisClip, TopClipToTipClip);
    float2 PrevScreen = PrevClip.xy / PrevClip.w;
    return float2(PosN.xy - PrevScreen);
}

float2 ComputeStaticVelocityTopTip(float2 ScreenPos, float DeviceZPrev, float4x4 TipClipToTopClip)
{
    float3 PosN = float3(ScreenPos, DeviceZPrev);

    float4 PrevClip = float4(PosN, 1);
    float4 ThisClip = mul(PrevClip, TipClipToTopClip);
    float2 ThisScreen = ThisClip.xy / ThisClip.w;
    return float2(ThisScreen - PosN.xy);
}

bool IsOffScreen(uint bCameraCut, float2 ScreenPos)
{
    bool bIsCameraCut = bCameraCut != 0;
    bool bIsOutOfBounds = max(abs(ScreenPos.x), abs(ScreenPos.y)) >= 1.0;

    return (bIsCameraCut || bIsOutOfBounds);
}

#define FOUR_POINTS_TIAN_SIZE 4
#define THREE_BY_THREE_PATCH_SIZE 9
#define THREE_BY_THREE_PATCH_DIM 3

static const int subsampleCount4PointTian = 4;
static const int subsampleCount5PointStencil = 5;
static const int subsampleCount9PointPatch = 9;

static const int2 subsamplePixelOffset4PointTian[FOUR_POINTS_TIAN_SIZE] =
{
    int2(0, 0), //K
	
	int2(0, 1),
	int2(1, 0),
	int2(1, 1)
};

static const int2 subsamplePixelOffset5PointStencil[5] =
{
    int2(0, 0), // K
	
	int2(0, -1),
	int2(-1, 0),
	int2(1, 0),
	int2(0, 1)
};

static const int2 subsamplePixelOffset9PointPatch[THREE_BY_THREE_PATCH_SIZE] =
{
    int2(0, 0), // K
	
	int2(-1, -1),
	int2(0, -1),
	int2(1, -1),
	int2(-1, 0),
	int2(1, 0),
	int2(-1, 1),
	int2(0, 1),
	int2(1, 1)
};

mtss_float gaussianDistributionWeightForVariance(float2 offset, float patchSize)
{
    return exp(-3.0f * (offset.x * offset.x + offset.y * offset.y) / ((patchSize + 1.0f) * (patchSize + 1.0f)));
}

static const mtss_float lanzcosPie = 3.1415926535897932f;
static const mtss_float lanzcosWidth = 2.0f;

mtss_float LanzcosEachDim(mtss_float diff)
{
    if (abs(diff) < 0.00001f)
    {
        return 1.0f;
    }
    else if (abs(diff) >= lanzcosWidth)
    {
        return 0.0f;
    }
    else
    {
		mtss_float nominator = lanzcosWidth * sin(lanzcosPie * diff) * sin(lanzcosPie * diff / lanzcosWidth);
		mtss_float denominator = lanzcosPie * lanzcosPie * diff * diff;
        return nominator * SafeRcp(denominator);
    }
}

mtss_float UpsampleLanzcos(mtss_float2 diff, mtss_float upsampleFactor)
{
    diff *= (upsampleFactor);
	mtss_float contributionX = LanzcosEachDim(diff.x);
	mtss_float contributionY = LanzcosEachDim(diff.y);
    return contributionX * contributionY;
}

mtss_float UpsampleFilterGaussian(mtss_float2 diff, mtss_float upsampleFactor)
{
	mtss_float u2 = upsampleFactor * upsampleFactor;
	// 1 - 1.9 * x^2 + 0.9 * x^4
	mtss_float x2 = saturate(u2 * dot(diff, diff));
    return mtss_float(((mtss_float(0.9f)) * x2 - mtss_float(1.9f)) * x2 + mtss_float(1.0f));
}

mtss_float GetUpsampleKernelWeight(mtss_float2 diff, mtss_float upsampleFactor, mtss_float minimalContribution)
{
	//mtss_float kernelWeight = UpsampleBicubic(diff, upsampleFactor);
	mtss_float kernelWeight = UpsampleLanzcos(diff, upsampleFactor);
	//mtss_float kernelWeight = UpsampleFilterTent(diff, 2.0f * SafeRcp(upsampleFactor));
	//mtss_float kernelWeight = UpsampleFilterGaussian(diff, upsampleFactor);
	//mtss_float kernelWeight = UpsampleFilterUniversal(diff, SafeRcp(upsampleFactor));
    return max(kernelWeight, minimalContribution);
}

mtss_float GetBlunterKernelWeight(mtss_float2 diff, mtss_float upsampleFactor, mtss_float minimalContribution)
{
	//mtss_float kernelWeight = UpsampleBicubic(diff, 1.0f);
	mtss_float kernelWeight = UpsampleFilterGaussian(diff, 0.45f);
	//mtss_float kernelWeight = UpsampleFilterTent(diff, 2.0f * SafeRcp(upsampleFactor));
	//mtss_float kernelWeight = UpsampleFilterGaussian(diff, upsampleFactor);
	//mtss_float kernelWeight = UpsampleFilterUniversal(diff, SafeRcp(upsampleFactor));
    return max(kernelWeight, minimalContribution);
}

float3 HSVtoRGB(float3 hsv)
{
    float h = hsv.x;
    float s = hsv.y;
    float v = hsv.z;

    const float PI = 3.14159265358979;
    float3 rgb = (float3) v;

    if (s > 0)
    {
        h = fmod(h + 2.0 * PI, 2.0 * PI);
        h /= (PI / 3.0);
        int i = int(floor(h));
        float f = h - i;
        float p = v * (1.0 - s);
        float q = v * (1.0 - (s * f));
        float t = v * (1.0 - (s * (1.0 - f)));

        switch (i)
        {
            case 0:
                rgb = float3(v, t, p);
                break;
            case 1:
                rgb = float3(q, v, p);
                break;
            case 2:
                rgb = float3(p, v, t);
                break;
            case 3:
                rgb = float3(p, q, v);
                break;
            case 4:
                rgb = float3(t, p, v);
                break;
            default:
                rgb = float3(v, p, q);
                break;
        }
    }
    return rgb;
}

float3 heatMap(float value, float lb = 0.0, float ub = 1.0)
{
    float p = saturate((value - lb) / (ub - lb));

    float r, g, b;
    float h = 3.7 * (1.0 - p); // 3.7 is blue
    float s = sqrt(p);
    float v = sqrt(p);
    return HSVtoRGB(float3(h, s, v));
}

float pseudoNormalizedSigmoid(float x, float power = 3.0f)
{
    x = saturate(x);
    float oneMinusX = 1.0f - x;
    float xPowered = pow(x, power);
    float oneMinusXPowered = pow(oneMinusX, power);
    return xPowered / (xPowered + oneMinusXPowered);
}

// Apply this to tonemap linear HDR color "c" after a sample is fetched in the resolve.
// Note "c" 1.0 maps to the expected limit of low-dynamic-range monitor output.

#define TONEMAPPING_ENABLED

float3 Tonemap(float3 c)
{
#ifdef TONEMAPPING_ENABLED
    return c * rcp(c + 1.0f);
#else
	return c;
#endif
}

// When the filter kernel is a weighted sum of fetched colors,
// it is more optimal to fold the weighting into the tonemap operation.
float3 TonemapWithWeight(float3 c, float w)
{
#ifdef TONEMAPPING_ENABLED
    return c * (w * rcp(c + 1.0f));
#else
	return c * w;
#endif	
}

// Apply this to restore the linear HDR color before writing out the result of the resolve.
float3 TonemapInvert(float3 c)
{
#ifdef TONEMAPPING_ENABLED
    return c * SafeRcp3Ret1(1.0f - c);
#else
	return c;
#endif
}

// Maps standard viewport UV to screen position.
float2 ViewportUVToScreenPos(float2 ViewportUV)
{
    return float2(2.0f * ViewportUV.x - 1.0f, 1.0f - 2.0f * ViewportUV.y);
}

float2 ScreenPosToViewportUV(float2 ScreenPos)
{
    return float2(0.5f + 0.5f * ScreenPos.x, 0.5f - 0.5f * ScreenPos.y);
}
