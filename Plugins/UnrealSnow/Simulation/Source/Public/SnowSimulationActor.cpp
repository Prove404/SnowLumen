#include "SnowSimulationActor.h"
#include "Misc/EngineVersionComparison.h"
#include "Simulation.h"
#include "Math/UnrealMathUtility.h"
#include "Util/MathUtil.h"
#include "Util/TextureUtil.h"
#include "Util/RuntimeMaterialChange.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RenderCommandFence.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "LandscapeDataAccess.h"
#include "UObject/NameTypes.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "SimulationWeatherDataProviderBase.h"
#include "Constant/ConstantWeatherProvider.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "SnowSimulation.h"
#include "SimpleAccumulationSim.h"

DEFINE_LOG_CATEGORY(SimulationLog);

ASnowSimulationActor::ASnowSimulationActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set default material path
	SnowSurfaceMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_VHM_Snow.M_VHM_Snow")));

	VHMMaterialInstance = nullptr;

	// Create default constant weather provider
	WeatherProvider = nullptr; // Will be set by user or created in BeginPlay
	CurrentSimulationTime = SimulationStart;
}

void ASnowSimulationActor::BeginPlay()
{
	Super::BeginPlay();

	// Compute the cells and grid from the landscape first
	Initialize();

	// Derive meters per cell (assuming 1uu = 1cm)
	MetersPerCell = (Landscape ? (Landscape->GetActorScale().X / 100.0f) : 0.0f) * static_cast<float>(CellSize);

	// Log grid information
	if (Landscape)
	{
		FVector LandscapeMin = Landscape->GetActorLocation();
		FVector LocalLandscapeScale = Landscape->GetActorScale();
		float OriginX_m = LandscapeMin.X / 100.0f;
		float OriginY_m = LandscapeMin.Y / 100.0f;
		float WidthMeters = LocalLandscapeScale.X * 100.0f;
		float HeightMeters = LocalLandscapeScale.Y * 100.0f;
		float CellSizeMeters = MetersPerCell;
		
		UE_LOG(LogTemp, Display, TEXT("[Snow] Grid meters: Origin=(%.3f,%.3f) Size=(%.3f,%.3f) Cells=%d x %d, CellSize_m=%.3f"),
			OriginX_m, OriginY_m, WidthMeters, HeightMeters, CellsDimensionX, CellsDimensionY, CellSizeMeters);
	}

	// Resolve simulation from selection or fallback
	Simulation = ResolveSimulation();

	// Log simulation decision with clear information about which class is in use
	if (UseInlineSimulation)
	{
		if (InlineSimulationClass)
		{
			UE_LOG(LogTemp, Display, TEXT("[Snow] Using inline simulation class: %s"), *InlineSimulationClass->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Snow] UseInlineSimulation=true but no InlineSimulationClass specified. Using fallback."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[Snow] Using project default simulation (UseInlineSimulation=false)"));
	}

	// Initialize components
	ClimateDataComponent = Cast<USimulationWeatherDataProviderBase>(GetComponentByClass(USimulationWeatherDataProviderBase::StaticClass()));
	if (ClimateDataComponent)
	{
		ClimateDataComponent->Initialize(StartTime, EndTime);
	}

	// Initialize simulation with grid (prefer USnowSimulation style initial grid setup if available)
	if (Simulation && Landscape)
	{
		// Prefer new BN-event initializer when using USnowSimulation
		if (USnowSimulation* SnowSim = Cast<USnowSimulation>(Simulation))
		{
			SnowSim->Initialize(CellsDimensionX, CellsDimensionY, MetersPerCell);
			// Provide terrain metadata to the simulation for redistribution models
			SnowSim->SetTerrainMetadata(LandscapeCells, CellsDimensionX, CellsDimensionY);
			// Bind material once (create MID and bind texture parameter)
			UpdateMaterialTexture();
			// Perform an initial upload so bound material sees a valid texture content
			SnowSim->UploadDepthToTexture();
		}
		else
		{
			// Fallback to legacy Initialize signature
			Simulation->Initialize(this, LandscapeCells, InitialMaxSnow, GetWorld());
			// Bind material once for legacy path as well
			UpdateMaterialTexture();
		}
		// Log resolved concrete simulation
		UE_LOG(LogTemp, Display, TEXT("[Snow] Using simulation: %s"), *Simulation->GetSimulationName());
	}
	else if (!Landscape)
	{
		UE_LOG(SimulationLog, Warning, TEXT("No landscape found in the level. Snow simulation will not initialize."));
	}

	// Log actor readiness
	UE_LOG(LogTemp, Display, TEXT("[Snow] Simulation actor ready. Cells=%d x %d"), CellsDimensionX, CellsDimensionY);

	// Weather provider resolution - derive effective provider from the four fields
	if (UseInlineWeatherProvider)
	{
		if (InlineWeatherProvider)
		{
			WeatherProvider = InlineWeatherProvider;
			UE_LOG(LogTemp, Display, TEXT("[Snow] Using inline weather provider: %s"), *InlineWeatherProvider->GetClass()->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Snow] UseInlineWeatherProvider=true but no InlineWeatherProvider specified. Creating default."));
			WeatherProvider = NewObject<UConstantWeatherProvider>(this);
		}
	}
	else
	{
		if (WeatherProviderClass)
		{
			USimulationWeatherDataProviderBase* NewProvider = NewObject<USimulationWeatherDataProviderBase>(this, WeatherProviderClass);
			if (NewProvider)
			{
				WeatherProvider = NewProvider;
				UE_LOG(LogTemp, Display, TEXT("[Snow] Using weather provider class: %s"), *WeatherProviderClass->GetName());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[Snow] Failed to instantiate weather provider from class. Creating default."));
				WeatherProvider = NewObject<UConstantWeatherProvider>(this);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Snow] UseInlineWeatherProvider=false but no WeatherProviderClass specified. Creating default."));
			WeatherProvider = NewObject<UConstantWeatherProvider>(this);
		}
	}

	if (WeatherProvider)
	{
		WeatherProvider->Initialize(SimulationStart, SimulationEnd);
	}

	// Log comprehensive startup information
	UE_LOG(LogTemp, Display, TEXT("[Snow] === STARTUP SUMMARY ==="));
	UE_LOG(LogTemp, Display, TEXT("[Snow] Weather Provider: %s (Mode: %s)"), 
		WeatherProvider ? *WeatherProvider->GetClass()->GetName() : TEXT("None"),
		UseInlineWeatherProvider ? TEXT("Inline") : TEXT("Class-based"));
	UE_LOG(LogTemp, Display, TEXT("[Snow] Simulation: %s (Mode: %s)"), 
		Simulation ? *Simulation->GetClass()->GetName() : TEXT("None"),
		UseInlineSimulation ? TEXT("Inline") : TEXT("Default"));
	UE_LOG(LogTemp, Display, TEXT("[Snow] ======================"));

	// Setup VHM integration (bind material target once if possible)
	SetupVHMIntegration();

	// Validate material parameters
	if (SnowSurfaceMaterial.IsValid() || SnowSurfaceMaterial.ToSoftObjectPath().IsValid())
	{
		UMaterialInterface* BaseMaterial = SnowSurfaceMaterial.LoadSynchronous();
		if (BaseMaterial)
		{
			bMaterialValidationPassed = CheckMaterialParamsValid(BaseMaterial);
			if (!bMaterialValidationPassed)
			{
				UE_LOG(LogTemp, Error, TEXT("[Snow] Material validation failed - simulation will not run"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Snow] Could not load SnowSurfaceMaterial for validation"));
			bMaterialValidationPassed = false;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] No SnowSurfaceMaterial set - skipping validation"));
		bMaterialValidationPassed = true; // Allow to continue with landscape material
	}

	CurrentSimulationTime = SimulationStart;

	UE_LOG(LogTemp, Display, TEXT("[Snow] Weather provider = %s, Start=%s, End=%s, dt=%.0fs"),
		   *GetNameSafe(WeatherProvider), *SimulationStart.ToString(), *SimulationEnd.ToString(), TimeStepSeconds);

	// Prime resources handled above during initialization/bind

	CurrentSleepTime = SleepTime;
	VisualAccumulator = 0.0f;
	SimulatedSecondsAccumulator = 0.0f;
	PrimaryActorTick.bCanEverTick = true;
}

void ASnowSimulationActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Retry VHM integration if bounds weren't ready initially
	if (VHMBoundsRetryCount > 0 && VHMBoundsRetryCount <= 1)
	{
		SetupVHMIntegration();
	}

	if (!Simulation || !bAutoRun) return;
	if (!bMaterialValidationPassed) return; // Early out if material validation failed
	if (IsActorBeingDestroyed()) return;
	auto* World = GetWorld();
	if (!World || World->bIsTearingDown) return;

	VisualAccumulator += DeltaTime;
	const float StepInterval = (SimRateHz > 0) ? (1.0f / static_cast<float>(SimRateHz)) : 0.25f;
	while (VisualAccumulator >= StepInterval)
	{
		VisualAccumulator -= StepInterval;

		// Perform one simulation step of duration SimDtSeconds
		FWeatherForcingData WeatherForcing;
		if (WeatherProvider)
		{
			WeatherForcing = WeatherProvider->GetWeatherForcing(CurrentSimulationTime);
		}

		if (USnowSimulation* SnowSim = Cast<USnowSimulation>(Simulation))
		{
			SnowSim->Step(SimDtSeconds, WeatherForcing, SnowSim->DepthMeters);
			SnowSim->UploadDepthToTexture();
			
			// Sync CPU buffer with simulation data for HUD display
			UpdateCpuDepthMeters(SnowSim->DepthMeters);
			
		// Log stats
		float MinV = FLT_MAX, MaxV = -FLT_MAX; double Sum = 0.0; const int32 Count = SnowSim->DepthMeters.Num();
		for (int32 i = 0; i < Count; ++i) { const float v = SnowSim->DepthMeters[i]; MinV = FMath::Min(MinV, v); MaxV = FMath::Max(MaxV, v); Sum += v; }
		const float Mean = (Count > 0) ? static_cast<float>(Sum / Count) : 0.0f;
		UE_LOG(LogTemp, Display, TEXT("[Snow] Depth min/max/mean = %.4f / %.4f / %.4f m"), MinV, MaxV, Mean);
		
		// Log HUD stats for CPU buffer
		if (CpuDepthMeters.Num() > 0)
		{
			float CpuMinV = FLT_MAX, CpuMaxV = -FLT_MAX;
			for (int32 i = 0; i < CpuDepthMeters.Num(); ++i) 
			{ 
				const float v = CpuDepthMeters[i]; 
				CpuMinV = FMath::Min(CpuMinV, v); 
				CpuMaxV = FMath::Max(CpuMaxV, v); 
			}
			UE_LOG(LogTemp, Display, TEXT("[Snow][HUD] CPUbuf min/max(mm)=%.1f/%.1f, tex size=%dx%d"), 
				CpuMinV * 1000.0f, CpuMaxV * 1000.0f, CellsDimensionX, CellsDimensionY);
		}
		}
		else
		{
			// Legacy simulation path; use existing simulate API for a single step
			Simulation->Simulate(this, this->CurrentSimulationStep, /*Timesteps=*/1, SaveMaterialTextures, DebugVisualizationType != EDebugVisualizationType::Nothing, DebugCells);
		}

		// Advance time
		CurrentSimulationTime += FTimespan::FromSeconds(SimDtSeconds);
		this->CurrentSimulationStep += FMath::RoundToInt(SimDtSeconds / 3600.0f);
		if (bLoopTime && CurrentSimulationTime > SimulationEnd)
		{
			CurrentSimulationTime = SimulationStart;
			this->CurrentSimulationStep = 0;
		}

		// Update material bindings after the step
		UpdateMaterialTexture();
	}

	if (DebugVisualizationType != EDebugVisualizationType::Nothing) DoRenderDebugInformation();
	if (RenderGrid) DoRenderGrid();
}

