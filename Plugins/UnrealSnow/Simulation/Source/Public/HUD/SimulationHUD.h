#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SimulationHUD.generated.h"


UCLASS()
class ASimulationHUD : public AHUD
{
	GENERATED_BODY()

public:

	/** Put Roboto Here */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UFont* UE4Font;

	void DrawHUD() override;
};