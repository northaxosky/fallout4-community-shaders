#ifdef BSEFFECT_PS_SOURCE
#if defined(ENVCUBE_RAIN) || defined(ENVCUBE_SNOW)
#define WEATHER
#endif

Texture2D<float4> TexBaseSampler : register(t0);
SamplerState SampBaseSampler : register(s0);
#if defined(PIPBOY_SCREEN)
Texture2D<float4> TexPipboySampler : register(t6);
SamplerState SampPipboySampler : register(s6);
#endif
#if defined(GRAYSCALE_TO_COLOR) || defined(GRAYSCALE_TO_ALPHA)
Texture2D<float4> TexGrayscaleSampler : register(t4);
SamplerState SampGrayscaleSampler : register(s4);
#endif
#if defined(SOFT)
Texture2D<float4> TexDepthSampler : register(t3);
#endif
#if defined(ENVMAP) || defined(PARTICLE_DISTORTION) || (defined(MEMBRANE) && !defined(NORMALS))
Texture2D<float4> TexNormalDistortionSampler : register(t1);
SamplerState SampNormalDistortionSampler : register(s1);
#endif
#if defined(MEMBRANE) && defined(ALPHA_TEST)
Texture2D<float4> TexNoiseSampler : register(t2);
SamplerState SampNoiseSampler : register(s2);
#endif
#if defined(ENVMAP)
TextureCube<float4> TexEnvmapSampler : register(t5);
SamplerState SampEnvmapSampler : register(s5);
#endif
#if defined(ENVMAP) || defined(PARTICLE_DISTORTION)
Texture2D<float4> TexEffectMaskSampler : register(t7);
SamplerState SampEffectMaskSampler : register(s7);
#endif
#if defined(WEATHER)
Texture2D<float4> TexWeatherDepth : register(t8);
#endif

#if defined(UI_MASK_RECTS)
cbuffer UIMaskData : register(b0)
{
    float4 UIMaskPadding[2];
    float4 UIMaskParams;
    float4 UIMaskRects[16];
    float4 UIMaskColor;
};
#elif defined(SOFT) || defined(PIPBOY_SCREEN)
cbuffer CameraData : register(b0)
{
    float4 CameraDataEffect;
#if defined(PIPBOY_SCREEN)
    float4 PipboyParams;
#endif
};
#endif

cbuffer PerMaterial : register(b1)
{
    float4 BaseColor;
    float4 BaseColorScale;
    float4 LightingInfluence;
#if defined(ENVMAP) || defined(PARTICLE_DISTORTION) || defined(WEATHER)
    float4 EffectScale;
#endif
#if defined(WEATHER)
    float4 WeatherScale;
#endif
};

cbuffer PerGeometry : register(b2)
{
    float4 PLightPositionX;
    float4 PLightPositionY;
    float4 PLightPositionZ;
    float4 PLightSpotDirX;
    float4 PLightSpotDirY;
    float4 PLightSpotDirZ;
    float4 PLightSpotExponent;
    float4 PLightSpotCosHalfPhi;
    float4 PLightingRadiusInverse;
    float4 PLightColorR;
    float4 PLightColorG;
    float4 PLightColorB;
    float4 DLightColor;
    float4 PropertyColor;
    float4 AlphaTestRef;
#if defined(MEMBRANE)
    float4 MembraneRimColor;
    float4 MembraneVars;
#endif
};

struct PSInput
{
    float4 Position : SV_POSITION0;
    float4 TexCoord0 : TEXCOORD0;
#if defined(ENVMAP) || defined(MEMBRANE) || defined(PIPBOY_SCREEN)
    float4 EyeVector : TEXCOORD4;
#endif
#if defined(WEATHER)
    float4 WeatherCoord : TEXCOORD3;
#endif
#if defined(ENVMAP)
    float3 Tangent : TEXCOORD7;
    float3 Bitangent : TEXCOORD8;
    float3 Normal : TEXCOORD9;
#endif
#if defined(VC)
    float4 Color : COLOR0;
#endif
    float4 FogParam : COLOR1;
#if defined(PIPBOY_SCREEN) || (defined(MEMBRANE) && (defined(SKINNED) || defined(NORMALS)))
    float3 TBN0 : TEXCOORD1;
#endif
#if defined(MEMBRANE) && !defined(NORMALS) && defined(SKINNED)
    float3 TBN1 : TEXCOORD2;
    float3 TBN2 : TEXCOORD3;
#endif
#if defined(LIGHTING)
    float3 MSPosition : TEXCOORD6;
#endif
#if defined(PARTICLES)
    float3 ParticlePosition : TEXCOORD5;
#elif defined(SOFT)
    float3 ParticlePosition : TEXCOORD5;
#endif
};

struct PSOutput
{
    float4 Color : SV_Target0;
};

#if defined(LIGHTING)
float3 GetLightingColor(float3 msPosition)
{
    float4 lightDistX = msPosition.xxxx - PLightPositionX;
    float4 lightDistY = msPosition.yyyy - PLightPositionY;
    float4 lightDistZ = msPosition.zzzz - PLightPositionZ;
    float4 lightDistance = sqrt(
        lightDistX * lightDistX +
        lightDistY * lightDistY +
        lightDistZ * lightDistZ);
    float4 lightFadeMul = saturate(lightDistance * PLightingRadiusInverse);
    lightFadeMul = 1.0.xxxx - lightFadeMul * lightFadeMul;
    lightFadeMul = pow(lightFadeMul, 2.2);

    lightDistance = max(lightDistance, 0.001);
    lightDistX /= lightDistance;
    lightDistY /= lightDistance;
    lightDistZ /= lightDistance;

    float4 spotFade = saturate(
        lightDistX * PLightSpotDirX +
        lightDistY * PLightSpotDirY +
        lightDistZ * PLightSpotDirZ);
    spotFade = saturate(
        1.0.xxxx -
        (1.0.xxxx - spotFade) /
        (-PLightSpotCosHalfPhi + 1.0.xxxx));
    float4 spotPower = (bool4)PLightSpotExponent ?
        min(pow(spotFade, PLightSpotExponent), 1.0.xxxx) :
        1.0.xxxx;
    lightFadeMul *= spotPower;

    float3 color = DLightColor.xyz;
    color.x += dot(PLightColorR * lightFadeMul, 1.0.xxxx);
    color.y += dot(PLightColorG * lightFadeMul, 1.0.xxxx);
    color.z += dot(PLightColorB * lightFadeMul, 1.0.xxxx);
    return color;
}
#endif

PSOutput main(PSInput input)
{
    PSOutput output;
#if defined(UI_MASK_RECTS)
    float baseTextureAlpha =
        TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy).w;
    float baseAlpha = baseTextureAlpha * BaseColor.w;
    float alpha = baseAlpha * PropertyColor.w;

    float alphaTest = PropertyColor.w;
    alphaTest *= baseAlpha;
    alphaTest -= AlphaTestRef.x;
    clip(alphaTest);
    [branch]
    if (AlphaTestRef.y < 1.0)
    {
        clip(AlphaTestRef.y - baseTextureAlpha);
    }

    int maskRectCount = (int)UIMaskParams.x;
    float maskedAlpha = alpha;
    float mask = 0.0;
    [loop]
    for (int maskRectIndex = 0;
         maskRectIndex < maskRectCount;
         ++maskRectIndex)
    {
        float4 maskRect = UIMaskRects[maskRectIndex];
        bool verticalFade = maskRect.x < 0.0;
        float left = verticalFade ? -maskRect.x : maskRect.x;
        float2 horizontalDistance =
            float2(left - input.TexCoord0.x, input.TexCoord0.x - maskRect.z);
        float farY = input.TexCoord0.y - maskRect.w;
        float2 verticalDistance =
            float2(maskRect.y - input.TexCoord0.y, farY);
        float rectDistance = max(
            max(horizontalDistance.x, horizontalDistance.y),
            max(verticalDistance.x, verticalDistance.y));
        float rectMask = 1.0 - UIMaskParams.y * rectDistance;
        mask = max(rectMask, mask);

        bool insideFade =
            verticalFade &&
            input.TexCoord0.y >= maskRect.y - 0.0025 &&
            input.TexCoord0.y <= maskRect.w + 0.0025;
        [branch]
        if (insideFade)
        {
            maskedAlpha *=
                (input.TexCoord0.y - maskRect.y) /
                (maskRect.w - maskRect.y);
        }
    }

    mask = saturate(mask);
    output.Color.w = maskedAlpha * mask * UIMaskParams.w;
    bool useLinearColor = UIMaskParams.z != 0.0;
    float3 gammaMaskColor = pow(UIMaskColor.xyz, 2.2);
    output.Color.xyz =
        useLinearColor ? UIMaskColor.xyz : gammaMaskColor;