void ASnowSimulationActor::DoRenderGrid()
{
	const auto Location = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraLocation();
	
	for (FDebugCell& Cell : DebugCells)
	{
		FVector Normal(Cell.Normal);
		Normal.Normalize();

		// @TODO get exact position using the height map
		FVector zOffset(0, 0, DebugGridZOffset);

		if (FVector::Dist(Cell.Centroid, Location) < CellDebugInfoDisplayDistance)
		{
			// Draw Cells
			DrawDebugLine(GetWorld(), Cell.P1 + zOffset, Cell.P2 + zOffset, FColor(255, 0, 0), false, -1, 0, 0.0f);
			DrawDebugLine(GetWorld(), Cell.P1 + zOffset, Cell.P3 + zOffset, FColor(255, 0, 0), false, -1, 0, 0.0f);
			DrawDebugLine(GetWorld(), Cell.P2 + zOffset, Cell.P4 + zOffset, FColor(255, 0, 0), false, -1, 0, 0.0f);
			DrawDebugLine(GetWorld(), Cell.P3 + zOffset, Cell.P4 + zOffset, FColor(255, 0, 0), false, -1, 0, 0.0f);
		}
	}
}

void ASnowSimulationActor::DoRenderDebugInformation()
{
	const auto Location = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraLocation();
	auto PlayerController = GetWorld()->GetFirstPlayerController();
	auto Pawn = PlayerController->GetPawn();

	// Draw SWE normal
	if (DebugVisualizationType == EDebugVisualizationType::SnowHeight)
	{
		for (auto& Cell : DebugCells)
		{
			// Sample snow height from CPU buffer instead of Cell.SnowMM
			float SnowHeightMM = 0.0f;
			if (CpuDepthMeters.Num() > 0)
			{
				// Convert world position to grid coordinates
				const FVector2D GridPos = FVector2D(
					(Cell.Centroid.X - Landscape->GetActorLocation().X) / (Landscape->GetActorScale().X * CellSize),
					(Cell.Centroid.Y - Landscape->GetActorLocation().Y) / (Landscape->GetActorScale().Y * CellSize)
				);
				
				const int32 GridX = FMath::Clamp(FMath::RoundToInt(GridPos.X), 0, CellsDimensionX - 1);
				const int32 GridY = FMath::Clamp(FMath::RoundToInt(GridPos.Y), 0, CellsDimensionY - 1);
				const int32 Index = GridY * CellsDimensionX + GridX;
				
				if (CpuDepthMeters.IsValidIndex(Index))
				{
					SnowHeightMM = CpuDepthMeters[Index] * 1000.0f; // Convert meters to mm
				}
			}
			
			if (SnowHeightMM > 0 && FVector::Dist(Cell.P1, Location) < CellDebugInfoDisplayDistance)
			{
				FVector Normal(Cell.Normal);
				Normal.Normalize();

				// @TODO get exact position using the height map
				FVector zOffset(0, 0, DebugGridZOffset);

				DrawDebugLine(GetWorld(), Cell.Centroid + zOffset, Cell.Centroid + FVector(0, 0, SnowHeightMM / 10) + zOffset, FColor(255, 0, 0), false, -1, 0, 0.0f);
			}
		}
	}

	// Render debug strings
	int Index = 0;
	for (auto& Cell : DebugCells)
	{
		auto Offset = Cell.Normal;
		Offset.Normalize();

		// @TODO get position from heightmap
		Offset *= 10;

		if (FVector::Dist(Cell.P1 + Offset, Location) < CellDebugInfoDisplayDistance)
		{
			FCollisionQueryParams TraceParams(FName(TEXT("Trace SWE")), true);
			TraceParams.bTraceComplex = true;
			TraceParams.AddIgnoredActor(Pawn);

			FHitResult HitOut(ForceInit);

			GetWorld()->LineTraceSingleByChannel(HitOut, Location, Cell.P1 + Offset, ECC_WorldStatic, TraceParams);

			auto Hit = HitOut.GetActor();

			//Hit any Actor?
			if (Hit == NULL)
			{
				switch (DebugVisualizationType)
				{
				case EDebugVisualizationType::SnowHeight:
					{
						// Sample snow height from CPU buffer instead of Cell.SnowMM
						float SnowHeightMM = 0.0f;
						if (CpuDepthMeters.Num() > 0)
						{
							// Convert world position to grid coordinates
							const FVector2D GridPos = FVector2D(
								(Cell.Centroid.X - Landscape->GetActorLocation().X) / (Landscape->GetActorScale().X * CellSize),
								(Cell.Centroid.Y - Landscape->GetActorLocation().Y) / (Landscape->GetActorScale().Y * CellSize)
							);
							
							const int32 GridX = FMath::Clamp(FMath::RoundToInt(GridPos.X), 0, CellsDimensionX - 1);
							const int32 GridY = FMath::Clamp(FMath::RoundToInt(GridPos.Y), 0, CellsDimensionY - 1);
							const int32 CellIndex = GridY * CellsDimensionX + GridX;
							
							if (CpuDepthMeters.IsValidIndex(CellIndex))
							{
								SnowHeightMM = CpuDepthMeters[CellIndex] * 1000.0f; // Convert meters to mm
							}
						}
						DrawDebugString(GetWorld(), Cell.Centroid, FString::FromInt(static_cast<int>(SnowHeightMM)) + " mm", nullptr, FColor::Purple, 0, true);
					}
					break;
				case EDebugVisualizationType::Position:
					DrawDebugString(GetWorld(), Cell.Centroid, "(" + FString::FromInt(static_cast<int>(Cell.Centroid.X / 100)) + "/" + FString::FromInt(static_cast<int>(Cell.Centroid.Y / 100)) + ")", nullptr, FColor::Purple, 0, true);
					break;
				case EDebugVisualizationType::Altitude:
					DrawDebugString(GetWorld(), Cell.Centroid, FString::FromInt(static_cast<int>(Cell.Altitude / 100)) + "m", nullptr, FColor::Purple, 0, true);
					break;
				case EDebugVisualizationType::Index:
					DrawDebugString(GetWorld(), Cell.Centroid, FString::FromInt(Index), nullptr, FColor::Purple, 0, true);
					break;
				case EDebugVisualizationType::Aspect:
					DrawDebugString(GetWorld(), Cell.Centroid, FString::FromInt(static_cast<int>(FMath::RadiansToDegrees(Cell.Aspect))), nullptr, FColor::Purple, 0, true);
					break;
				case EDebugVisualizationType::Curvature:
					DrawDebugString(GetWorld(), Cell.Centroid, FString::SanitizeFloat(Cell.Curvature), nullptr, FColor::Purple, 0, true);
					break;
				default:
					break;
				}
			}
		}

		Index++;
	}
}

