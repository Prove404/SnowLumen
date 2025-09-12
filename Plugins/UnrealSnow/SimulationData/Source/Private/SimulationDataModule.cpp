// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Public/SimulationData.h"

class FSimulationDataModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{

	}

	virtual void ShutdownModule() override
	{

	}
};

IMPLEMENT_MODULE(FSimulationDataModule, SimulationData)