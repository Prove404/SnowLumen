#include "SimulationPixelShader.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

void FSimulationPixelShaderModule::StartupModule()
{
    // Shader directory mapping is handled through the Build.cs file
    // The AdditionalPropertiesForReceipt.Add() call in Build.cs registers the shader directories
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SimulationPixelShader"));
    if (Plugin.IsValid())
    {
        const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
        UE_LOG(LogTemp, Log, TEXT("SimulationPixelShader plugin shader directory: %s"), *ShaderDir);
    }
}

void FSimulationPixelShaderModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FSimulationPixelShaderModule, SimulationPixelShader)