void ASnowSimulationActor::Initialize()
{
	if (GetWorld())
	{
		auto Level = GetWorld()->PersistentLevel;

		for (TActorIterator<ALandscape> LandscapeIterator(GetWorld()); LandscapeIterator; ++LandscapeIterator)
		{
			if (LandscapeIterator->ActorHasTag(FName("landscape"))) Landscape = *LandscapeIterator;
		}

		if (Landscape)
		{
			LandscapeScale = Landscape->GetActorScale();
			auto& LandscapeComponents = Landscape->LandscapeComponents;
			auto NumLandscapes = Landscape->LandscapeComponents.Num();
			auto LastLandscapeComponent = Landscape->LandscapeComponents[NumLandscapes - 1];
			int32 NumComponentsX = LastLandscapeComponent->SectionBaseX / LastLandscapeComponent->ComponentSizeQuads + 1;
			int32 NumComponentsY = LastLandscapeComponent->SectionBaseY / LastLandscapeComponent->ComponentSizeQuads + 1;

			OverallResolutionX = Landscape->SubsectionSizeQuads * Landscape->NumSubsections * NumComponentsX + 1;
			OverallResolutionY = Landscape->SubsectionSizeQuads * Landscape->NumSubsections * NumComponentsY + 1;

			CellsDimensionX = OverallResolutionX / CellSize - 1; // -1 because we create cells and use 4 vertices
			CellsDimensionY = OverallResolutionY / CellSize - 1; // -1 because we create cells and use 4 vertices
			NumCells = CellsDimensionX * CellsDimensionY;

			DebugCells.Reserve(CellsDimensionX * CellsDimensionY);
			LandscapeCells.Reserve(CellsDimensionX * CellsDimensionY);

			TArray<FVector> CellWorldVertices;
			CellWorldVertices.SetNumUninitialized(OverallResolutionX * OverallResolutionY);

			float MinAltitude = 1e6;
			float MaxAltitude = 0;
			for (auto Component : LandscapeComponents)
			{
				// @TODO use runtime compatible version
				FLandscapeComponentDataInterface LandscapeData(Component);
				for (int32 Y = 0; Y < Component->ComponentSizeQuads; Y++) // not +1 because the vertices are stored twice (first and last)
				{
					for (int32 X = 0; X < Component->ComponentSizeQuads; X++) // not +1 because the vertices are stored twice (first and last)
					{
						auto Vertex = LandscapeData.GetWorldVertex(X, Y);
						CellWorldVertices[Component->SectionBaseX + X + OverallResolutionX * Y + Component->SectionBaseY * OverallResolutionX] = Vertex;
						MinAltitude = FMath::Min(MinAltitude, Vertex.Z);
						MaxAltitude = FMath::Max(MaxAltitude, Vertex.Z);
					}
				}
			}

			// Distance between neighboring cells in cm (calculate as in https://forums.unrealengine.com/showthread.php?57338-Calculating-Exact-Map-Size)
			const float L = LandscapeScale.X / 100 * CellSize;

			/*
			// Calculate slope map
			SlopeTexture = UTexture2D::CreateTransient((OverallResolutionX - 1), (OverallResolutionY - 1), EPixelFormat::PF_B8G8R8A8);
			SlopeTexture->UpdateResource();

			TArray<FColor> SlopeTextureData;
			SlopeTextureData.Empty((OverallResolutionX - 1) * (OverallResolutionY - 1));

			for (int32 Y = 1; Y < OverallResolutionY - 1; Y++)
			{
				for (int32 X = 1; X < OverallResolutionX - 1; X++)
				{
					FVector C1 = CellWorldVertices[(Y - 1) * (OverallResolutionX - 1) + (X - 1)];
					FVector C2 = CellWorldVertices[(Y - 1) * (OverallResolutionX - 1) + (X + 0)];
					FVector C3 = CellWorldVertices[(Y - 1) * (OverallResolutionX - 1) + (X + 1)];
					FVector C4 = CellWorldVertices[(Y + 0) * (OverallResolutionX - 1) + (X - 1)];
					FVector C5 = CellWorldVertices[(Y + 0) * (OverallResolutionX - 1) + (X + 0)];
					FVector C6 = CellWorldVertices[(Y + 0) * (OverallResolutionX - 1) + (X + 1)];
					FVector C7 = CellWorldVertices[(Y + 1) * (OverallResolutionX - 1) + (X - 1)];
					FVector C8 = CellWorldVertices[(Y + 1) * (OverallResolutionX - 1) + (X + 0)];
					FVector C9 = CellWorldVertices[(Y + 1) * (OverallResolutionX - 1) + (X + 1)];

					float x = (C3.Z + 2 * C6.Z + C9.Z - C1.Z - 2 * C4.Z - C7.Z) / (8 * L * 100);
					float y = (C1.Z + 2 * C2.Z + C3.Z - C7.Z - 2 * C8.Z - C9.Z) / (8 * L * 100);
					float Slope = FMath::Atan(FMath::Sqrt(x * x + y * y));
					float GrayScale = Slope / (PI / 2) * 255;
					uint8 GrayInt = static_cast<uint8>(GrayScale);
					SlopeTextureData.Add(FColor(GrayInt, GrayInt, GrayInt));
				}
			}
			FRenderCommandFence UpdateTextureFence;
			UpdateTextureFence.BeginFence();

			UpdateTexture(SlopeTexture, SlopeTextureData);

			UpdateTextureFence.Wait();

			if (SaveMaterialTextures)
			{
				// Create screenshot folder if not already present.
				IFileManager::Get().MakeDirectory(*FPaths::ScreenShotDir(), true);

				const FString ScreenFileName(FPaths::ScreenShotDir() / TEXT("SlopeMap"));

				uint32 ExtendXWithMSAA = SlopeTextureData.Num() / SlopeTexture->GetSizeY();

				// Save the contents of the array to a bitmap file. (24bit only so alpha channel is dropped)
				FFileHelper::CreateBitmap(*ScreenFileName, ExtendXWithMSAA, SlopeTexture->GetSizeY(), SlopeTextureData.GetData());
			}
			*/

			// Create Cells
			int Index = 0;
			for (int32 Y = 0; Y < CellsDimensionY; Y++)
			{
				for (int32 X = 0; X < CellsDimensionX; X++)
				{
					auto VertexX = X * CellSize;
					auto VertexY = Y * CellSize;
					FVector P0 = CellWorldVertices[VertexY * OverallResolutionX + VertexX];
					FVector P1 = CellWorldVertices[VertexY * OverallResolutionX + (VertexX + CellSize)];
					FVector P2 = CellWorldVertices[(VertexY + CellSize) * OverallResolutionX + VertexX];
					FVector P3 = CellWorldVertices[(VertexY + CellSize) * OverallResolutionX + (VertexX + CellSize)];

					FVector Normal = FVector::CrossProduct(P1 - P0, P2 - P0);
					FVector Centroid = FVector((P0.X + P1.X + P2.X + P3.X) / 4, (P0.Y + P1.Y + P2.Y + P3.Y) / 4, (P0.Z + P1.Z + P2.Z + P3.Z) / 4);

					float Altitude = Centroid.Z;

					float Area = FMath::Abs(FVector::CrossProduct(P0 - P3, P1 - P3).Size() / 2 + FVector::CrossProduct(P2 - P3, P0 - P3).Size() / 2);

					float AreaXY = FMath::Abs(FVector2D::CrossProduct(FVector2D(P0 - P3), FVector2D(P1 - P3)) / 2
						+ FVector2D::CrossProduct(FVector2D(P2 - P3), FVector2D(P0 - P3)) / 2);

					FVector P0toP3 = P3 - P0;
					FVector P0toP3ProjXY = FVector(P0toP3.X, P0toP3.Y, 0);
					float Inclination = IsAlmostZero(P0toP3.Size()) ? 0 : FMath::Abs(FMath::Acos(FVector::DotProduct(P0toP3, P0toP3ProjXY) / (P0toP3.Size() * P0toP3ProjXY.Size())));

					// @TODO assume constant for the moment, later handle in input data
					const float CellLatitude = FMath::DegreesToRadians(47);

					// @TODO what is the aspect of the XY plane?
					
					
					FVector2D NormalProjXY = FVector2D(Normal.X, Normal.Y);
					FVector2D North2D = FVector2D(1, 0);
					float Dot = FVector2D::DotProduct(NormalProjXY, North2D);
					float Det = NormalProjXY.X * North2D.Y - NormalProjXY.Y*North2D.X;
					float Aspect = FMath::Atan2(Det, Dot);
					Aspect = NormalizeAngle360(Aspect);
					
					//float Aspect = IsAlmostZero(NormalProjXY.Size()) ? 0 : FMath::Abs(FMath::Acos(FVector::DotProduct(North, NormalProjXY) / NormalProjXY.Size()));

					// Initial conditions
					float SnowWaterEquivalent = 0.0f;
					if (Altitude / 100.0f > 3300.0f)
					{
						auto AreaSquareMeters = Area / (100 * 100);
						float we = (2.5 + Altitude / 100 * 0.001) * AreaSquareMeters;

						SnowWaterEquivalent = we;

						InitialMaxSnow = FMath::Max(SnowWaterEquivalent / AreaSquareMeters, InitialMaxSnow);
					}

					// Create cells
					FLandscapeCell Cell(Index, P0, P1, P2, P3, Normal, Area, AreaXY, Centroid, Altitude, Aspect, Inclination, Latitude, SnowWaterEquivalent);
					LandscapeCells.Add(Cell);

					FDebugCell DebugCell(P0, P1, P2, P3, Centroid, Normal, Altitude, Aspect);
					DebugCells.Add(DebugCell);

					Index++;
				}
			}

		
			// Calculate curvature
			for (int32 CellIndexY = 0; CellIndexY < CellsDimensionY; ++CellIndexY)
			{
				for (int32 CellIndexX = 0; CellIndexX < CellsDimensionX; ++CellIndexX)
				{
					FLandscapeCell& Cell = LandscapeCells[CellIndexX + CellsDimensionX * CellIndexY];
					FLandscapeCell* Neighbours[8];

					Neighbours[0] = GetCellChecked(CellIndexX, CellIndexY - 1);						// N
					Neighbours[1] = GetCellChecked(CellIndexX + 1, CellIndexY - 1);					// NE
					Neighbours[2] = GetCellChecked(CellIndexX + 1, CellIndexY);						// E
					Neighbours[3] = GetCellChecked(CellIndexX + 1, CellIndexY + 1);					// SE

					Neighbours[4] = GetCellChecked(CellIndexX, CellIndexY + 1);						// S
					Neighbours[5] = GetCellChecked(CellIndexX - 1, CellIndexY + 1); 				// SW
					Neighbours[6] = GetCellChecked(CellIndexX - 1, CellIndexY);						// W
					Neighbours[7] = GetCellChecked(CellIndexX - 1, CellIndexY - 1);					// NW

					if (Neighbours[0] == nullptr || Neighbours[1] == nullptr || Neighbours[2] == nullptr || Neighbours[3] == nullptr
						|| Neighbours[4] == nullptr || Neighbours[5] == nullptr || Neighbours[6] == nullptr || Neighbours[7] == nullptr) continue;

					float Z1 = Neighbours[1]->Altitude / 100; // NW
					float Z2 = Neighbours[0]->Altitude / 100; // N
					float Z3 = Neighbours[7]->Altitude / 100; // NE
					float Z4 = Neighbours[2]->Altitude / 100; // W
					float Z5 = Cell.Altitude / 100;
					float Z6 = Neighbours[6]->Altitude / 100; // E
					float Z7 = Neighbours[3]->Altitude / 100; // SW	
					float Z8 = Neighbours[4]->Altitude / 100; // S
					float Z9 = Neighbours[5]->Altitude / 100; // SE

					float D = ((Z4 + Z6) / 2 - Z5) / (L * L);
					float E = ((Z2 + Z8) / 2 - Z5) / (L * L);
					Cell.Curvature = 2 * (D + E);
				}
			}

			UE_LOG(SimulationLog, Display, TEXT("Num components: %d"), LandscapeComponents.Num());
			UE_LOG(SimulationLog, Display, TEXT("Num subsections: %d"), Landscape->NumSubsections);
			UE_LOG(SimulationLog, Display, TEXT("SubsectionSizeQuads: %d"), Landscape->SubsectionSizeQuads);
			UE_LOG(SimulationLog, Display, TEXT("ComponentSizeQuads: %d"), Landscape->ComponentSizeQuads);

			// Update shader
			SetScalarParameterValue(Landscape, Param_CellsDimensionX, CellsDimensionX);
			SetScalarParameterValue(Landscape, Param_CellsDimensionY, CellsDimensionY);
			SetScalarParameterValue(Landscape, Param_ResolutionX, OverallResolutionX);
			SetScalarParameterValue(Landscape, Param_ResolutionY, OverallResolutionY);
		}
		else
		{
			UE_LOG(SimulationLog, Warning, TEXT("No landscape found with 'landscape' tag. Simulation will not initialize properly."));
		}
	}
}

