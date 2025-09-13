#pragma once
#include "LandscapeProxy.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Engine.h"
// #include "Private/Materials/MaterialInstanceSupport.h" // Private header not accessible from plugins
#include "LandscapeComponent.h"

// Inline functions for setting material parameters on landscape
inline void SetVectorParameterValue(ALandscapeProxy* Landscape, FName ParameterName, FLinearColor Value)
{
	if (Landscape)
	{
		for (int32 Index = 0; Index < Landscape->LandscapeComponents.Num(); ++Index)
		{
			if (Landscape->LandscapeComponents[Index])
			{
				UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Landscape->LandscapeComponents[Index]->GetMaterial(0));
				if (MIC)
				{
					// Use the public API to set vector parameter
					MIC->SetVectorParameterValueEditorOnly(ParameterName, Value);
				}
			}
		}
	}
}

inline void SetTextureParameterValue(ALandscapeProxy* Landscape, FName ParameterName, UTexture* Value, UEngine* Engine)
{
	if (Landscape)
	{
		for (int32 Index = 0; Index < Landscape->LandscapeComponents.Num(); ++Index)
		{
			if (Landscape->LandscapeComponents[Index])
			{
				UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Landscape->LandscapeComponents[Index]->GetMaterial(0));
				if (MIC)
				{
					// Use the public API to set texture parameter
					MIC->SetTextureParameterValueEditorOnly(ParameterName, Value);
				}
			}
		}
	}
}

inline void SetScalarParameterValue(ALandscapeProxy* Landscape, FName ParameterName, float Value)
{
	if (Landscape)
	{
		for (int32 Index = 0; Index < Landscape->LandscapeComponents.Num(); ++Index)
		{
			if (Landscape->LandscapeComponents[Index])
			{
				UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Landscape->LandscapeComponents[Index]->GetMaterial(0));
				if (MIC)
				{
					// Use the public API to set scalar parameter
					MIC->SetScalarParameterValueEditorOnly(ParameterName, Value);
				}
			}
		}
	}
}

/**
 * Validates that a material has all required parameters for snow simulation.
 * @param Material - The material interface to validate
 * @return true if all required parameters are present, false otherwise
 */
bool CheckMaterialParamsValid(UMaterialInterface* Material);
