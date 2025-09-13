
#include "DegreeDayGPUSimulation.h"
#include "Simulation.h"
#include "LandscapeDataAccess.h"
#include "SnowSimulationActor.h"
#include "Util/MathUtil.h"
#include "LandscapeComponent.h"

FString UDegreeDayGPUSimulation::GetSimulationName() const
{
	return FString(TEXT("Degree Day GPU"));
}

void UDegreeDayGPUSimulation::Simulate(ASnowSimulationActor* SimulationActor, int32 CurrentSimulationStep, int32 Timesteps, bool SaveSnowMap, bool CaptureDebugInformation, TArray<FDebugCell>& DebugCells)
{
	SimulationComputeShader->ExecuteComputeShader(CurrentSimulationStep, Timesteps, SimulationActor->CurrentSimulationTime.GetHour(), CaptureDebugInformation, DebugCells);
	SimulationPixelShader->ExecutePixelShader(RenderTarget, SaveSnowMap);

	// Log snow depth statistics
	if (SimulationComputeShader)
	{
		float MaxSnow = SimulationComputeShader->GetMaxSnow();
		UE_LOG(LogTemp, Display, TEXT("[Snow] DepthTex max=%.3fm"), MaxSnow / 1000.0f); // Convert mm to meters
	}
}

void UDegreeDayGPUSimulation::Initialize(ASnowSimulationActor* SimulationActor, const TArray<FLandscapeCell>& LandscapeCells, float InitialMaxSnow, UWorld* World)
{
	// Create shader
	SimulationComputeShader = new FSimulationComputeShader(World->Scene->GetFeatureLevel());
	SimulationPixelShader = new FSnowPixelShader(World->Scene->GetFeatureLevel());

	// Create Cells
	TResourceArray<FGPUSimulationCell> Cells;
	for (const FLandscapeCell& LandscapeCell : LandscapeCells)
	{
		FGPUSimulationCell Cell(LandscapeCell.Aspect, LandscapeCell.Inclination, LandscapeCell.Altitude, 
			LandscapeCell.Latitude, LandscapeCell.Area, LandscapeCell.AreaXY, LandscapeCell.InitialWaterEquivalent);
		Cells.Add(Cell);
	}
	

	// Initialize render target with R16F format for snow depth
 	RenderTarget = NewObject<UTextureRenderTarget2D>(this);
	RenderTarget->InitCustomFormat(SimulationActor->CellsDimensionX, SimulationActor->CellsDimensionY, EPixelFormat::PF_R16F, false);
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->AddressX = TA_Clamp;
	RenderTarget->AddressY = TA_Clamp;
	RenderTarget->Filter = TF_Bilinear;

	// Ensure proper texture settings for VHM material
	UTexture* SnowDepthTex = Cast<UTexture>(RenderTarget);
	if (SnowDepthTex)
	{
		SnowDepthTex->SRGB = false;
		SnowDepthTex->CompressionSettings = TC_HDR;
		SnowDepthTex->LODGroup = TEXTUREGROUP_Pixels2D;
	}

	RenderTarget->UpdateResource();
	
	// One-time log after render target creation
	UE_LOG(LogTemp, Display, TEXT("[Snow] Created DegreeDayGPUSimulation RenderTarget: PF=%s, size=%dx%d"), 
		*UEnum::GetValueAsString(RenderTarget->GetFormat()),
		RenderTarget->SizeX, RenderTarget->SizeY);
	
	// Initialize shaders
	auto ClimateData = SimulationActor->ClimateDataComponent->CreateRawClimateDataResourceArray(SimulationActor->StartTime, SimulationActor->EndTime);
	auto SimulationTimeSpan = SimulationActor->EndTime - SimulationActor->StartTime;
	int32 TotalHours = static_cast<int32>(SimulationTimeSpan.GetTotalHours());

	SimulationComputeShader->Initialize(Cells, *ClimateData, k_e, k_m, TMeltA, TMeltB, TSnowA, TSnowB, TotalHours, 
		SimulationActor->CellsDimensionX, SimulationActor->CellsDimensionY, SimulationActor->ClimateDataComponent->GetMeasurementAltitude(), InitialMaxSnow);

	SimulationPixelShader->Initialize(SimulationComputeShader->GetSnowBuffer(), SimulationComputeShader->GetMaxSnowBuffer(), SimulationActor->CellsDimensionX, SimulationActor->CellsDimensionY);
}

UTexture* UDegreeDayGPUSimulation::GetSnowMapTexture()
{
	if (RenderTarget)
	{
		// Ensure render target is properly updated
		RenderTarget->UpdateResource();
		UTexture* RenderTargetTexture = Cast<UTexture>(RenderTarget);
		return RenderTargetTexture;
	}
	// Fallback to base-managed texture
	return Super::GetSnowMapTexture();
}


float UDegreeDayGPUSimulation::GetMaxSnow()
{
	return SimulationComputeShader->GetMaxSnow();
}