void ASnowSimulationActor::UpdateMaterialTexture()
{
	if (!Simulation) return;

	// Prefer USnowSimulation texture path
	UTexture* SnowMap = Simulation->GetSnowMapTexture();
	UTexture2D* SnowTex2D = Cast<UTexture2D>(SnowMap);
	if (!SnowTex2D)
	{
		return;
	}

	// Initialize binding: VHM takes priority, Landscape only if VHM is absent
	if (!ActiveRenderBinding.bInitialized)
	{
		// Try VHM first - if not found, SetupVHMIntegration will fallback to Landscape
		SetupVHMIntegration();
	}

	// Only update Landscape MID if VHM is not present
	if (ActiveRenderBinding.Target == FRenderBinding::ETarget::Landscape && Landscape)
	{
		// Bind or reuse a single MID for the landscape material
		if (!SnowMID)
		{
			// Either create from selected snow surface material or from current
			UMaterialInterface* BaseMaterial = nullptr;
			if (SnowSurfaceMaterial.IsValid() || SnowSurfaceMaterial.ToSoftObjectPath().IsValid())
			{
				BaseMaterial = SnowSurfaceMaterial.LoadSynchronous();
			}
			if (!BaseMaterial)
			{
				BaseMaterial = Landscape->GetLandscapeMaterial();
			}
			if (BaseMaterial)
			{
				SnowMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			}
		}

		// Apply to landscape
		SetTextureParameterValue(Landscape, Param_SnowMap, SnowTex2D, GEngine);
		UE_LOG(LogTemp, Display, TEXT("[Snow] Landscape SetParam SnowMap=Texture"));
	}

	// Apply snow parameters using the active binding
	ApplySnowParams(ActiveRenderBinding, SnowTex2D);
}

static bool HasTexParam(UMaterialInterface* Mat, FName Name)
{
	if (!Mat) return false;
	TArray<FMaterialParameterInfo> Infos; TArray<FGuid> Ids;
	Mat->GetAllTextureParameterInfo(Infos, Ids);
	return Infos.ContainsByPredicate([&](const FMaterialParameterInfo& I){ return I.Name == Name; });
}

static bool HasScalarParam(UMaterialInterface* Mat, FName Name)
{
	if (!Mat) return false;
	TArray<FMaterialParameterInfo> Infos; TArray<FGuid> Ids;
	Mat->GetAllScalarParameterInfo(Infos, Ids);
	return Infos.ContainsByPredicate([&](const FMaterialParameterInfo& I){ return I.Name == Name; });
}

