#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FAlbedoPixelShader : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAlbedoPixelShader);
    SHADER_USE_PARAMETER_STRUCT(FAlbedoPixelShader, FGlobalShader);
public:
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FVector2f, SnowInvSizePerMeter)     // float2
        SHADER_PARAMETER(FVector2f, SnowOriginMeters)        // float2
        SHADER_PARAMETER(float,     SnowDisplacementScale)   // float
        SHADER_PARAMETER(float,     DepthClamp_m)            // float
        SHADER_PARAMETER(float,     AlbedoWSA)               // float
        SHADER_PARAMETER(float,     AlbedoBSA)               // float
        SHADER_PARAMETER(float,     RoughnessBase)           // float
        SHADER_PARAMETER(uint32,    bDebug)                  // replaces bool
        SHADER_PARAMETER(int32,     ClimateDataDimension)    // int
        SHADER_PARAMETER(int32,     CellsDimensionX)         // int
        SHADER_PARAMETER(int32,     CellsDimensionY)         // int
        SHADER_PARAMETER_TEXTURE(Texture2D, SnowDepthTex)
        SHADER_PARAMETER_SAMPLER(SamplerState, SnowDepthTexSampler)
        SHADER_PARAMETER_SRV(StructuredBuffer<float>, AlbedoInputBuffer)
        SHADER_PARAMETER_SRV(StructuredBuffer<uint>, MaxSnowInputBuffer)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Params) { return true; }
};

class FAlbedoVertexShader : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAlbedoVertexShader);
    SHADER_USE_PARAMETER_STRUCT(FAlbedoVertexShader, FGlobalShader);
public:
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FVector2f, SnowInvSizePerMeter)
        SHADER_PARAMETER(FVector2f, SnowOriginMeters)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Params) { return true; }
};