#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FWorldClimDataModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};

// API macro definition
#ifdef WORLCLIMDATA_API
    #error "WORLCLIMDATA_API macro already defined"
#endif

#ifdef WORLCLIMDATA_API
    #define WORLCLIMDATA_API DLLEXPORT
#else
    #define WORLCLIMDATA_API DLLIMPORT
#endif