static bool HasVectorParam(UMaterialInterface* Mat, FName Name)
{
	if (!Mat) return false;
	TArray<FMaterialParameterInfo> Infos; TArray<FGuid> Ids;
	Mat->GetAllVectorParameterInfo(Infos, Ids);
	return Infos.ContainsByPredicate([&](const FMaterialParameterInfo& I){ return I.Name == Name; });
}

void ASnowSimulationActor::SetupVHMIntegration()
{
	if (!GetWorld()) return;

	// Find a Virtual Heightfield Mesh component without including VHM headers (UE5.6 compatible)
	AActor* FoundActor = nullptr;
	UActorComponent* FoundComponent = nullptr;

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		const TSet<UActorComponent*>& Components = Actor->GetComponents();
		for (UActorComponent* Component : Components)
		{
			if (!Component) continue;
			const FString ClassName = Component->GetClass()->GetName();
			if (ClassName.Contains(TEXT("VirtualHeightfieldMesh")))
			{
				FoundActor = Actor;
				FoundComponent = Component;
				break;
			}
		}

		if (FoundComponent)
		{
			break;
		}
	}

	if (!FoundComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] No VirtualHeightfieldMesh component found in level. Will bind to Landscape instead."));
		// Fallback to landscape binding
		SetupLandscapeBinding();
		return;
	}

	UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(FoundComponent);
	if (!PrimComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] Found VHM component is not a UPrimitiveComponent. Will bind to Landscape instead."));
		SetupLandscapeBinding();
		return;
	}

	// Get VHM world bounds and convert cm→m for origin/size
	FBox Bounds = PrimComponent->Bounds.GetBox();
	const FVector MinCm = Bounds.Min;
	const FVector MaxCm = Bounds.Max;
	const FVector2D OriginMeters = FVector2D(MinCm.X, MinCm.Y) / 100.0f;
	const FVector2D SizeMeters = FVector2D(MaxCm.X - MinCm.X, MaxCm.Y - MinCm.Y) / 100.0f;

	// Check if bounds are ready (size > small number)
	const float SmallNumber = 0.001f; // 1mm threshold
	if (SizeMeters.X <= SmallNumber || SizeMeters.Y <= SmallNumber)
	{
		VHMBoundsRetryCount++;
		if (VHMBoundsRetryCount <= 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Snow] VHM bounds not ready yet (SizeMeters=(%.6f,%.6f)). Retrying next tick..."), SizeMeters.X, SizeMeters.Y);
			return; // Defer one tick and retry
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Snow] VHM bounds still zero after retry. Falling back to grid size."));
			// Fallback to grid size only if bounds still zero
			const FVector2D GridSizeMeters = FVector2D(CellsDimensionX * MetersPerCell, CellsDimensionY * MetersPerCell);
			const FVector ActorLocationCm = GetActorLocation();
			const FVector2D GridOriginMeters = FVector2D(ActorLocationCm.X, ActorLocationCm.Y) / 100.0f;
			
			// Use grid domain for VHM parameters
			SetupVHMMaterialParameters(PrimComponent, FoundActor, GridOriginMeters, GridSizeMeters);
			return;
		}
	}

	// Bounds are ready, proceed with VHM bounds
	SetupVHMMaterialParameters(PrimComponent, FoundActor, OriginMeters, SizeMeters);

}

void ASnowSimulationActor::SetupVHMMaterialParameters(UPrimitiveComponent* PrimComponent, AActor* FoundActor, const FVector2D& OriginMeters, const FVector2D& SizeMeters)
{
	// Never overwrite a valid VHM mapping once set
	if (ActiveRenderBinding.bInitialized && ActiveRenderBinding.Target == FRenderBinding::ETarget::VHM)
	{
		UE_LOG(LogTemp, Display, TEXT("[Snow] VHM mapping already set, preserving existing values"));
		return;
	}

	// Compute inverse size per meter - check for division by zero
	if (SizeMeters.X <= 0.0f || SizeMeters.Y <= 0.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("[Snow] VHM SizeMeters is zero or negative: (%.6f,%.6f). Cannot compute inverse."), SizeMeters.X, SizeMeters.Y);
		return;
	}

	const FVector2D InvSizePerMeter = FVector2D(1.0f / SizeMeters.X, 1.0f / SizeMeters.Y);

	// Assert finite and >0 (fixed assertion logic)
	checkf(FMath::IsFinite(InvSizePerMeter.X) && FMath::IsFinite(InvSizePerMeter.Y), 
		TEXT("VHM inverse size per meter must be finite"));
	checkf(InvSizePerMeter.X > 0.0f && InvSizePerMeter.Y > 0.0f, 
		TEXT("VHM inverse size per meter must be positive"));

	// Store VHM-specific mapping as active binding
	ActiveRenderBinding.Target = FRenderBinding::ETarget::VHM;
	ActiveRenderBinding.OriginMeters = OriginMeters;
	ActiveRenderBinding.SizeMeters = SizeMeters;
	ActiveRenderBinding.InvSizePerMeter = InvSizePerMeter;
	ActiveRenderBinding.bInitialized = true;

	// Log: origin, size, inverse, displacement
	UE_LOG(LogTemp, Display, TEXT("[Snow] VHM Domain: OriginMeters=(%.6f,%.6f), SizeMeters=(%.6f,%.6f), InvSizePerMeter=(%.8f,%.8f), DisplacementScale=%.3f"),
		OriginMeters.X, OriginMeters.Y, SizeMeters.X, SizeMeters.Y, InvSizePerMeter.X, InvSizePerMeter.Y, SnowDisplacementScale);

	// Resolve base material
	UMaterialInterface* BaseMaterial = nullptr;
	if (SnowSurfaceMaterial.IsValid() || SnowSurfaceMaterial.ToSoftObjectPath().IsValid())
	{
		BaseMaterial = SnowSurfaceMaterial.LoadSynchronous();
	}

	if (!BaseMaterial)
	{
		BaseMaterial = PrimComponent->GetMaterial(TargetVHMSlotIndex);
	}

	if (!BaseMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] VHM base material not set (slot %d). Skipping VHM binding."), TargetVHMSlotIndex);
		return;
	}

	// Validate expected parameters (warn-only)
	ValidateMaterialParameters(BaseMaterial);

	// Create or reuse a dynamic material instance
	UMaterialInterface* ExistingMat = PrimComponent->GetMaterial(TargetVHMSlotIndex);
	if (bOverrideExistingMaterial || ExistingMat == nullptr)
	{
		VHMMaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		PrimComponent->SetMaterial(TargetVHMSlotIndex, VHMMaterialInstance);
	}
	else
	{
		VHMMaterialInstance = Cast<UMaterialInstanceDynamic>(ExistingMat);
		if (!VHMMaterialInstance)
		{
			VHMMaterialInstance = UMaterialInstanceDynamic::Create(ExistingMat, this);
			PrimComponent->SetMaterial(TargetVHMSlotIndex, VHMMaterialInstance);
		}
	}

	if (VHMMaterialInstance)
	{
		// Initialize static parameters; dynamic ones are updated each tick
		VHMMaterialInstance->SetScalarParameterValue(Param_SnowDisplacementScale, SnowDisplacementScale);
		VHMMaterialInstance->SetScalarParameterValue(Param_SparkleIntensity, SparkleIntensity);
		VHMMaterialInstance->SetScalarParameterValue(Param_SparkleScale, SparkleScale);
		VHMMaterialInstance->SetScalarParameterValue(Param_SnowAgeDays, SnowAgeDays);
		VHMMaterialInstance->SetScalarParameterValue(Param_Grain_um, GrainSize_um);
		VHMMaterialInstance->SetScalarParameterValue(Param_Impurity_ppm, Impurity_ppm);

		// Set simulation domain parameters
		VHMMaterialInstance->SetVectorParameterValue(Param_SnowOriginMeters, FLinearColor(OriginMeters.X, OriginMeters.Y, 0, 0));
		VHMMaterialInstance->SetVectorParameterValue(Param_SnowInvSizePerMeter, FLinearColor(InvSizePerMeter.X, InvSizePerMeter.Y, 0, 0));
		VHMMaterialInstance->SetScalarParameterValue(Param_SnowDisplacementScale, SnowDisplacementScale);

		// Log all static material parameters set on VHM during setup
		UE_LOG(LogTemp, Display, TEXT("[Snow] VHM Setup SetParam %s=%.3f, %s=%.3f, %s=%.3f, %s=%.3f, %s=%.3f, %s=%.3f"),
			*Param_SnowDisplacementScale.ToString(), SnowDisplacementScale,
			*Param_SparkleIntensity.ToString(), SparkleIntensity,
			*Param_SparkleScale.ToString(), SparkleScale,
			*Param_SnowAgeDays.ToString(), SnowAgeDays,
			*Param_Grain_um.ToString(), GrainSize_um,
			*Param_Impurity_ppm.ToString(), Impurity_ppm);

		UE_LOG(LogTemp, Display, TEXT("[Snow] Bound snow material to VHM (actor=%s, slot=%d)"), *FoundActor->GetName(), TargetVHMSlotIndex);
		
		// Log binding setup once per target
		UE_LOG(LogTemp, Display, TEXT("[Snow][Bind] Target=VHM Origin=(%.3f,%.3f) Size=(%.0f,%.0f) Inv=(%.8f,%.8f)"),
			OriginMeters.X, OriginMeters.Y, SizeMeters.X, SizeMeters.Y, InvSizePerMeter.X, InvSizePerMeter.Y);
	}

}

