#pragma once

#include "CoreMinimal.h"
#include "EditorFramework/AssetImportData.h"
#include "HDRData.generated.h"

// Define API macro if not already defined
#ifndef WORLCLIMDATA_API
#define WORLCLIMDATA_API
#endif

UCLASS()
class WORLCLIMDATA_API UHDRData : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 NROWS;

	UPROPERTY()
	int32 NCOLS;

	UPROPERTY()
	int32 NBANDS;

	UPROPERTY()
	int32 NBITS;

	UPROPERTY()
	int32 BANDROWBYTES;

	UPROPERTY()
	int32 TOTALROWBYTES;

	UPROPERTY()
	int32 BANDGAPBYTES;

	UPROPERTY()
	int32 NODATA;

	UPROPERTY()
	float ULXMAP;

	UPROPERTY()
	float ULYMAP;

	UPROPERTY()
	float XDIM;

	UPROPERTY()
	float YDIM;

	UPROPERTY()
	int32 MinX;

	UPROPERTY()
	int32 MaxX;

	UPROPERTY()
	int32 MinY;

	UPROPERTY()
	int32 MaxY;

	UPROPERTY()
	int32 MinValue;

	UPROPERTY()
	int32 MaxValue;

	UPROPERTY()
	FString Month;

	UPROPERTY(VisibleAnywhere, Category = "Import")
	TObjectPtr<UAssetImportData> AssetImportData;
};
