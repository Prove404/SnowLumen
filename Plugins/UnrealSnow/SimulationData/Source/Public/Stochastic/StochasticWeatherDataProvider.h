#pragma once

#include "SimulationWeatherDataProviderBase.h"
#include <vector>
#include "ClimateData.h"
#include "StochasticWeatherDataProvider.generated.h"

/** State of the simulation. */
enum class WeatherState : int8
{
	WET, DRY 
};

/**
* Simple stochastic weather provider which generates hourly precipitation using a two state Markov chain which does not 
* change transition probabilities during the day or during seasons. Temperature follows a simple sinusoidal pattern 
* and precipitation amount follows an exponential distribution. To account for spatial variation noise is applied
* to the precipitation. The temperature and the precipitation are not correlated.
*/
UCLASS(Blueprintable, BlueprintType)
class SIMULATIONDATA_API UStochasticWeatherDataProvider : public USimulationWeatherDataProviderBase
{
	GENERATED_BODY()

private:
	/** State of the simulation. */
	WeatherState State;
	
	std::vector<std::vector<FClimateData>> ClimateData;
public:
	// @TODO fix probabilities
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", DisplayName = "P_I_W")
	/** Initial probability of a wet day. */
	float P_I_W = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", DisplayName = "P_WD")
	/** Probability of a wet hour given the previous hour was dry. */
	float P_WD = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", DisplayName = "P_WW")
	/** Probability of a wet hour given the previous hour was wet. */
	float P_WW = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	/** Number of measuring stations per dimension. */
	int32 Resolution = 10;

	UStochasticWeatherDataProvider();

	virtual TResourceArray<FClimateData>* CreateRawClimateDataResourceArray(FDateTime StartTime, FDateTime EndTime) override final;

	virtual void Initialize(FDateTime StartTime, FDateTime EndTime) override final;

	/** Return weather forcing for a given time and optional grid coord */
	virtual FWeatherForcingData GetWeatherForcing(FDateTime Time, int32 GridX = 0, int32 GridY = 0) override;

private:
	FDateTime StartTimeRef;
	int32 TotalHours = 0;
};