void ASnowSimulationActor::SetupLandscapeBinding()
{
	if (!Landscape)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] No landscape found. Cannot setup landscape binding."));
		return;
	}

	FVector LandscapeMin = Landscape->GetActorLocation();
	FVector LocalLandscapeScale = Landscape->GetActorScale();
	float OriginX_m = LandscapeMin.X / 100.0f;
	float OriginY_m = LandscapeMin.Y / 100.0f;
	float WidthMeters = LocalLandscapeScale.X * 100.0f;
	float HeightMeters = LocalLandscapeScale.Y * 100.0f;
	float InvX_per_m = (WidthMeters != 0.0f) ? (1.0f / WidthMeters) : 0.0f;
	float InvY_per_m = (HeightMeters != 0.0f) ? (1.0f / HeightMeters) : 0.0f;

	// Store landscape-specific mapping as active binding
	ActiveRenderBinding.Target = FRenderBinding::ETarget::Landscape;
	ActiveRenderBinding.OriginMeters = FVector2D(OriginX_m, OriginY_m);
	ActiveRenderBinding.SizeMeters = FVector2D(WidthMeters, HeightMeters);
	ActiveRenderBinding.InvSizePerMeter = FVector2D(InvX_per_m, InvY_per_m);
	ActiveRenderBinding.bInitialized = true;

	// Log binding setup once per target
	UE_LOG(LogTemp, Display, TEXT("[Snow][Bind] Target=Landscape Origin=(%.3f,%.3f) Size=(%.0f,%.0f) Inv=(%.8f,%.8f)"),
		OriginX_m, OriginY_m, WidthMeters, HeightMeters, InvX_per_m, InvY_per_m);
}

void ASnowSimulationActor::ApplySnowParams(const ASnowSimulationActor::FRenderBinding& Binding, UTexture2D* SnowTex2D)
{
	if (!Binding.bInitialized || !SnowTex2D)
	{
		return;
	}

	if (Binding.Target == FRenderBinding::ETarget::VHM && VHMMaterialInstance)
	{
		VHMMaterialInstance->SetTextureParameterValue(Param_SnowDepthTex, SnowTex2D);
		VHMMaterialInstance->SetScalarParameterValue(Param_SnowDisplacementScale, SnowDisplacementScale);
		VHMMaterialInstance->SetVectorParameterValue(Param_SnowOriginMeters, FLinearColor(Binding.OriginMeters.X, Binding.OriginMeters.Y, 0, 0));
		VHMMaterialInstance->SetVectorParameterValue(Param_SnowInvSizePerMeter, FLinearColor(Binding.InvSizePerMeter.X, Binding.InvSizePerMeter.Y, 0, 0));
		
		// VHM parameters applied using stored binding mapping
	}
	else if (Binding.Target == FRenderBinding::ETarget::Landscape && SnowMID)
	{
		SnowMID->SetTextureParameterValue(Param_SnowDepthTex, SnowTex2D);
		SnowMID->SetScalarParameterValue(Param_SnowDisplacementScale, SnowDisplacementScale);
		SnowMID->SetVectorParameterValue(Param_SnowOriginMeters, FLinearColor(Binding.OriginMeters.X, Binding.OriginMeters.Y, 0, 0));
		SnowMID->SetVectorParameterValue(Param_SnowInvSizePerMeter, FLinearColor(Binding.InvSizePerMeter.X, Binding.InvSizePerMeter.Y, 0, 0));
		
		// Landscape parameters applied using stored binding mapping
	}
}

#if WITH_EDITOR
void ASnowSimulationActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	//Get the name of the property that was changed
	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if ((PropertyName == GET_MEMBER_NAME_CHECKED(ASnowSimulationActor, CellSize))) {
		Initialize();
	}
}
#endif

bool ASnowSimulationActor::ValidateMaterialParameters(UMaterialInterface* BaseMat)
{
	if (!BaseMat)
	{
		UE_LOG(LogTemp, Error, TEXT("[Snow] Material validation failed: No material provided"));
		return false;
	}

	bool bValidationPassed = true;

	// Core snow parameters (required - hard fail if missing)
	if (!HasTexParam(BaseMat, Param_SnowDepthTex))
	{
		UE_LOG(LogTemp, Error, TEXT("[Snow] Material validation failed: Missing required texture parameter '%s'"), *Param_SnowDepthTex.ToString());
		bValidationPassed = false;
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[Snow] Material validation: Found required texture parameter '%s'"), *Param_SnowDepthTex.ToString());
	}

	if (!HasVectorParam(BaseMat, Param_SnowOriginMeters))
	{
		UE_LOG(LogTemp, Error, TEXT("[Snow] Material validation failed: Missing required vector parameter '%s'"), *Param_SnowOriginMeters.ToString());
		bValidationPassed = false;
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[Snow] Material validation: Found required vector parameter '%s'"), *Param_SnowOriginMeters.ToString());
	}

	if (!HasVectorParam(BaseMat, Param_SnowInvSizePerMeter))
	{
		UE_LOG(LogTemp, Error, TEXT("[Snow] Material validation failed: Missing required vector parameter '%s'"), *Param_SnowInvSizePerMeter.ToString());
		bValidationPassed = false;
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[Snow] Material validation: Found required vector parameter '%s'"), *Param_SnowInvSizePerMeter.ToString());
	}

	if (!HasScalarParam(BaseMat, Param_SnowDisplacementScale))
	{
		UE_LOG(LogTemp, Error, TEXT("[Snow] Material validation failed: Missing required scalar parameter '%s'"), *Param_SnowDisplacementScale.ToString());
		bValidationPassed = false;
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[Snow] Material validation: Found required scalar parameter '%s'"), *Param_SnowDisplacementScale.ToString());
	}

	// Visual parameters (optional - warn but don't fail)
	if (!HasScalarParam(BaseMat, Param_AlbedoWSA))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] Material missing optional scalar param %s"), *Param_AlbedoWSA.ToString());
	}
	if (!HasScalarParam(BaseMat, Param_AlbedoBSA))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] Material missing optional scalar param %s"), *Param_AlbedoBSA.ToString());
	}
	if (!HasScalarParam(BaseMat, Param_RoughnessBase))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] Material missing optional scalar param %s"), *Param_RoughnessBase.ToString());
	}
	if (!HasScalarParam(BaseMat, Param_SparkleIntensity))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] Material missing optional scalar param %s"), *Param_SparkleIntensity.ToString());
	}
	if (!HasScalarParam(BaseMat, Param_SparkleScale))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] Material missing optional scalar param %s"), *Param_SparkleScale.ToString());
	}
	if (!HasScalarParam(BaseMat, Param_SnowAgeDays))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] Material missing optional scalar param %s"), *Param_SnowAgeDays.ToString());
	}
	if (!HasScalarParam(BaseMat, Param_Grain_um))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] Material missing optional scalar param %s"), *Param_Grain_um.ToString());
	}
	if (!HasScalarParam(BaseMat, Param_Impurity_ppm))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] Material missing optional scalar param %s"), *Param_Impurity_ppm.ToString());
	}

	return bValidationPassed;
}

