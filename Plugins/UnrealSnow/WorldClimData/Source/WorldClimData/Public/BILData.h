#pragma once

#include "CoreMinimal.h"
#include "EditorFramework/AssetImportData.h"
#include "BILData.generated.h"

// Define API macro if not already defined
#ifndef WORLCLIMDATA_API
#define WORLCLIMDATA_API
#endif

UCLASS()
class WORLCLIMDATA_API UBILData : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<int16> Data;

	UPROPERTY(VisibleAnywhere, Category = "Import")
	TObjectPtr<UAssetImportData> AssetImportData;
};
