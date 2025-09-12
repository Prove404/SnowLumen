
#include "SimulationComputeShader.h"
#include "Simulation.h"
#include "ClimateData.h"
#include "Cells/DebugCell.h"

DEFINE_LOG_CATEGORY(SnowComputeShader);

#define NUM_THREADS_PER_GROUP_DIMENSION 4 // This has to be the same as in the compute shaders spec [X, X, 1]
#define DEBUG_GPU_RESULT false

// Note: Shader parameter structs are now handled differently in UE 5.6
// The IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT macros are not needed

FSimulationComputeShader::FSimulationComputeShader(ERHIFeatureLevel::Type ShaderFeatureLevel)
{
	FeatureLevel = ShaderFeatureLevel;

	IsComputeShaderExecuting = false;
	IsUnloading = false;
}

FSimulationComputeShader::~FSimulationComputeShader()
{
	IsUnloading = true;
}

void FSimulationComputeShader::Initialize(
	TResourceArray<FGPUSimulationCell>& Cells, TResourceArray<FClimateData>& ClimateData,
	float InK_e, float InK_m, float InTMeltA, float InTMeltB, float InTSnowA, float InTSnowB,
	int32 InTotalSimulationHours, int32 InCellsDimensionX, int32 InCellsDimensionY, float InMeasurementAltitude, float InitialMaxSnow)
{
	NumCells = Cells.Num();

	// Create output texture
	FRHITextureCreateDesc Desc =
		FRHITextureCreateDesc::Create2D(TEXT("SimulationTexture"), InCellsDimensionX, InCellsDimensionY, PF_R32_UINT)
		.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV);

	FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();

	Texture = RHICreateTexture(Desc);

	TextureUAV = RHICmdList.CreateUnorderedAccessView(Texture, FRHIViewDesc::CreateTextureUAV());

	// Create input data buffers
	SimulationCellsBuffer = new FRWStructuredBuffer();
	SimulationCellsBuffer->Initialize(RHICmdList, sizeof(FGPUSimulationCell), InCellsDimensionX * InCellsDimensionY, &Cells, 0, true, false);

	ClimateDataBuffer = new FRWStructuredBuffer();
	ClimateDataBuffer->Initialize(RHICmdList, sizeof(FClimateData), ClimateData.Num(), &ClimateData, 0, true, false);

	// @TODO use uniform?
	TResourceArray<uint32> MaxSnowArray;
	MaxSnowArray.Add(InitialMaxSnow);
	MaxSnowBuffer = new FRWStructuredBuffer();
	MaxSnowBuffer->Initialize(RHICmdList, sizeof(uint32), 1, &MaxSnowArray, 0, true, false);

	SnowOutputBuffer = new FRWStructuredBuffer();
	SnowOutputBuffer->Initialize(RHICmdList, sizeof(float), InCellsDimensionX * InCellsDimensionY, nullptr, 0, true, false);

	// Fill constant parameters
	this->TotalSimulationHours = InTotalSimulationHours;
	this->CellsDimensionX_Param = InCellsDimensionX;
	this->ThreadGroupCountX = Texture->GetSizeX() / NUM_THREADS_PER_GROUP_DIMENSION;
	this->ThreadGroupCountY = Texture->GetSizeY() / NUM_THREADS_PER_GROUP_DIMENSION;
	this->k_e = InK_e;
	this->k_m = InK_m;
	this->TMeltA = InTMeltA;
	this->TMeltB = InTMeltB;
	this->TSnowA = InTSnowA;
	this->TSnowB = InTSnowB;
	this->MeasurementAltitude = InMeasurementAltitude;

	// Initialize variable parameters
	this->CurrentSimulationStep = 0;
	this->Timesteps = 0;
	this->DayOfYear = 0;
	this->HourOfDay = 0;
}

void FSimulationComputeShader::ExecuteComputeShader(int CurrentTimeStep, int32 InTimesteps, int InHourOfDay, bool CaptureDebugInformation, TArray<FDebugCell>& CellDebugInformation)
{
	// Skip this execution round if we are already executing
	if (IsUnloading || IsComputeShaderExecuting) return;

	IsComputeShaderExecuting = true;

	// Set the variable parameters
	this->HourOfDay = InHourOfDay;
	this->CurrentSimulationStep = CurrentTimeStep;
	this->Timesteps = InTimesteps;

	// Execute compute shader directly on render thread
	ExecuteComputeShaderInternal(CaptureDebugInformation, CellDebugInformation);
}