void ASnowSimulationActor::StepSimulation(float dtSeconds)
{
	// Accumulate simulated seconds towards weather step size
	SimulatedSecondsAccumulator += dtSeconds;

	if (SimulatedSecondsAccumulator + 1e-6f < TimeStepSeconds)
	{
		return;
	}

	int32 NumSteps = FMath::Max(1, FMath::FloorToInt(SimulatedSecondsAccumulator / TimeStepSeconds));
	SimulatedSecondsAccumulator -= NumSteps * TimeStepSeconds;

	static bool bLoggedWeatherUnits = false;

	for (int32 StepIdx = 0; StepIdx < NumSteps; ++StepIdx)
	{
		FWeatherForcingData WeatherForcing;
		if (WeatherProvider)
		{
			WeatherForcing = WeatherProvider->GetWeatherForcing(CurrentSimulationTime);
		}

		if (!bLoggedWeatherUnits)
		{
			UE_LOG(LogTemp, Display, TEXT("[Snow] Weather units: T(K)=%.1f, SWdown(W/m²)=%.0f, LWdown(W/m²)=%.0f, Wind(m/s)=%.1f, RH(0-1)=%.2f, PrecipRate(kg/m²/s)=%.6f, SnowFrac(0-1)=%.2f"),
			   WeatherForcing.Temperature_K, WeatherForcing.SWdown_Wm2, WeatherForcing.LWdown_Wm2,
			   WeatherForcing.Wind_mps, WeatherForcing.RH_01, WeatherForcing.PrecipRate_kgm2s, WeatherForcing.SnowFrac_01);
			bLoggedWeatherUnits = true;
		}

		if (Simulation)
		{
			// If the simulation derives from USnowSimulation, use its Step/Upload path
			if (USnowSimulation* SnowSim = Cast<USnowSimulation>(Simulation))
			{
				SnowSim->Step(TimeStepSeconds, WeatherForcing, SnowSim->DepthMeters);
				SnowSim->UploadDepthToTexture();
				
				// Sync CPU buffer with simulation data for HUD display
				UpdateCpuDepthMeters(SnowSim->DepthMeters);
				
				// Stats logging for visibility
				float MinV = FLT_MAX, MaxV = -FLT_MAX; double Sum = 0.0; int32 Count = SnowSim->DepthMeters.Num();
				for (int32 i = 0; i < Count; ++i) { float v = SnowSim->DepthMeters[i]; MinV = FMath::Min(MinV, v); MaxV = FMath::Max(MaxV, v); Sum += v; }
				float Mean = (Count > 0) ? static_cast<float>(Sum / Count) : 0.0f;
				UE_LOG(LogTemp, Display, TEXT("[Snow] DepthTex min=%.4f m, max=%.4f m, mean=%.4f m"), MinV, MaxV, Mean);
				
				// Log HUD stats for CPU buffer
				if (CpuDepthMeters.Num() > 0)
				{
					float CpuMinV = FLT_MAX, CpuMaxV = -FLT_MAX;
					for (int32 i = 0; i < CpuDepthMeters.Num(); ++i) 
					{ 
						const float v = CpuDepthMeters[i]; 
						CpuMinV = FMath::Min(CpuMinV, v); 
						CpuMaxV = FMath::Max(CpuMaxV, v); 
					}
					UE_LOG(LogTemp, Display, TEXT("[Snow][HUD] CPUbuf min/max(mm)=%.1f/%.1f, tex size=%dx%d"), 
						CpuMinV * 1000.0f, CpuMaxV * 1000.0f, CellsDimensionX, CellsDimensionY);
				}
			}
			else
			{
				// Fallback to legacy Simulate API
				Simulation->Simulate(this, this->CurrentSimulationStep, Timesteps, SaveMaterialTextures, DebugVisualizationType != EDebugVisualizationType::Nothing, DebugCells);
				if (Landscape)
				{
					SetScalarParameterValue(Landscape, Param_MaxSnow, Simulation->GetMaxSnow());
				}
			}
		}

		CurrentSimulationTime += FTimespan(0, 0, 0, 0, FMath::RoundToInt(TimeStepSeconds));
		this->CurrentSimulationStep += FMath::RoundToInt(TimeStepSeconds / 3600.0f);

		if (bLoopTime && CurrentSimulationTime >= SimulationEnd)
		{
			CurrentSimulationTime = SimulationStart;
			this->CurrentSimulationStep = 0;
			UE_LOG(LogTemp, Display, TEXT("[Snow] Time looped back to start"));
		}
	}

	// After steps, update material instance parameters using the current snow map texture
	UpdateMaterialTexture();
}

void ASnowSimulationActor::LogDepthStats()
{
	if (CpuDepthMeters.Num() == 0) return;
	float MinV = FLT_MAX;
	float MaxV = -FLT_MAX;
	double Sum = 0.0;
	int32 Count = CpuDepthMeters.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		float v = CpuDepthMeters[i];
		MinV = FMath::Min(MinV, v);
		MaxV = FMath::Max(MaxV, v);
		Sum += v;
	}
	float Mean = static_cast<float>(Sum / FMath::Max(1, Count));
	UE_LOG(LogTemp, Display, TEXT("[Snow] DepthTex min=%.4f m, max=%.4f m, mean=%.4f m"), MinV, MaxV, Mean);
}

void ASnowSimulationActor::UploadDepthToTexture()
{
	if (CpuDepthMeters.Num() != CellsDimensionX * CellsDimensionY)
	{
		return;
	}

	if (!SnowDepthTexture)
	{
		SnowDepthTexture = UTexture2D::CreateTransient(CellsDimensionX, CellsDimensionY, EPixelFormat::PF_R16F);
		SnowDepthTexture->SRGB = false;
		SnowDepthTexture->CompressionSettings = TC_HDR;
		SnowDepthTexture->LODGroup = TEXTUREGROUP_Pixels2D;
		SnowDepthTexture->AddressX = TA_Clamp;
		SnowDepthTexture->AddressY = TA_Clamp;
		SnowDepthTexture->Filter = TF_Bilinear;
		SnowDepthTexture->UpdateResource();
		
		// One-time log after texture creation
		UE_LOG(LogTemp, Display, TEXT("[Snow] Created SnowDepthTexture: PF=%s, sRGB=%s, size=%dx%d"), 
			*UEnum::GetValueAsString(SnowDepthTexture->GetPixelFormat()),
			SnowDepthTexture->SRGB ? TEXT("true") : TEXT("false"),
			SnowDepthTexture->GetSizeX(), SnowDepthTexture->GetSizeY());
	}

	UTexture2D* TextureToUpdate = SnowDepthTexture;
	// Upload with correct half-float stride using UpdateTextureRegions
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, CellsDimensionX, CellsDimensionY);
	const uint32 SrcBpp = sizeof(FFloat16);
	const uint32 SrcPitch = CellsDimensionX * sizeof(FFloat16);
	// Allocate heap buffer; copy half values
	const int32 Count = CellsDimensionX * CellsDimensionY;
	FFloat16* HalfData = new FFloat16[Count];
	for (int32 i = 0; i < Count; ++i)
	{
		HalfData[i] = CpuDepthMeters.IsValidIndex(i) ? CpuDepthMeters[i] : FFloat16(0.0f);
	}
	auto Cleanup = [](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
	{
		delete[] reinterpret_cast<FFloat16*>(SrcData);
		delete Regions;
	};
	TextureToUpdate->UpdateTextureRegions(0, 1, Region, SrcPitch, SrcBpp, reinterpret_cast<uint8*>(HalfData), Cleanup);

	// Bind for preview and log stats
	if (Landscape)
	{
		SetTextureParameterValue(Landscape, Param_SnowDepthTex, SnowDepthTexture, GEngine);
		UE_LOG(LogTemp, Display, TEXT("[Snow] Landscape SetParam SnowDepthTex=Texture"));
	}
	LogDepthStats();
}

