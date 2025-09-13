#pragma once

#include "WorldClimDataAssets.h"
#include "SimulationWeatherDataProviderBase.h"
#include "Engine/EngineTypes.h"
#include "WorldClimWeatherDataProvider.generated.h"


/**
* Weather data provider which provides data from www.worldclim.org downscaled to hourly data as described in "Utility of daily vs. monthly large-scale climate data: an
* intercomparison of two statistical downscaling methods".
*/
UCLASS(Blueprintable, BlueprintType)
class SIMULATIONDATA_API UWorldClimWeatherDataProvider : public USimulationWeatherDataProviderBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TArray<UMonthlyWorldClimDataAsset*> MonthlyData;

	// Optional CSV override (same format as CsvWeatherProvider)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	FFilePath CsvFilePath;

	// Grid sampling or single point
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bUseSinglePoint = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta=(EditCondition="bUseSinglePoint"))
	float SampleLatitude = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta=(EditCondition="bUseSinglePoint"))
	float SampleLongitude = 7.5f;

	// Simple snow fraction heuristic: T<=0 => snow
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bUseSimpleSnowFrac = true;

	virtual TResourceArray<FClimateData>* CreateRawClimateDataResourceArray(FDateTime StartTime, FDateTime EndTime) override final;

	virtual void Initialize(FDateTime StartTime, FDateTime EndTime) override final;

	virtual FWeatherForcingData GetWeatherForcing(FDateTime Time, int32 GridX = 0, int32 GridY = 0) override;

private:
	// Precomputed hourly series
	TArray<FWeatherForcingData> HourlySeries; // if CsvFilePath set, populated from CSV
	FDateTime SeriesStart;
	int32 SeriesHours = 0;
	bool bUseCsv = false;

	bool LoadCsvOverride();
	FWeatherForcingData SampleMonthlyToHourly(FDateTime Time) const;
};