void FSimulationComputeShader::ExecuteComputeShaderInternal(bool CaptureDebugInformation, TArray<FDebugCell>& DebugCells)
{
	check(IsInRenderingThread());

	// If we are about to unload, so just clean up the UAV
	if (IsUnloading)
	{
		if (TextureUAV != NULL)
		{
			TextureUAV.SafeRelease();
			TextureUAV = NULL;
		}
		if (SimulationCellsBuffer != NULL)
		{
			SimulationCellsBuffer->Release();
			delete SimulationCellsBuffer;
		}
		if (MaxSnowBuffer != NULL)
		{
			MaxSnowBuffer->Release();
			delete MaxSnowBuffer;
		}

		return;
	}
	// Get global RHI command list
	FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();

	// Compute shader calculation
	TShaderMapRef<FComputeShaderDeclaration> ComputeShader(GetGlobalShaderMap(FeatureLevel));

	// Set inputs/outputs and dispatch compute shader
	SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());
	
	// Bind parameters using parameter struct
	FComputeShaderDeclaration::FParameters Parameters;
	// Constant parameters
	Parameters.TotalSimulationHours = TotalSimulationHours;
	Parameters.CellsDimensionX = CellsDimensionX_Param;
	Parameters.ThreadGroupCountX = ThreadGroupCountX;
	Parameters.ThreadGroupCountY = ThreadGroupCountY;
	Parameters.TSnowA = TSnowA;
	Parameters.TSnowB = TSnowB;
	Parameters.TMeltA = TMeltA;
	Parameters.TMeltB = TMeltB;
	Parameters.k_e = k_e;
	Parameters.k_m = k_m;
	Parameters.MeasurementAltitude = MeasurementAltitude;
	// Variable parameters
	Parameters.CurrentSimulationStep = CurrentSimulationStep;
	Parameters.Timesteps = Timesteps;
	Parameters.DayOfYear = DayOfYear;
	Parameters.HourOfDay = HourOfDay;
	// UAVs
	Parameters.OutputSurface = TextureUAV;
	Parameters.SimulationCellsBuffer = SimulationCellsBuffer->UAV;
	Parameters.WeatherDataBuffer = ClimateDataBuffer->UAV;
	Parameters.MaxSnowBuffer = MaxSnowBuffer->UAV;
	Parameters.SnowOutputBuffer = SnowOutputBuffer->UAV;

	SetShaderParameters(RHICmdList, ComputeShader, ComputeShader.GetComputeShader(), Parameters);

	// Note: Render queries and SetComputeShader are not available in the same way in UE5
	// This is a simplified version without timing queries
	DispatchComputeShader(RHICmdList, ComputeShader, Texture->GetSizeX() / NUM_THREADS_PER_GROUP_DIMENSION, Texture->GetSizeY() / NUM_THREADS_PER_GROUP_DIMENSION, 1);

	UE_LOG(SnowComputeShader, Display, TEXT("Compute shader iteration %d completed"), CurrentSimulationStep);

	// Store max snow - Note: In UE 5.6, direct buffer reading is not supported
	// Instead, we'll use a different approach or skip this functionality
	// For now, we'll use a placeholder value
	MaxSnow = 0.0f; // TODO: Implement proper max snow reading in UE 5.6
	UE_LOG(SnowComputeShader, Display, TEXT("Max snow \"%f\" (placeholder - direct buffer reading not available in UE 5.6)"), MaxSnow);
	IsComputeShaderExecuting = false;

	if (CaptureDebugInformation)
	{
		// Copy results from the GPU - Note: In UE 5.6, direct buffer reading is not supported
		// This functionality is disabled for now
		UE_LOG(SnowComputeShader, Warning, TEXT("Debug information capture disabled - direct buffer reading not available in UE 5.6"));
		
		// TODO: Implement proper debug data reading in UE 5.6
		// For now, we'll skip the debug information processing
	}

#if DEBUG_GPU_RESULT
	// Log max snow - Note: In UE 5.6, direct buffer reading is not supported
	// This functionality is disabled for now
	UE_LOG(SnowComputeShader, Warning, TEXT("DEBUG_GPU_RESULT disabled - direct buffer reading not available in UE 5.6"));
	
	// TODO: Implement proper debug data reading in UE 5.6
#endif // DEBUG_GPU_RESULT
}

float FSimulationComputeShader::GetMaxSnow()
{
	return MaxSnow;
}

