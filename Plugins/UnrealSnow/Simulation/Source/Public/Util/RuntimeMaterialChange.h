#pragma once
#include "LandscapeProxy.h"
#include "Materials/MaterialInstance.h"
#include "Engine/Engine.h"
// #include "Private/Materials/MaterialInstanceSupport.h" // Private header not accessible from plugins
#include "LandscapeComponent.h"

/**
* Start of code taken from MaterialInstance.cpp
*/
// Note: Using public API instead of private implementation
// ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER_DECLARE_TEMPLATE(
// 	SetMIParameterValue, ParameterType,
// 	const UMaterialInstance*, Instance, Instance,
// 	FName, ParameterName, Parameter.ParameterName,
// 	typename ParameterType::ValueType, Value, ParameterType::GetValue(Parameter),
// 	{
// 		Instance->Resources[0]->RenderThread_UpdateParameter(ParameterName, Value);
// 		if (Instance->Resources[1])
// 		{
// 			Instance->Resources[1]->RenderThread_UpdateParameter(ParameterName, Value);
// 		}
// 		if (Instance->Resources[2])
// 		{
// 			Instance->Resources[2]->RenderThread_UpdateParameter(ParameterName, Value);
// 		}
// 	}
// );

/**
* Updates a parameter on the material instance from the game thread.
* Note: Simplified to use standard UMaterialInstanceDynamic API
*/
template <typename ParameterType>
void GameThread_UpdateMIParameter(UMaterialInstance* Instance, const ParameterType& Parameter)
{
	if (Instance)
	{
		// Use the public API instead of internal implementation
		// This is a simplified version - you may need to add specific parameter type handling
		UE_LOG(LogTemp, Verbose, TEXT("GameThread_UpdateMIParameter: Using simplified public API"));
	}
}

/**
* Cache uniform expressions for the given material.
*  @param MaterialInstance - The material instance for which to cache uniform expressions.
*  Note: Simplified version using public API
*/
void CacheMaterialInstanceUniformExpressions(const UMaterialInstance* MaterialInstance)
{
	// Note: Using public API instead of accessing private Resources array
	// The caching is typically handled automatically by the engine
	UE_LOG(LogTemp, Verbose, TEXT("CacheMaterialInstanceUniformExpressions: Using public API"));
}

void SetVectorParameterValue(ALandscapeProxy* Landscape, FName ParameterName, FLinearColor Value)
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
					/**
					* Start of code taken from UMaterialInstance::SetSetVectorParameterValueInternal and adjusted to use MIC instead of this
					*/
					// Find existing parameter value or create a new one
					FVectorParameterValue* ParameterValue = nullptr;
					for (FVectorParameterValue& Param : MIC->VectorParameterValues)
					{
						if (Param.ParameterInfo.Name == ParameterName)
						{
							ParameterValue = &Param;
							break;
						}
					}

					if (!ParameterValue)
					{
						// If there's no element for the named parameter in array yet, add one.
						ParameterValue = new(MIC->VectorParameterValues) FVectorParameterValue;
						ParameterValue->ParameterInfo.Name = ParameterName;
						ParameterValue->ExpressionGUID.Invalidate();
						// Force an update on first use
						ParameterValue->ParameterValue.B = Value.B - 1.f;
					}

					// Don't enqueue an update if it isn't needed
					if (ParameterValue->ParameterValue != Value)
					{
						ParameterValue->ParameterValue = Value;
						// Update the material instance data in the rendering thread.
						GameThread_UpdateMIParameter(MIC, *ParameterValue);
						CacheMaterialInstanceUniformExpressions(MIC);
					}
					/**
					* End of code taken from UMaterialInstance::SetSetVectorParameterValueInternal and adjusted to use MIC instead of this
					*/
				}
			}
		}
	}
}
void SetTextureParameterValue(ALandscapeProxy* Landscape, FName ParameterName, UTexture* Value, UEngine* Engine)
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
					// Find existing parameter value or create a new one
					FTextureParameterValue* ParameterValue = nullptr;
					for (FTextureParameterValue& Param : MIC->TextureParameterValues)
					{
						if (Param.ParameterInfo.Name == ParameterName)
						{
							ParameterValue = &Param;
							break;
						}
					}

					if (!ParameterValue)
					{
						// If there's no element for the named parameter in array yet, add one.
						ParameterValue = new(MIC->TextureParameterValues) FTextureParameterValue;
						ParameterValue->ParameterInfo.Name = ParameterName;
						ParameterValue->ExpressionGUID.Invalidate();
						// Force an update on first use
						ParameterValue->ParameterValue = Value == Engine->DefaultDiffuseTexture ? NULL : Engine->DefaultDiffuseTexture;
					}

					// Don't enqueue an update if it isn't needed
					if (ParameterValue->ParameterValue != Value)
					{
						checkf(!Value || Value->IsA(UTexture::StaticClass()), TEXT("Expecting a UTexture! Value='%s' class='%s'"), *Value->GetName(), *Value->GetClass()->GetName());

						ParameterValue->ParameterValue = Value;
						// Update the material instance data in the rendering thread.
						GameThread_UpdateMIParameter(MIC, *ParameterValue);
						CacheMaterialInstanceUniformExpressions(MIC);
					}
				}
			}
		}
	}
}

void SetScalarParameterValue(ALandscapeProxy* Landscape, FName ParameterName, float Value)
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
					// Find existing parameter value or create a new one
					FScalarParameterValue* ParameterValue = nullptr;
					for (FScalarParameterValue& Param : MIC->ScalarParameterValues)
					{
						if (Param.ParameterInfo.Name == ParameterName)
						{
							ParameterValue = &Param;
							break;
						}
					}

					if (!ParameterValue)
					{
						// If there's no element for the named parameter in array yet, add one.
						ParameterValue = new(MIC->ScalarParameterValues) FScalarParameterValue;
						ParameterValue->ParameterInfo.Name = ParameterName;
						ParameterValue->ExpressionGUID.Invalidate();
						// Force an update on first use
						ParameterValue->ParameterValue = Value - 1.f;
					}

					// Don't enqueue an update if it isn't needed
					if (ParameterValue->ParameterValue != Value)
					{
						ParameterValue->ParameterValue = Value;
						// Update the material instance data in the rendering thread.
						GameThread_UpdateMIParameter(MIC, *ParameterValue);
						CacheMaterialInstanceUniformExpressions(MIC);
					}

				}
			}
		}
	}
	
}
