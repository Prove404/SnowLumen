#include "WorldClimWeatherDataProvider.h"
#include "SimulationData.h"

TResourceArray<FClimateData>* UWorldClimWeatherDataProvider::CreateRawClimateDataResourceArray(FDateTime StartTime, FDateTime EndTime)
{
	return nullptr;
}

void UWorldClimWeatherDataProvider::Initialize(FDateTime StartTime, FDateTime EndTime)
{
}
