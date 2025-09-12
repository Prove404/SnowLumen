#pragma once

#include "SimulationWeatherDataProviderBase.h"
#include "CsvWeatherProvider.generated.h"

/**
* CSV weather provider that loads weather data from a CSV file.
* Supports time interpolation and per-cell data.
*/
UCLASS(Blueprintable, BlueprintType)
class SIMULATIONDATA_API UCsvWeatherProvider : public USimulationWeatherDataProviderBase
{
	GENERATED_BODY()

public:
	UCsvWeatherProvider();

	/** Path to the CSV file */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FFilePath CsvFilePath;

	/** Time format string for parsing timestamps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FString TimeFormat = TEXT("yyyy-MM-dd HH:mm");

	/** Whether the CSV contains uniform data for the entire grid */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	bool bUniformGrid = true;

	/** Expected CSV header (case-insensitive):
	* time, T2m_C, RH_pct, Wind_mps, SWdown_Wm2, LWdown_Wm2, Precip_mmph, SnowFrac_0_1
	* Optional columns for per-cell data: i, j
	*/

	virtual void Initialize(FDateTime StartTime, FDateTime EndTime) override;

	virtual float GetMeasurementAltitude() override { return 1000.0f; }

	virtual TResourceArray<FClimateData>* CreateRawClimateDataResourceArray(FDateTime StartTime, FDateTime EndTime) override;

	virtual FWeatherForcingData GetWeatherForcing(FDateTime Time, int32 GridX = 0, int32 GridY = 0) override;

private:
	/** Parsed weather records */
	TArray<FWeatherForcingData> WeatherRecords;

	/** Load and parse the CSV file */
	bool LoadCsvData();

	/** Parse a single CSV line into weather data */
	bool ParseCsvLine(const FString& Line, FWeatherForcingData& OutData);

	/** Find weather data records bracketing the given time */
	void FindBracketingRecords(FDateTime Time, int32& OutIndex1, int32& OutIndex2, float& OutAlpha);

	/** Interpolate between two weather records */
	FWeatherForcingData InterpolateRecords(const FWeatherForcingData& Record1, const FWeatherForcingData& Record2, float Alpha);
};