void ASnowSimulationActor::DebugFillDepth(float MaxDepthMeters /*= 0.2f*/)
{
#if !UE_BUILD_SHIPPING
	if (!GIsEditor)
	{
		checkf(false, TEXT("Debug depth writer is active — disable before shipping."));
		return;
	}
#else
	checkf(false, TEXT("Debug depth writer is active — disable before shipping."));
	return;
#endif

	if (CellsDimensionX <= 0 || CellsDimensionY <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Snow] DebugFillDepth: invalid dimensions."));
		return;
	}

	if (USnowSimulation* SnowSim = Cast<USnowSimulation>(Simulation))
	{
		// Fill a linear X ramp into the sim's depth buffer and upload
		TArray<float>& H = SnowSim->DepthMeters;
		const int32 Wd = SnowSim->GridX;
		const int32 Hg = SnowSim->GridY;
		if (H.Num() == Wd * Hg && Wd > 1)
		{
			for (int32 y = 0; y < Hg; ++y)
			{
				for (int32 x = 0; x < Wd; ++x)
				{
					H[y * Wd + x] = (static_cast<float>(x) / static_cast<float>(Wd - 1)) * MaxDepthMeters;
				}
			}
			SnowSim->UploadDepthToTexture();
			UpdateMaterialTexture();
			UE_LOG(LogTemp, Display, TEXT("[Snow] DebugFillDepth max=%.3f m"), MaxDepthMeters);
		}
		return;
	}

	// Fallback: keep old local gradient path
	CpuDepthMeters.SetNum(CellsDimensionX * CellsDimensionY, EAllowShrinking::No);
	const float CenterX = (CellsDimensionX - 1) * 0.5f;
	const float CenterY = (CellsDimensionY - 1) * 0.5f;
	const float MaxRadius = FMath::Sqrt(CenterX * CenterX + CenterY * CenterY);
	for (int32 y = 0; y < CellsDimensionY; ++y)
	{
		for (int32 x = 0; x < CellsDimensionX; ++x)
		{
			float dx = (float)x - CenterX;
			float dy = (float)y - CenterY;
			float r = FMath::Sqrt(dx*dx + dy*dy);
			float t = FMath::Clamp(r / MaxRadius, 0.0f, 1.0f);
			CpuDepthMeters[y * CellsDimensionX + x] = t * MaxDepthMeters;
		}
	}
	UploadDepthToTexture();
	UpdateMaterialTexture();
}

void ASnowSimulationActor::PrintStatus()
{
	UE_LOG(LogTemp, Display, TEXT("=== SNOW SIMULATION STATUS ==="));
	
	// Provider/Simulation classes in use
	UE_LOG(LogTemp, Display, TEXT("Provider: %s"), 
		WeatherProvider ? *WeatherProvider->GetClass()->GetName() : TEXT("None"));
	UE_LOG(LogTemp, Display, TEXT("Simulation: %s"), 
		Simulation ? *Simulation->GetClass()->GetName() : TEXT("None"));
	
	// Grid information
	if (Landscape)
	{
		FVector LandscapeMin = Landscape->GetActorLocation();
		FVector LocalLandscapeScale = Landscape->GetActorScale();
		float OriginX_m = LandscapeMin.X / 100.0f;
		float OriginY_m = LandscapeMin.Y / 100.0f;
		float WidthMeters = LocalLandscapeScale.X * 100.0f;
		float HeightMeters = LocalLandscapeScale.Y * 100.0f;
		float CellSizeMeters = MetersPerCell;
		
		UE_LOG(LogTemp, Display, TEXT("Grid Origin: (%.3f, %.3f) m"), OriginX_m, OriginY_m);
		UE_LOG(LogTemp, Display, TEXT("Grid Size: %.3f x %.3f m"), WidthMeters, HeightMeters);
		UE_LOG(LogTemp, Display, TEXT("Cells: %d x %d"), CellsDimensionX, CellsDimensionY);
		UE_LOG(LogTemp, Display, TEXT("Cell Size: %.3f m"), CellSizeMeters);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("Grid: No landscape found"));
	}
	
	// Material parameter values
	if (VHMMaterialInstance)
	{
		FLinearColor OriginMeters, InvSizePerMeter;
		float DisplacementScale;
		
		VHMMaterialInstance->GetVectorParameterValue(Param_SnowOriginMeters, OriginMeters);
		VHMMaterialInstance->GetVectorParameterValue(Param_SnowInvSizePerMeter, InvSizePerMeter);
		VHMMaterialInstance->GetScalarParameterValue(Param_SnowDisplacementScale, DisplacementScale);
		
		UE_LOG(LogTemp, Display, TEXT("VHM Material Params:"));
		UE_LOG(LogTemp, Display, TEXT("  SnowOriginMeters: (%.6f, %.6f)"), OriginMeters.R, OriginMeters.G);
		UE_LOG(LogTemp, Display, TEXT("  SnowInvSizePerMeter: (%.8f, %.8f)"), InvSizePerMeter.R, InvSizePerMeter.G);
		UE_LOG(LogTemp, Display, TEXT("  SnowDisplacementScale: %.3f"), DisplacementScale);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("VHM Material: Not bound"));
	}
	
	if (SnowMID)
	{
		FLinearColor OriginMeters, InvSizePerMeter;
		float DisplacementScale;
		
		SnowMID->GetVectorParameterValue(Param_SnowOriginMeters, OriginMeters);
		SnowMID->GetVectorParameterValue(Param_SnowInvSizePerMeter, InvSizePerMeter);
		SnowMID->GetScalarParameterValue(Param_SnowDisplacementScale, DisplacementScale);
		
		UE_LOG(LogTemp, Display, TEXT("SnowMID Material Params:"));
		UE_LOG(LogTemp, Display, TEXT("  SnowOriginMeters: (%.6f, %.6f)"), OriginMeters.R, OriginMeters.G);
		UE_LOG(LogTemp, Display, TEXT("  SnowInvSizePerMeter: (%.8f, %.8f)"), InvSizePerMeter.R, InvSizePerMeter.G);
		UE_LOG(LogTemp, Display, TEXT("  SnowDisplacementScale: %.3f"), DisplacementScale);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("SnowMID Material: Not created"));
	}
	
	// Depth statistics (CPU-side if available)
	if (CpuDepthMeters.Num() > 0)
	{
		float MinV = FLT_MAX, MaxV = -FLT_MAX;
		double Sum = 0.0;
		int32 Count = CpuDepthMeters.Num();
		
		for (int32 i = 0; i < Count; ++i)
		{
			const float v = CpuDepthMeters[i];
			MinV = FMath::Min(MinV, v);
			MaxV = FMath::Max(MaxV, v);
			Sum += v;
		}
		
		float Mean = (Count > 0) ? static_cast<float>(Sum / Count) : 0.0f;
		UE_LOG(LogTemp, Display, TEXT("CPU Depth Stats: min=%.4f m, max=%.4f m, mean=%.4f m"), MinV, MaxV, Mean);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("CPU Depth Stats: No data available"));
	}
	
	// Simulation-specific depth stats
	if (USnowSimulation* SnowSim = Cast<USnowSimulation>(Simulation))
	{
		if (SnowSim->DepthMeters.Num() > 0)
		{
			float MinV = FLT_MAX, MaxV = -FLT_MAX;
			double Sum = 0.0;
			int32 Count = SnowSim->DepthMeters.Num();
			
			for (int32 i = 0; i < Count; ++i)
			{
				const float v = SnowSim->DepthMeters[i];
				MinV = FMath::Min(MinV, v);
				MaxV = FMath::Max(MaxV, v);
				Sum += v;
			}
			
			float Mean = (Count > 0) ? static_cast<float>(Sum / Count) : 0.0f;
			UE_LOG(LogTemp, Display, TEXT("Simulation Depth Stats: min=%.4f m, max=%.4f m, mean=%.4f m"), MinV, MaxV, Mean);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Simulation Depth Stats: No data available"));
		}
	}
	
	UE_LOG(LogTemp, Display, TEXT("=== END STATUS ==="));
}

void ASnowSimulationActor::UpdateCpuDepthMeters(const TArray<float>& InDepthMeters)
{
	if (InDepthMeters.Num() != CellsDimensionX * CellsDimensionY)
	{
		// Resize depth buffer if needed and clamp to expected size
		CpuDepthMeters.SetNum(CellsDimensionX * CellsDimensionY, EAllowShrinking::No);
	}

	const int32 Count = FMath::Min(CpuDepthMeters.Num(), InDepthMeters.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		CpuDepthMeters[i] = InDepthMeters[i];
	}

	UploadDepthToTexture();
}

USimulationBase* ASnowSimulationActor::ResolveSimulation()
{
    // If inline mode is enabled, attempt to instantiate from InlineSimulationClass
    if (UseInlineSimulation && InlineSimulationClass)
    {
        if (InlineSimulationClass->IsChildOf(USimulationBase::StaticClass()) && InlineSimulationClass != USimulationBase::StaticClass())
        {
            USimulationBase* NewSim = NewObject<USimulationBase>(this, InlineSimulationClass);
            if (NewSim)
            {
                UE_LOG(LogTemp, Display, TEXT("[Snow] Instantiated simulation from inline class: %s"), *InlineSimulationClass->GetName());
                return NewSim;
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[Snow] InlineSimulationClass must be a concrete subclass of USimulationBase. Falling back."));
        }
    }

    // Fallback to a concrete Simple Accumulation simulation
    USimpleAccumulationSim* FallbackSim = NewObject<USimpleAccumulationSim>(this);
    UE_LOG(LogTemp, Display, TEXT("[Snow] Using fallback simulation: %s"), *GetNameSafe(FallbackSim));
    return FallbackSim;
}