#if defined(PREMULTIPLY_ALPHA)
    output.Color.xyz *= output.Color.w;
#endif
#else
#if defined(WEATHER)
    float2 weatherPixel = input.WeatherCoord.xy + 1.0;
    weatherPixel *= WeatherScale.x;
    weatherPixel *= 0.5;
    int2 weatherPixelInt = (int2)weatherPixel;
    float weatherDepth =
        TexWeatherDepth.Load(int3(weatherPixelInt, 0)).x;
    clip(weatherDepth - input.WeatherCoord.z);
#endif
#if defined(VC)
#if defined(MEMBRANE) && defined(GRAYSCALE_TO_COLOR) && defined(IGNORE_TEX_ALPHA) && !defined(GRAYSCALE_TO_ALPHA) && !defined(ALPHA_TEST)
    float2 membraneVertexColor =
        pow(input.Color.wx, float2(2.2, 1.0));
#elif defined(MEMBRANE)
    float4 vertexColor = pow(input.Color, 2.2);
#elif defined(SOFT) && defined(LIGHTING) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA) && !defined(TEXTURE)
    float4 vertexColor = pow(input.Color, 5.0 / 2.0);
#elif defined(SOFT) && defined(LIGHTING) && defined(GRAYSCALE_TO_COLOR) && !defined(GRAYSCALE_TO_ALPHA)
    float4 vertexColor =
        pow(input.Color, float4(1.0, 2.2, 2.2, 2.2));
#elif defined(SOFT) && defined(LIGHTING) && defined(GRAYSCALE_TO_ALPHA) && !defined(GRAYSCALE_TO_COLOR)
    float4 vertexColor =
        pow(input.Color, float4(2.2, 2.2, 2.2, 1.0));
#elif defined(PARTICLE_DISTORTION) && defined(SOFT) && defined(ADDBLEND) && defined(GRAYSCALE_TO_ALPHA) && !defined(GRAYSCALE_TO_COLOR)
    float4 vertexColor =
        pow(input.Color, float4(2.2, 2.2, 2.2, 1.0));
#elif defined(PARTICLE_DISTORTION) && defined(SOFT) && !defined(LIGHTING) && defined(GRAYSCALE_TO_COLOR) && !defined(GRAYSCALE_TO_ALPHA)
    float4 vertexColor = 1.0.xxxx;
    vertexColor.xw =
        pow(input.Color.wx, float2(2.2, 1.0)).yx;
#elif defined(MULTBLEND) && defined(GRAYSCALE_TO_COLOR) && !defined(GRAYSCALE_TO_ALPHA) && !defined(SOFT)
    float4 vertexColor =
        pow(input.Color, float4(1.0, 2.2, 2.2, 2.2));
#elif defined(GRAYSCALE_TO_ALPHA) && !defined(GRAYSCALE_TO_COLOR) && !defined(SOFT)
    float4 vertexColor =
        pow(input.Color, float4(2.2, 2.2, 2.2, 1.0));
#else
    float4 vertexColor = pow(input.Color, 2.2);
