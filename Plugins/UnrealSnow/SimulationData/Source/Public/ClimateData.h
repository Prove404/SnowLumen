#pragma once

#include "CoreMinimal.h"
#include "ClimateData.generated.h"

/** Weather data (precipitation and temperature). */
USTRUCT(BlueprintType)
struct FClimateData
{
	GENERATED_USTRUCT_BODY()

	float Temperature;
	float Precipitation;

	FClimateData(float InPrecipitation, float InTemperature)
		: Temperature(InTemperature), Precipitation(InPrecipitation)
	{
	}

	FClimateData() : Temperature(0.0f), Precipitation(0.0f)
	{
	}
};

/** Comprehensive weather forcing data for snow simulation. */
USTRUCT(BlueprintType)
struct FWeatherForcingData
{
	GENERATED_USTRUCT_BODY()

public:
	/** Temperature in Kelvin */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float Temperature_K = 273.15f;  // 0°C in Kelvin

	/** Shortwave downward radiation in W/m² */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float SWdown_Wm2 = 0.0f;

	/** Longwave downward radiation in W/m² */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float LWdown_Wm2 = 200.0f;

	/** Wind speed in m/s */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float Wind_mps = 2.0f;

	/** Relative humidity (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float RH_01 = 0.6f;

	/** Precipitation rate in kg/m²/s */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float PrecipRate_kgm2s = 0.0f;

	/** Snow fraction (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float SnowFrac_01 = 0.0f;

	/** Timestamp for this forcing data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	FDateTime Timestamp;

	FWeatherForcingData() {}

	FWeatherForcingData(FDateTime InTimestamp, float TempK, float SWdown, float LWdown, float Wind, float RH, float Precip, float SnowFrac)
		: Temperature_K(TempK), SWdown_Wm2(SWdown), LWdown_Wm2(LWdown), Wind_mps(Wind), RH_01(RH),
		  PrecipRate_kgm2s(Precip), SnowFrac_01(SnowFrac), Timestamp(InTimestamp)
	{
	}
};
