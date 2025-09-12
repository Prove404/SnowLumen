#include "AlbedoPixelShaderDeclaration.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

IMPLEMENT_GLOBAL_SHADER(FAlbedoPixelShader,
   "/Project/UnrealSnow/AlbedoPixelShader.usf", "MainPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FAlbedoVertexShader,
   "/Project/UnrealSnow/AlbedoPixelShader.usf", "MainVS", SF_Vertex);