#endif
#if defined(SOFT) && defined(LIGHTING) && (defined(GRAYSCALE_TO_COLOR) || defined(GRAYSCALE_TO_ALPHA))
#if defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA) && !defined(TEXTURE)
#elif defined(GRAYSCALE_TO_COLOR) && !defined(GRAYSCALE_TO_ALPHA)
    float4 grayscaleVertexColor = pow(
        vertexColor,
        float4(1.0, 1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
#elif (defined(GRAYSCALE_TO_ALPHA) && !defined(GRAYSCALE_TO_COLOR)) || (defined(VC) && defined(LIGHTING) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA) && !defined(TEXTURE))
    float4 grayscaleVertexColor = pow(
        vertexColor,
        float4(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2, 1.0));
#else
    float4 grayscaleVertexColor = pow(vertexColor, 1.0 / 2.2);
#endif
#endif
#endif
#if defined(VC) && defined(ENVMAP) && defined(TEXTURE) && defined(SOFT) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA)
    float2 compositionVertexColor = pow(
        input.Color.xw,
        float2(2.2, 5.0 / 2.0));
#endif
#if defined(VC) && defined(PARTICLE_DISTORTION) && defined(LIGHTING) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA)
    float2 particleGrayscaleVertexColor = pow(input.Color.xw, 2.2);
#endif

#if defined(MEMBRANE)
#if defined(ALPHA_TEST) && defined(GRAYSCALE_TO_COLOR) && ((defined(GRAYSCALE_TO_ALPHA) && defined(NORMALS)) || defined(IGNORE_TEX_ALPHA))
    float membraneAlpha =
        TexNoiseSampler.Sample(
            SampNoiseSampler,
            input.TexCoord0.zw).w;
#if defined(VC)
    membraneAlpha *= vertexColor.w;
#endif
    if (membraneAlpha - AlphaTestRef.x < 0.0)
    {
        discard;
    }
#endif
#if defined(GRAYSCALE_TO_COLOR) && defined(IGNORE_TEX_ALPHA) && !defined(GRAYSCALE_TO_ALPHA)
#if defined(ALPHA_TEST)
    float membraneGrayscaleColorScale =
        pow(PropertyColor.x, 1.0 / 2.2);
#if defined(VC)
    membraneGrayscaleColorScale *= input.Color.x;
#endif
    membraneGrayscaleColorScale *= input.EyeVector.w;
#else
    float membraneGrayscaleColorScale =
        pow(PropertyColor.x, 1.0 / 2.2);
#if defined(VC)
#if defined(GRAYSCALE_TO_COLOR) && defined(IGNORE_TEX_ALPHA) && !defined(GRAYSCALE_TO_ALPHA) && !defined(ALPHA_TEST)
    membraneGrayscaleColorScale =
        membraneVertexColor.y * membraneGrayscaleColorScale;
#else
    membraneGrayscaleColorScale *= vertexColor.x;
#endif
#endif
    membraneGrayscaleColorScale *= input.EyeVector.w;
#endif
#endif
#if !(defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA))
    float4 baseTexColor =
        TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
    float4 baseColor = baseTexColor;
#if defined(IGNORE_TEX_ALPHA) || defined(GRAYSCALE_TO_ALPHA)
    baseColor.w = 1.0;
#endif
    float4 baseColorMul = PropertyColor;
#if defined(VC)
#if defined(GRAYSCALE_TO_COLOR) && defined(IGNORE_TEX_ALPHA) && !defined(GRAYSCALE_TO_ALPHA) && !defined(ALPHA_TEST)
    baseColorMul.xw *= membraneVertexColor.yx;
#else
    baseColorMul *= vertexColor;
#endif
#endif
#if !defined(GRAYSCALE_TO_ALPHA)
    baseColor = baseColorMul * baseColor;
#endif
#endif
#if defined(NORMALS)
    float3 membraneNormal = input.TBN0;
#else
    float3 membraneNormal =
        TexNormalDistortionSampler.Sample(
            SampNormalDistortionSampler,
            input.TexCoord0.zw).xzy * 2.0 - 1.0;
#if defined(SKINNED)
    membraneNormal = mul(
        membraneNormal,
        transpose(float3x3(input.TBN0, input.TBN1, input.TBN2)));
#endif
#endif
#if defined(ALPHA_TEST) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA) && !defined(NORMALS)
    float membraneAlpha =
        TexNoiseSampler.Sample(
            SampNoiseSampler,
            input.TexCoord0.zw).w;
#if defined(VC)
    membraneAlpha *= vertexColor.w;
#endif
    clip(membraneAlpha - AlphaTestRef.x);
#endif
#if defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA)
    float2 grayscaleBaseScale = pow(PropertyColor.xw, 1.0 / 2.2);
#if defined(VC)
    grayscaleBaseScale *= input.Color.xw;
#endif
    float2 membraneGrayscaleScale =
        grayscaleBaseScale * input.EyeVector.ww;
    float4 baseTexColor =
        TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
    float4 baseColor = baseTexColor;
#endif
#if defined(ALPHA_TEST) && !(defined(GRAYSCALE_TO_COLOR) && (defined(GRAYSCALE_TO_ALPHA) || defined(IGNORE_TEX_ALPHA)))
    float membraneAlpha =
        TexNoiseSampler.Sample(
            SampNoiseSampler,
            input.TexCoord0.zw).w;
#if defined(VC)
    membraneAlpha *= vertexColor.w;
#endif
    clip(membraneAlpha - AlphaTestRef.x);
#endif
#endif

#if !defined(MEMBRANE)
    float4 baseTexColor = 1.0.xxxx;
    float4 baseColor = 1.0.xxxx;
#if defined(PARTICLE_DISTORTION)
    float2 distortion =
        TexNormalDistortionSampler.Sample(
            SampNormalDistortionSampler,
            input.TexCoord0.zw).xy * 2.0 - 1.0;
    float distortionScale = input.ParticlePosition.y * EffectScale.x;
    float4 distortedTexCoord =
        distortion.xyxy * distortionScale + input.TexCoord0.zwxy;
    baseTexColor =
        TexBaseSampler.Sample(SampBaseSampler, distortedTexCoord.xy);
#if defined(GRAYSCALE_TO_COLOR)
    baseTexColor.y = pow(baseTexColor.y, 1.0 / 2.2);
#endif
    float4 distortionMaskColor =
        TexEffectMaskSampler.Sample(
            SampEffectMaskSampler,
            distortedTexCoord.zw);
    baseTexColor *= distortionMaskColor;
    baseColor *= baseTexColor;
#elif defined(TEXTURE) && !(defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA))
    baseTexColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
    baseColor *= baseTexColor;
#endif
#endif

#if defined(RGB_FALLOFF)
    baseColor.xyz *= input.TexCoord0.zzz;
#endif
#if defined(FALLOFF)
    baseColor.w *= input.TexCoord0.z;
#endif
#if !defined(MEMBRANE)
    float4 baseColorMul = BaseColor;
#if defined(VC)
    baseColorMul *= vertexColor;
#endif
    baseColor = baseColorMul * baseColor;
#endif

#if defined(SOFT)
    float depth =
        TexDepthSampler.Load(int3(input.Position.xy, 0)).x;
#if defined(ENVMAP) && defined(VC) && defined(TEXTURE) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA)
    depth =
        (1.0 - depth) * CameraDataEffect.z + CameraDataEffect.y;
    float depthNear =
        CameraDataEffect.y * CameraDataEffect.z + CameraDataEffect.y;
    float2 depthScale = float2(
        LightingInfluence.y / depth,
        LightingInfluence.y / depthNear);
    float depthFade = saturate(depthScale.x - input.ParticlePosition.z);
    float nearFade = saturate(input.ParticlePosition.z - depthScale.y);
#elif (defined(GRAYSCALE_TO_COLOR) && !defined(GRAYSCALE_TO_ALPHA) && (!defined(LIGHTING) || !defined(VC) || defined(PARTICLE_DISTORTION) || (defined(ENVMAP) && defined(ADDBLEND)))) || (!defined(LIGHTING) && ((!defined(TEXTURE) && (defined(GRAYSCALE_TO_COLOR) || defined(GRAYSCALE_TO_ALPHA))) || (defined(VC) && defined(GRAYSCALE_TO_ALPHA) && !defined(GRAYSCALE_TO_COLOR))))
    float depthFar =
        (1.0 - depth) * CameraDataEffect.z + CameraDataEffect.y;
    float depthScaleFar = LightingInfluence.y / depthFar;
    float depthNear =
        CameraDataEffect.y * CameraDataEffect.z + CameraDataEffect.y;
    float depthScaleNear = LightingInfluence.y / depthNear;
    float depthFade = saturate(depthScaleFar - input.ParticlePosition.z);
    float nearFade = saturate(input.ParticlePosition.z - depthScaleNear);
#elif (defined(GRAYSCALE_TO_ALPHA) && !defined(GRAYSCALE_TO_COLOR)) || (defined(VC) && defined(LIGHTING) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA) && !defined(TEXTURE))
    float depthFar =
        (1.0 - depth) * CameraDataEffect.z + CameraDataEffect.y;
    float depthNear =
        CameraDataEffect.y * CameraDataEffect.z + CameraDataEffect.y;
    float2 depthScale = float2(
        LightingInfluence.y / depthFar,
        LightingInfluence.y / depthNear);
    float depthFade = saturate(depthScale.x - input.ParticlePosition.z);
    float nearFade = saturate(input.ParticlePosition.z - depthScale.y);
#elif defined(PARTICLE_DISTORTION) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA)
    float depthFar =
        (1.0 - depth) * CameraDataEffect.z + CameraDataEffect.y;
    float depthScaleFar = LightingInfluence.y / depthFar;
    float depthNear =
        CameraDataEffect.y * CameraDataEffect.z + CameraDataEffect.y;
    float depthScaleNear = LightingInfluence.y / depthNear;
    float depthFade = saturate(depthScaleFar - input.ParticlePosition.z);
    float nearFade = saturate(input.ParticlePosition.z - depthScaleNear);
#else
    float depthFar =
        (1.0 - depth) * CameraDataEffect.z + CameraDataEffect.y;
    float depthNear =
        CameraDataEffect.y * CameraDataEffect.z + CameraDataEffect.y;
    float2 depthScale = LightingInfluence.yy / float2(depthFar, depthNear);
    float depthFade = saturate(depthScale.x - input.ParticlePosition.z);
    float nearFade = saturate(input.ParticlePosition.z - depthScale.y);
#endif
    nearFade = smoothstep(0.075, 0.5, nearFade);
#if defined(GRAYSCALE_TO_COLOR) || defined(GRAYSCALE_TO_ALPHA)
    float softFade = depthFade * nearFade;
    baseColor.w *= softFade;
#else
    baseColor.w *= depthFade * nearFade;
#endif
#endif

#if defined(GRAYSCALE_TO_COLOR) || defined(GRAYSCALE_TO_ALPHA)
#if defined(MEMBRANE)
#if defined(GRAYSCALE_TO_ALPHA) && !defined(GRAYSCALE_TO_COLOR)
    float grayscaleBaseAlpha = pow(PropertyColor.w, 1.0 / 2.2);
#if defined(VC)
    grayscaleBaseAlpha *= input.Color.w;
#endif
#elif defined(ALPHA_TEST) && defined(GRAYSCALE_TO_COLOR) && !defined(GRAYSCALE_TO_ALPHA) && defined(VC) && !defined(IGNORE_TEX_ALPHA)
    float grayscaleBaseColorX =
        pow(PropertyColor.x, 1.0 / 2.2) * input.Color.x;
#elif !defined(GRAYSCALE_TO_ALPHA) && !(defined(GRAYSCALE_TO_COLOR) && defined(IGNORE_TEX_ALPHA))
    float4 grayscaleBaseColor = pow(PropertyColor, 1.0 / 2.2);
#if defined(VC)
    grayscaleBaseColor =
        pow(vertexColor, 1.0 / 2.2) * grayscaleBaseColor;
#endif
#endif
#elif defined(VC) && defined(ENVMAP) && defined(TEXTURE) && defined(SOFT) && defined(LIGHTING) && defined(ADDBLEND) && defined(GRAYSCALE_TO_COLOR) && !defined(GRAYSCALE_TO_ALPHA)
    float4 grayscaleBaseColor = input.Color;
    grayscaleBaseColor *= pow(BaseColor, 1.0 / 2.2);
#elif defined(VC) && defined(PARTICLE_DISTORTION) && !defined(SOFT) && defined(GRAYSCALE_TO_ALPHA) && !defined(GRAYSCALE_TO_COLOR) && (defined(LIGHTING) || defined(MULTBLEND))
    float4 grayscaleBaseColor = vertexColor;
    grayscaleBaseColor *= pow(BaseColor, 1.0 / 2.2);
#elif defined(VC) && defined(PARTICLE_DISTORTION) && defined(SOFT) && (defined(ADDBLEND) || defined(LIGHTING)) && defined(GRAYSCALE_TO_ALPHA) && !defined(GRAYSCALE_TO_COLOR)
    float4 grayscaleBaseColor = vertexColor;
    grayscaleBaseColor *= pow(BaseColor, 1.0 / 2.2);
#elif defined(VC) && defined(PARTICLE_DISTORTION) && defined(SOFT) && !defined(LIGHTING) && defined(GRAYSCALE_TO_COLOR) && !defined(GRAYSCALE_TO_ALPHA)
    float4 grayscaleBaseColor = vertexColor;
    grayscaleBaseColor *= pow(BaseColor, 1.0 / 2.2);
#elif defined(VC) && defined(PARTICLE_DISTORTION) && defined(LIGHTING) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA)
    float4 grayscaleBaseColor = 1.0.xxxx;
    grayscaleBaseColor.xw = pow(
        particleGrayscaleVertexColor,
        1.0 / 2.2);
    grayscaleBaseColor *= pow(BaseColor, 1.0 / 2.2);
#elif defined(VC) && defined(PARTICLE_DISTORTION)
    float4 grayscaleBaseColor = input.Color;
    grayscaleBaseColor *= pow(BaseColor, 1.0 / 2.2);
#elif defined(VC) && defined(ENVMAP) && defined(TEXTURE) && defined(SOFT) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA)
    float4 grayscaleBaseColor = 1.0.xxxx;
    grayscaleBaseColor.xw = pow(
        compositionVertexColor,
        float2(1.0 / 2.2, 2.0 / 5.0));
    grayscaleBaseColor *= pow(BaseColor, 1.0 / 2.2);
#elif defined(VC) && defined(ENVMAP) && defined(TEXTURE) && !defined(SOFT) && defined(LIGHTING) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA)
    float4 compositionVertexColor = pow(
        input.Color,
        float4(2.2, 1.0, 1.0 / 2.2, 2.2));
    float4 grayscaleBaseColor =
        pow(compositionVertexColor, 1.0 / 2.2);
    grayscaleBaseColor *= pow(BaseColor, 1.0 / 2.2);
#elif defined(VC) && defined(SOFT) && defined(LIGHTING) && defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA) && !defined(TEXTURE)
    float4 grayscaleBaseColor = pow(vertexColor, 2.0 / 5.0);
    grayscaleBaseColor *= pow(BaseColor, 1.0 / 2.2);
#elif defined(VC) && ((defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA) && (!defined(SOFT) || defined(LIGHTING))) || (defined(GRAYSCALE_TO_COLOR) && !defined(GRAYSCALE_TO_ALPHA) && defined(SOFT) && defined(LIGHTING)))
#if defined(SOFT) && defined(LIGHTING)
    float4 grayscaleBaseColor = grayscaleVertexColor;
#else
    float4 grayscaleBaseColor = pow(vertexColor, 1.0 / 2.2);
#endif
    grayscaleBaseColor *= pow(BaseColor, 1.0 / 2.2);
#else
    float4 grayscaleBaseColor = pow(BaseColor, 1.0 / 2.2);
#if defined(VC)
#if defined(SOFT) && !defined(LIGHTING)
#if defined(GRAYSCALE_TO_COLOR)
    grayscaleBaseColor.x *= input.Color.x;
#endif
#if defined(GRAYSCALE_TO_ALPHA)
    grayscaleBaseColor.w *= input.Color.w;
#endif
#elif defined(GRAYSCALE_TO_ALPHA) && !defined(GRAYSCALE_TO_COLOR) && !defined(SOFT)
    grayscaleBaseColor *= vertexColor;
#elif defined(SOFT) && defined(LIGHTING)
    grayscaleBaseColor *= grayscaleVertexColor;
#elif defined(MULTBLEND) && defined(GRAYSCALE_TO_COLOR) && !defined(GRAYSCALE_TO_ALPHA) && !defined(SOFT)
    grayscaleBaseColor =
        pow(
            vertexColor,
            float4(1.0, 1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2)) *
        grayscaleBaseColor;
#else
    grayscaleBaseColor =
        pow(vertexColor, 1.0 / 2.2) * grayscaleBaseColor;
#endif
#endif
#endif
#endif
#if defined(GRAYSCALE_TO_COLOR)
#if defined(MEMBRANE)
#if defined(GRAYSCALE_TO_ALPHA)
    float grayscaleColorScale = membraneGrayscaleScale.x;
#elif defined(IGNORE_TEX_ALPHA)
    float grayscaleColorScale = membraneGrayscaleColorScale;
#elif defined(ALPHA_TEST) && defined(VC)
    float grayscaleColorScale =
        grayscaleBaseColorX * input.EyeVector.w;
#else
    float grayscaleColorScale =
        grayscaleBaseColor.x * input.EyeVector.w;
#endif
#elif defined(PARTICLE_DISTORTION)
    float grayscaleColorScale = grayscaleBaseColor.x;
#else
    float grayscaleColorScale =
        grayscaleBaseColor.x * input.TexCoord0.z;
#endif
#if defined(SOFT)
    grayscaleColorScale *= softFade;
#endif
#endif
#if defined(GRAYSCALE_TO_ALPHA)
#if defined(MEMBRANE)
#if !defined(GRAYSCALE_TO_COLOR)
    float grayscaleAlphaScale =
        grayscaleBaseAlpha * input.EyeVector.w;
#else
    float grayscaleAlphaScale = membraneGrayscaleScale.y;
#endif
#elif defined(PARTICLE_DISTORTION)
    float grayscaleAlphaScale = grayscaleBaseColor.w;
#else
    float grayscaleAlphaScale =
        grayscaleBaseColor.w * input.TexCoord0.z;
#endif
#endif
#if !defined(MEMBRANE) && !defined(PARTICLE_DISTORTION) && ((defined(GRAYSCALE_TO_COLOR) && defined(GRAYSCALE_TO_ALPHA)) || (!defined(TEXTURE) && defined(GRAYSCALE_TO_COLOR)))
    baseTexColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
#endif
#if defined(GRAYSCALE_TO_COLOR)
#if defined(PARTICLE_DISTORTION)
    float grayscaleColor = baseTexColor.y;
#else
    float grayscaleColor = pow(baseTexColor.y, 1.0 / 2.2);
#endif
    baseColor.xyz =
        TexGrayscaleSampler.Sample(
            SampGrayscaleSampler,
            float2(grayscaleColor, grayscaleColorScale)).xyz;
#if !defined(MEMBRANE)
    baseColor.xyz *=
        BaseColorScale.x;
#endif
#endif
#if defined(GRAYSCALE_TO_ALPHA)
#if !defined(MEMBRANE)
    grayscaleAlphaScale *= pow(PropertyColor.w, 1.0 / 2.2);
#endif
#if defined(SOFT) && !defined(PARTICLE_DISTORTION)
    grayscaleAlphaScale *= softFade;
#endif
#if !defined(TEXTURE) && !defined(GRAYSCALE_TO_COLOR)
    baseTexColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
#endif
    baseColor.w =
        TexGrayscaleSampler.Sample(
            SampGrayscaleSampler,
            float2(baseTexColor.w, grayscaleAlphaScale)).w;
#if defined(SOFT) && defined(PARTICLE_DISTORTION)
    baseColor.w *= softFade;
#endif
#endif

#if defined(MEMBRANE)
    float membraneDot = dot(membraneNormal, input.EyeVector.xyz);
    float membraneColorMul =
        pow(saturate(1.0 - membraneDot), MembraneVars.x);
    float4 membraneColor = membraneColorMul * MembraneRimColor;
    baseColor.w += membraneColor.w;
#if defined(GRAYSCALE_TO_COLOR)
    baseColor.xyz =
        baseColor.xyz * MembraneVars.z +
        membraneColor.xyz * membraneColor.www;
#elif defined(GRAYSCALE_TO_ALPHA)
    baseColor.xyz =
        baseColorMul.xyz * baseColor.xyz +
        membraneColor.xyz * membraneColor.www;
#else
    baseColor.xyz =
        membraneColor.xyz * membraneColor.www + baseColor.xyz;
#endif
#endif

#if defined(ENVMAP)
    float3 normalColor =
        TexNormalDistortionSampler.Sample(
            SampNormalDistortionSampler,
            input.TexCoord0.xy).xyw;
    float2 normalXY = normalColor.xy * 2.0 - 1.0;
    float normalZ = sqrt(1.0 - min(dot(normalXY, normalXY), 1.0));
    float3 worldNormal = normalize(
        input.Tangent * normalXY.x +
        input.Bitangent * normalXY.y +
        input.Normal * normalZ);
    float reflectionDot = dot(worldNormal, input.EyeVector.xyz);
    reflectionDot *= 2.0;
    float3 reflection =
        reflectionDot * worldNormal - input.EyeVector.xyz;
    reflection = -reflection;
    float3 envmapColor =
        TexEnvmapSampler.Sample(SampEnvmapSampler, reflection).xyz;
    envmapColor *= EffectScale.x;
    envmapColor *= normalColor.z;
    envmapColor *=
        TexEffectMaskSampler.Sample(
            SampEffectMaskSampler,
            input.TexCoord0.xy).x;
#if defined(GRAYSCALE_TO_COLOR) || defined(GRAYSCALE_TO_ALPHA)
    baseColor.xyz = baseColor.xyz + envmapColor;
#else
    baseColor.xyz += envmapColor;
#endif
#endif

#if defined(MEMBRANE)
    float3 lightColor = baseColor.xyz;
    float alpha = baseColor.w;
#else
    float3 propertyColor = PropertyColor.xyz;
#if defined(LIGHTING)
    propertyColor = GetLightingColor(input.MSPosition);
#endif
    float3 propertyBaseColor = propertyColor;
    propertyBaseColor *= baseColor.xyz;
    float3 lightColor = lerp(
        baseColor.xyz,
        propertyBaseColor,
        LightingInfluence.xxx);
    float alpha = baseColor.w;
#if !defined(GRAYSCALE_TO_ALPHA)
    alpha *= PropertyColor.w;
#endif
#endif

#if defined(ADDBLEND)
#if defined(PIPBOY_SCREEN)
    float addBlendFade = 1.0 - input.FogParam.w;
    float3 blendedColor = lightColor * addBlendFade;
#else
    float3 blendedColor = lightColor * (1.0 - input.FogParam.www);
#endif
#elif defined(MULTBLEND)
    float3 blendedColor = lerp(
        lightColor,
        1.0.xxx,
        saturate(1.5 * input.FogParam.w).xxx);
    blendedColor = lerp(1.0.xxx, blendedColor, alpha.xxx);
#else
    float3 blendedColor = lerp(
        lightColor,
        input.FogParam.xyz,
        input.FogParam.www);
#endif

    float4 finalColor = float4(blendedColor, alpha);
#if !defined(PREMULTIPLY_ALPHA) && !defined(PIPBOY_SCREEN)
    output.Color = finalColor;
#endif

#if !defined(MEMBRANE)
    float alphaTest = baseColor.w;
#if !defined(GRAYSCALE_TO_ALPHA)
#if defined(PIPBOY_SCREEN) && defined(ENVMAP)
    alphaTest *= PropertyColor.w;
#else
    alphaTest = PropertyColor.w;
    alphaTest *= baseColor.w;
#endif
#endif
    alphaTest -= AlphaTestRef.x;
    clip(alphaTest);
#endif
#if defined(PIPBOY_SCREEN)
    float4 pipboyColor;
    if (PipboyParams.y == 0.0)
    {
        pipboyColor = pow(
            TexPipboySampler.Sample(
                SampPipboySampler,
                input.TexCoord0.xy),
            2.2);
    }
    else
    {
        pipboyColor =
            TexPipboySampler.Sample(
                SampPipboySampler,
                input.TexCoord0.xy);
    }
#if defined(ADDBLEND)
    pipboyColor.xyz *= PipboyParams.w;
    pipboyColor.xyz =
        lightColor * addBlendFade + pipboyColor.xyz;
#else
    pipboyColor.xyz =
        pipboyColor.xyz * PipboyParams.w + finalColor.xyz;
#endif
    bool scalePipboyColor = PipboyParams.x != 0.0;
    float4 scaledPipboyColor = pipboyColor * BaseColor.w;
    finalColor =
        scalePipboyColor ? scaledPipboyColor : pipboyColor;
    finalColor.w *= PipboyParams.z;
#endif
    [branch]
    if (AlphaTestRef.y < 1.0)
    {
#if defined(TEXTURE) && !defined(PARTICLE_DISTORTION)
        clip(AlphaTestRef.y - baseTexColor.w);
#else
        clip(AlphaTestRef.y -
            TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy).w);
#endif
    }
#if defined(PREMULTIPLY_ALPHA)
#if defined(PIPBOY_SCREEN)
    finalColor.xyz *= finalColor.w;
#else
    finalColor.xyz *= alpha;
#endif
    output.Color = finalColor;
#elif defined(PIPBOY_SCREEN)
    output.Color = finalColor;
#endif
#endif
    return output;
}
#endif

#ifdef BSEFFECT_VS_SOURCE
#if defined(ENVCUBE_RAIN) || defined(ENVCUBE_SNOW)
#define WEATHER
#endif
#if defined(SKINNED)
#define COMPOSED_VERTEX_INPUT
#endif
cbuffer PerFrame : register(b12)
{
    float4 Scene[47];
};
#ifdef ENVCUBE_SNOW
cbuffer PerWeather : register(b0)
{
    float4 SnowParameters;
};
#endif
#if (defined(FALLOFF) || defined(RGB_FALLOFF)) && !defined(MEMBRANE)
cbuffer PerTechnique : register(b1)
{
    float4 PerMaterial[3];
};
#elif defined(SOFT) && !defined(MEMBRANE)
cbuffer PerTechnique : register(b1)
{
    float4 PerMaterial[2];
};
#elif defined(TEXTURE) && !defined(MEMBRANE)
cbuffer PerTechnique : register(b1)
{
    float4 PerMaterial[1];
};
#endif
cbuffer PerGeometry : register(b2)
{
    row_major float4x4 WorldViewProj;
#ifdef MERGE_INSTANCED
    row_major float3x4 ModelView;
    float4 GeometryPadding0[6];
    float4 EyePosition;
    float4 GeometryPadding1[10];
    uint4 AttributeOffsets;
    uint4 InstanceParams;
#elif defined(MEMBRANE)
    float4 MembranePadding0[9];
    float4 EyePosition;
    float4 MembranePadding1;
    float4 MembraneData;
#elif defined(WEATHER)
    row_major float3x4 ModelView;
    float4 GeometryPadding0[6];
    float4 EyePosition;
    float4 GeometryPadding1[2];
    float4 WeatherOffset;
    float4 WeatherCenter;
    float4 WeatherSize;
    float4 RainOffset;
    row_major float4x4 WeatherTransform;
#elif defined(FALLOFF) || defined(RGB_FALLOFF)
    row_major float3x4 ModelView;
#elif defined(ENVMAP)
    row_major float3x4 ModelView;
    float4 GeometryPadding[6];
    float4 EyePosition;
#endif
#if defined(ENVMAP) && defined(PIPBOY_SCREEN) && (defined(FALLOFF) || defined(RGB_FALLOFF))
    float4 GeometryPadding[6];
    float4 EyePosition;
#endif
};
#ifdef SKINNED
cbuffer PerSkin : register(b10)
{
    float4 BoneData[180];
};
#endif
#ifdef INDEXED_TEXTURE
cbuffer IndexedTextureData : register(b11)
{
    float4 IndexedTextureTransforms[128];
};
#endif
#ifdef MERGE_INSTANCED
ByteAddressBuffer VertexData : register(t8);
ByteAddressBuffer VertexIndices : register(t9);
ByteAddressBuffer InstanceIndices : register(t10);
struct InstanceTransform
{
    float4 Basis[3];
    float4 TranslateScale;
    float4 Tint;
};
StructuredBuffer<InstanceTransform> InstanceTransforms : register(t11);
struct VSMergedInput
{
    uint VertexId : SV_VertexID;
};
uint PackedIndexOffset(uint element)
{
    return mad(-2, element & 1, element * 2);
}
uint SelectPackedIndex(uint packed, uint element)
{
    uint parity = element & 1;
    return (packed & 0xFFFF) * (1 - parity) +
        (packed >> 16) * parity;
}
float2 UnpackHalf2(uint packed)
{
    return float2(f16tof32(packed), f16tof32(packed >> 16));
}
float4 UnpackUnorm4(uint packed)
{
    return float4(
        (packed & 0xFF) / 255.0,
        ((packed >> 8) & 0xFF) / 255.0,
        ((packed >> 16) & 0xFF) / 255.0,
        (packed >> 24) / 255.0);
}
#endif
struct VSInput
{
    float4 Position : POSITION0;
#ifdef TEXTURE
    float2 TexCoord : TEXCOORD0;
#endif
#ifdef ENVCUBE_SNOW
    float4 WeatherData : TEXCOORD1;
#elif defined(ENVCUBE_RAIN)
    float4 WeatherData : TEXCOORD1;
#endif
#if defined(NORMALS) && !defined(WEATHER)
    float4 Normal : NORMAL0;
#endif
#if defined(ENVMAP) && !defined(WEATHER)
    float4 Binormal : BINORMAL0;
#endif
#ifdef PARTICLES
    float ParticleData : TEXCOORD2;
#endif
#ifdef VC
    float4 Color : COLOR0;
#endif
#ifdef SKINNED
    float4 BlendWeight : BLENDWEIGHT0;
    float4 BlendIndices : BLENDINDICES0;
#endif
};
struct VSOutput
{
    float4 Position : SV_POSITION0;
    float4 TexCoord : TEXCOORD0;
#if defined(ENVMAP) || defined(PIPBOY_SCREEN) || defined(MEMBRANE)
    float4 EyeVector : TEXCOORD4;
#endif
#ifdef WEATHER
    float4 WeatherCoord : TEXCOORD3;
#endif
#ifdef ENVMAP
    float3 Tangent : TEXCOORD7;
    float3 Bitangent : TEXCOORD8;
    float3 Normal : TEXCOORD9;
#endif
#ifdef VC
    float4 Color : COLOR0;
#endif
    float4 FogParam : COLOR1;
#if defined(PIPBOY_SCREEN) || (defined(MEMBRANE) && defined(NORMALS))
    float3 EffectNormal : TEXCOORD1;
#endif
#if defined(SKINNED) && defined(MEMBRANE) && !defined(NORMALS)
    float3 MembraneTangentX : TEXCOORD1;
    float3 MembraneTangentY : TEXCOORD2;
    float3 MembraneTangentZ : TEXCOORD3;
#endif
#ifdef LIGHTING
    float3 MSPosition : TEXCOORD6;
#endif
#if defined(PARTICLES) || defined(SOFT)
    float3 ParticlePosition : TEXCOORD5;
#endif
};
#ifdef SKINNED
float3x4 BoneMatrix(int row, float3 origin)
{
    return float3x4(
        BoneData[row] - float4(0.0, 0.0, 0.0, origin.x),
        BoneData[row + 1] - float4(0.0, 0.0, 0.0, origin.y),
        BoneData[row + 2] - float4(0.0, 0.0, 0.0, origin.z));
}
float3 TransformPoint(float3x4 transform, float4 value)
{
    return float3(
        dot(value, transform[0]),
        dot(value, transform[1]),
        dot(value, transform[2]));
}
float3 TransformAxis(float3x4 transform, float3 axis)
{
    return normalize(float3(
        dot(axis, transform[0].xyz),
        dot(axis, transform[1].xyz),
        dot(axis, transform[2].xyz)));
}
#endif
#ifdef MERGE_INSTANCED
VSOutput main(VSMergedInput merged)
#else
VSOutput main(VSInput input)
#endif
{
#ifdef MERGE_INSTANCED
    VSInput input;
    uint vertexElement = merged.VertexId + InstanceParams.z;
    uint vertexIndex = SelectPackedIndex(
        VertexIndices.Load(PackedIndexOffset(vertexElement)), vertexElement);
    uint instanceElement = vertexElement / (InstanceParams.y * 3);
    uint instanceIndex = SelectPackedIndex(
        InstanceIndices.Load(PackedIndexOffset(instanceElement)),
        instanceElement);
    InstanceTransform instance = InstanceTransforms[instanceIndex];
    float3x3 instanceBasis = float3x3(
        instance.Basis[0].xyz, instance.Basis[1].xyz, instance.Basis[2].xyz);
    uint vertexBase = vertexIndex * InstanceParams.x;
    uint2 packedPosition = VertexData.Load2(vertexBase);
    float4 vertexPosition = float4(
        UnpackHalf2(packedPosition.x), UnpackHalf2(packedPosition.y));
    input.Position = float4(
        instance.TranslateScale.w *
            mul(vertexPosition.xyz, instanceBasis) +
        instance.TranslateScale.xyz,
        vertexPosition.w);
#ifdef TEXTURE
    input.TexCoord = UnpackHalf2(
        VertexData.Load(vertexBase + AttributeOffsets.z));
#endif
#ifdef NORMALS
    uint packedNormal =
        VertexData.Load(vertexBase + AttributeOffsets.x);
    uint3 normalBytes = uint3(
        packedNormal & 0xFF,
        (packedNormal >> 8) & 0xFF,
        (packedNormal >> 16) & 0xFF);
    float3 mergedNormal;
    mergedNormal.x =
        normalBytes.x * (2.0 / 255.0) - 1.0;
    mergedNormal.yz =
        (float2)normalBytes.yz * (2.0 / 255.0) - 1.0;
#ifdef ENVMAP
    float mergedNormalW =
        (packedNormal >> 24) * (2.0 / 255.0) - 1.0;
#endif
#endif
#ifdef ENVMAP
    uint packedBinormal =
        VertexData.Load(vertexBase + AttributeOffsets.y);
    float mergedBinormalW =
        (packedBinormal >> 24) * (2.0 / 255.0) - 1.0;
    uint3 binormalBytes = uint3(
        packedBinormal & 0xFF,
        (packedBinormal >> 8) & 0xFF,
        (packedBinormal >> 16) & 0xFF);
    float3 mergedBinormal;
    mergedBinormal.x =
        binormalBytes.x * (2.0 / 255.0) - 1.0;
    mergedBinormal.yz =
        (float2)binormalBytes.yz * (2.0 / 255.0) - 1.0;
#endif
#ifdef VC
    float4 packedColor = UnpackUnorm4(
        VertexData.Load(vertexBase + AttributeOffsets.w));
    float instanceTint = InstanceTransforms[instanceIndex].Tint.x;
    input.Color = float4(packedColor.xyz * instanceTint, packedColor.w);
#endif
#endif
    VSOutput output;
#ifdef WEATHER
#ifdef ENVCUBE_RAIN
    float rainFactor = (input.WeatherData.y + 1.0) * 0.5;
    float4 rainStartPosition = -float4(RainOffset.xyz, 0.0);
#endif
    float3 weatherCell =
        (input.Position.xyz + WeatherOffset.xyz) / WeatherSize.x;
    float3 weatherFraction = fmod(weatherCell, 1.0);
    float3 modelPosition =
        weatherFraction * WeatherSize.x +
        (WeatherCenter.xyz - WeatherSize.xxx * 0.5);
    float4 weatherPosition;
    weatherPosition.xyz = modelPosition;
    weatherPosition.w = 1.0;
#ifdef ENVCUBE_RAIN
    float4 rainEnd = mul(WorldViewProj, weatherPosition);
    rainStartPosition += weatherPosition;
    float4 rainStart = mul(WorldViewProj, rainStartPosition);
    float4 projected = lerp(rainStart, rainEnd, rainFactor);
    projected.xy += input.WeatherData.xy;
#else
    float weatherSine;
    float weatherCosine;
    sincos(WeatherOffset.w, weatherSine, weatherCosine);
    float2 weatherRotation0 = float2(weatherCosine, -weatherSine);
    float2 weatherRotation1 = float2(weatherSine, weatherCosine);
    float2 weatherA = float2(
        dot(weatherRotation0, input.WeatherData.xy),
        dot(weatherRotation1, input.WeatherData.xy));
    float2 weatherB = float2(
        dot(weatherRotation0, input.WeatherData.zw),
        dot(weatherRotation1, input.WeatherData.zw));
    float2 snowOffset =
        weatherA * SnowParameters.xy + weatherB;
    float4 projected = mul(WorldViewProj, weatherPosition);
    projected.xy += snowOffset;
#endif
#else
#ifdef SKINNED
    const float4 weights = float4(
        input.BlendWeight.xyz,
        1.0 - saturate(input.BlendWeight.x + input.BlendWeight.y +
                       input.BlendWeight.z));
    const int4 rows = (int4)(765.010010 * input.BlendIndices);
    const float3 origin = Scene[35].xyz;
    const float3x4 boneTransform =
        weights.x * BoneMatrix(rows.x, origin) +
        weights.y * BoneMatrix(rows.y, origin) +
        weights.z * BoneMatrix(rows.z, origin) +
        weights.w * BoneMatrix(rows.w, origin);
    const float3 skinnedPosition =
        TransformPoint(boneTransform, float4(input.Position.xyz, 1.0));
    const float4 position = float4(skinnedPosition, 1.0);
    float4 projected = float4(
        dot(Scene[8], position),
        dot(Scene[9], position),
        dot(Scene[10], position),
        dot(Scene[11], position));
#elif defined(COMPOSED_VERTEX_INPUT)
    float4 projected = mul(WorldViewProj, input.Position);
#else
    float4 projected = mul(WorldViewProj, float4(input.Position.xyz, 1.0));
#endif
#endif
#ifdef SKY_OBJECT
    projected.z = projected.w;
#endif
    output.Position = projected;
#ifdef MEMBRANE
#ifdef PROJECTED_UV
    float projectedU = atan2(input.Position.x, input.Position.y);
    projectedU /= 3.1416;
    output.TexCoord.x =
        abs(projectedU) * MembraneData.w + MembraneData.y;
    output.TexCoord.y =
        input.Position.z * MembraneData.z + MembraneData.x;
#else
    output.TexCoord.xy =
        input.TexCoord * MembraneData.zw + MembraneData.xy;
#endif
#if defined(ALPHA_TEST) || (defined(SKINNED) && !defined(NORMALS))
    output.TexCoord.z = input.TexCoord.x;
#else
    output.TexCoord.z = 1.0;
#endif
    output.TexCoord.w = input.TexCoord.y;
#elif defined(TEXTURE)
#ifdef INDEXED_TEXTURE
    uint indexedTexture = (uint)input.Position.w;
    float2 baseTexCoord =
        IndexedTextureTransforms[indexedTexture].yw * input.TexCoord +
        IndexedTextureTransforms[indexedTexture].xz;
#else
    float2 baseTexCoord = input.TexCoord;
#endif
    output.TexCoord.xy =
        baseTexCoord * PerMaterial[0].zw + PerMaterial[0].xy;
#ifdef PARTICLE_DISTORTION
    output.TexCoord.z = baseTexCoord.x;
#else
    output.TexCoord.z = 1.0;
#endif
    output.TexCoord.w = baseTexCoord.y;
#else
    output.TexCoord = float4(0.0, 0.0, 1.0, 0.0);
#endif
#ifdef PIPBOY_SCREEN
#ifndef ENVMAP
    output.EyeVector = float4(normalize(-input.Position.xyz), 0.0);
#endif
#endif
#ifdef MEMBRANE
#if defined(SKINNED) && defined(NORMALS)
    output.EyeVector = float4(normalize(-skinnedPosition), 1.0);
#else
    output.EyeVector =
        float4(normalize(EyePosition.xyz - input.Position.xyz), 1.0);
#endif
#endif
#ifdef VC
    output.Color = input.Color;
#endif
#if (defined(FALLOFF) || defined(RGB_FALLOFF)) && !defined(WEATHER)
#ifdef SKINNED
    float3 modelViewPosition = skinnedPosition;
#else
    float3 modelViewPosition =
        mul(ModelView, float4(input.Position.xyz, 1.0));
#endif
    float3 viewDirection = normalize(-modelViewPosition);
#if defined(ENVMAP) && !defined(PIPBOY_SCREEN)
    output.EyeVector.xyz = viewDirection;
#endif
#ifdef MERGE_INSTANCED
    float3 localNormal = mergedNormal;
#else
    float3 localNormal = input.Normal.xyz * 2.0 - 1.0;
#endif
#ifdef MERGE_INSTANCED
    localNormal = mul(localNormal, instanceBasis);
#endif
#ifdef SKINNED
    float3 modelViewNormal = TransformAxis(boneTransform, localNormal);
#else
    float3 modelViewNormal;
    modelViewNormal.x = dot(ModelView[0].xyz, localNormal);
    modelViewNormal.y = dot(ModelView[1].xyz, localNormal);
    modelViewNormal.z = dot(ModelView[2].xyz, localNormal);
    modelViewNormal = normalize(modelViewNormal);
#endif
    float falloff = saturate(
        (abs(dot(modelViewNormal, viewDirection)) - PerMaterial[2].x) /
        (PerMaterial[2].y - PerMaterial[2].x));
    falloff = falloff * falloff * (3.0 - 2.0 * falloff);
    output.TexCoord.z =
        lerp(PerMaterial[2].z, PerMaterial[2].w, falloff);
#endif
#if (defined(FALLOFF) || defined(RGB_FALLOFF)) && defined(WEATHER)
    float3 modelViewNormal = normalize(float3(
        ModelView[0].z,
        ModelView[1].z,
        ModelView[2].z));
    float3 modelViewPosition = mul(ModelView, weatherPosition);
    float3 viewDirection = normalize(-modelViewPosition);
    float falloff = saturate(
        (abs(dot(modelViewNormal, viewDirection)) - PerMaterial[2].x) /
        (PerMaterial[2].y - PerMaterial[2].x));
    falloff = falloff * falloff * (3.0 - 2.0 * falloff);
    output.TexCoord.z =
        lerp(PerMaterial[2].z, PerMaterial[2].w, falloff);
#endif
#ifdef WEATHER
    output.WeatherCoord.x = dot(WeatherTransform[0], weatherPosition);
    output.WeatherCoord.y = -dot(WeatherTransform[1], weatherPosition);
    output.WeatherCoord.z = dot(WeatherTransform[2], weatherPosition);
    output.WeatherCoord.w = dot(WeatherTransform[3], weatherPosition);
#endif
#ifdef ENVMAP
#ifdef WEATHER
    float3 eyeVector = EyePosition.xyz - input.Position.xyz;
    output.EyeVector.x = dot(ModelView[0].xyz, eyeVector);
    output.EyeVector.y = dot(ModelView[1].xyz, eyeVector);
    output.EyeVector.z = dot(ModelView[2].xyz, eyeVector);
    output.EyeVector.w = 0.0;
    output.Tangent = float3(
        ModelView[0].x,
        ModelView[1].x,
        ModelView[2].x);
    output.Bitangent = float3(
        ModelView[0].y,
        ModelView[1].y,
        ModelView[2].y);
    output.Normal = float3(
        ModelView[0].z,
        ModelView[1].z,
        ModelView[2].z);
#else
#ifdef PIPBOY_SCREEN
    output.EyeVector =
        float4(normalize(EyePosition.xyz - input.Position.xyz), 0.0);
    float3 outputNormal = modelViewNormal;
#elif !defined(FALLOFF) && !defined(RGB_FALLOFF)
#ifdef MERGE_INSTANCED
    float3 eyeVector = EyePosition.xyz - vertexPosition.xyz;
#elif defined(SKINNED)
    float3 eyeVector = -skinnedPosition;
#else
    float3 eyeVector = EyePosition.xyz - input.Position.xyz;
#endif
#ifdef SKINNED
    output.EyeVector = float4(normalize(eyeVector), 0.0);
#else
    output.EyeVector.x = dot(ModelView[0].xyz, eyeVector);
    output.EyeVector.y = dot(ModelView[1].xyz, eyeVector);
    output.EyeVector.z = dot(ModelView[2].xyz, eyeVector);
#endif
#ifdef MERGE_INSTANCED
    float3 outputNormal = mul(mergedNormal, instanceBasis);
#else
    float3 outputNormal = input.Normal.xyz * 2.0 - 1.0;
#endif
#ifdef SKINNED
    outputNormal = TransformAxis(boneTransform, outputNormal);
#endif
#else
    float3 outputNormal = modelViewNormal;
#endif
    output.EyeVector.w = 0.0;
#ifdef MERGE_INSTANCED
    float3 tangent = float3(
        input.Position.w,
        mergedNormalW,
        mergedBinormalW);
#else
    float3 tangent = float3(
        input.Position.w,
        input.Normal.w * 2.0 - 1.0,
        input.Binormal.w * 2.0 - 1.0);
#endif
#ifdef MERGE_INSTANCED
    tangent = mul(tangent, instanceBasis);
#elif defined(SKINNED)
    tangent = TransformAxis(boneTransform, tangent);
#endif
    output.Tangent.x = dot(tangent, ModelView[0].xyz);
    output.Tangent.y = dot(tangent, ModelView[1].xyz);
    output.Tangent.z = dot(tangent, ModelView[2].xyz);
#ifdef MERGE_INSTANCED
    float3 localBitangent = mergedBinormal;
#else
    float3 localBitangent = input.Binormal.xyz * 2.0 - 1.0;
#endif
#ifdef MERGE_INSTANCED
    localBitangent = mul(localBitangent, instanceBasis);
#elif defined(SKINNED)
    localBitangent = TransformAxis(boneTransform, localBitangent);
#endif
    output.Bitangent.x = dot(localBitangent, ModelView[0].xyz);
    output.Bitangent.y = dot(localBitangent, ModelView[1].xyz);
    output.Bitangent.z = dot(localBitangent, ModelView[2].xyz);
    output.Normal.x = dot(outputNormal, ModelView[0].xyz);
    output.Normal.y = dot(outputNormal, ModelView[1].xyz);
    output.Normal.z = dot(outputNormal, ModelView[2].xyz);
#ifdef PIPBOY_SCREEN
    output.EffectNormal = modelViewNormal;
#endif
#endif
#endif
    float3 viewPosition;
    viewPosition.x = dot(Scene[20], projected);
    viewPosition.y = dot(Scene[21], projected);
    viewPosition.z = dot(Scene[22], projected);
#ifdef SOFT
    output.ParticlePosition.z = projected.w / PerMaterial[1].x;
#endif
    float fogPlane = dot(Scene[14], float4(viewPosition, 1.0));
    fogPlane += Scene[35].z;
    float distanceRamp =
        length(viewPosition) * Scene[41].x - Scene[41].z;
    float distanceFactor = saturate(distanceRamp);
    float2 fogRamp =
        saturate(fogPlane.xx * Scene[46].xy - Scene[46].zw);
    float fogAmount = lerp(fogRamp.x, fogRamp.y, distanceFactor);
    float fogDensity =
        (1.0 - fogAmount) + fogAmount * Scene[44].w;
    float fogLimit = Scene[43].w;
    if (distanceRamp > 0.75)
    {
        fogLimit = min(
            lerp(
                Scene[43].w,
                1.0,
                (distanceFactor - 0.75) * 4.0),
            1.0);
    }
    float nearScale =
        distanceRamp < 0.015 ? distanceFactor * 66.666672 : 1.0;
    float fogCurve = min(pow(distanceFactor, Scene[42].w), fogLimit);
    float3 nearFog = lerp(Scene[42].xyz, Scene[44].xyz, fogCurve);
    float3 farFog = lerp(Scene[43].xyz, Scene[45].xyz, fogCurve);
    output.FogParam.xyz = lerp(nearFog, farFog, fogAmount);
    output.FogParam.w = (fogCurve * fogDensity) * nearScale;
#ifdef PIPBOY_SCREEN
#ifndef ENVMAP
    output.EffectNormal = float3(0.0, 0.0, 1.0);
#endif
#endif
#if defined(MEMBRANE) && defined(NORMALS)
#ifdef SKINNED
    output.EffectNormal = TransformAxis(
        boneTransform, input.Normal.xyz * 2.0 - 1.0);
#else
    output.EffectNormal = input.Normal.xyz * 2.0 - 1.0;
#endif
#endif
#if defined(SKINNED) && defined(MEMBRANE) && !defined(NORMALS)
    output.MembraneTangentX = float3(1.0, 0.0, 0.0);
    output.MembraneTangentY = float3(0.0, 1.0, 0.0);
    output.MembraneTangentZ = float3(0.0, 0.0, 1.0);
#endif
#ifdef LIGHTING
#ifdef WEATHER
    output.MSPosition = modelPosition;
#elif defined(SKINNED)
    output.MSPosition = skinnedPosition;
#else
    output.MSPosition = input.Position.xyz;
#endif
#endif
#if defined(PARTICLES) && defined(SOFT)
    output.ParticlePosition.x = 0.0;
    output.ParticlePosition.y = input.ParticleData;
#elif defined(PARTICLES)
    output.ParticlePosition.xz = 0.0;
    output.ParticlePosition.y = input.ParticleData;
#elif defined(SOFT)
    output.ParticlePosition.xy = 0.0;
#endif
    return output;
}
#endif
