#pragma once
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "SnowSimulation.h"
#include "DegreeDaySimulation.generated.h"


/**
* Snow simulation similar to the one proposed by Simon Premoze in "Geospecific rendering of alpine terrain".
* Snow deposition is implemented similar to Fearings "Computer Modelling Of Fallen Snow".
*/
UCLASS(Blueprintable, BlueprintType)
class SIMULATION_API UDegreeDaySimulation : public USnowSimulation
{
	GENERATED_BODY()

public:
	// Ensure derived degree-day sims get grid/texture setup via base
	virtual void Initialize_Implementation(int32 GX, int32 GY, float CellM) override
	{
		Super::Initialize_Implementation(GX, GY, CellM);
	}
	/** Slope threshold for the snow deposition of the cells in degrees.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	float SlopeThreshold = 45;

	/** Threshold A air temperature above which some precipitation is assumed to be rain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", DisplayName = "TSnow A")
	float TSnowA = 0;

	/** Threshold B air temperature above which all precipitation is assumed to be rain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", DisplayName = "TSnow B")
	float TSnowB = 2;

	/** Threshold A air temperature above which some snow starts melting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", DisplayName = "TMelt A")
	float TMeltA = -5;

	/** Threshold B air temperature above which all snow starts melting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", DisplayName = "TMelt B")
	float TMeltB = -2;

	/** Time constant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", DisplayName = "k_e")
	float k_e = 0.2;

	/** Proportional constant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", DisplayName = "k_m")
	float k_m = 4;

	// Per-step accumulation + simple degree-day melt on OutDepthMeters (meters)
	virtual void Step(float DtSeconds, const FWeatherForcingData& W, TArray<float>& OutDepthMeters) override
	{
		if (OutDepthMeters.Num() == 0 || DtSeconds <= 0.0f)
		{
			return;
		}

		// 1) Accumulation from precipitation (kg/m^2/s → m/s via density)
		const float precip_kg_m2_s = FMath::Max(0.0f, W.PrecipRate_kgm2s);
		const float snowfrac = FMath::Clamp(W.SnowFrac_01, 0.0f, 1.0f);
		// Convert water-equivalent mass flux to snow depth using configurable density
		const float rho_snow = (FreshSnowDensity_kgm3 > 1.0f) ? FreshSnowDensity_kgm3 : 100.0f; // kg/m^3
		float dH_acc = (rho_snow > 1e-6f) ? (precip_kg_m2_s * snowfrac / rho_snow) * DtSeconds : 0.0f;
		
		// 2) Simple degree-day melt when air temperature > 0°C
		const float Tair_C = W.Temperature_K - 273.15f;
		float melt_m = 0.0f;
		if (Tair_C > 0.0f)
		{
			const float DDF_m_per_C_day = 0.004f; // m/°C/day (tunable)
			melt_m = DDF_m_per_C_day * Tair_C * (DtSeconds / 86400.0f);
		}

		// Convert precipitation water equivalent during the step to mm for logging (1 kg/m² = 1 mm)
		const float PrecipWE_mm = precip_kg_m2_s * DtSeconds; // kg/m²/s * s = kg/m² = mm
		const float DeltaSnow_mm = (dH_acc - melt_m) * 1000.0f; // meters to mm

		// Log each simulation step using depth units
		UE_LOG(LogTemp, Verbose, TEXT("[Snow] t=%s, dts=%.0f, precipWE=%.2f mm, SnowFrac=%.2f -> dS=%.2f mm"),
			*W.Timestamp.ToString(), DtSeconds, PrecipWE_mm, snowfrac, DeltaSnow_mm);

		if (dH_acc > 0.0f)
		{
			// 3) Terrain redistribution (Blöschl-inspired): reduce on steep slopes, increase with curvature
			if (bHasTerrainMetadata && TerrainSlopeDegrees.Num() == OutDepthMeters.Num() && TerrainCurvature.Num() == OutDepthMeters.Num())
			{
				// Compute per-cell factor: (1 - f(slope)) * (1 + a3 * curvature)
				// Using f = 0 for slope<15°, else slope/65 as in CPU sim; a3=50
				constexpr float SlopeThresholdDeg = 15.0f;
				constexpr float SlopeScale = 65.0f;
				constexpr float A3 = 50.0f;
				for (int32 i = 0; i < OutDepthMeters.Num(); ++i)
				{
					const float slopeDeg = TerrainSlopeDegrees[i];
					const float f = (slopeDeg < SlopeThresholdDeg) ? 0.0f : (slopeDeg / SlopeScale);
					const float factor = FMath::Max(0.0f, (1.0f - f) * (1.0f + A3 * TerrainCurvature[i]));
					OutDepthMeters[i] += dH_acc * factor;
				}
			}
			else
			{
				for (float& h : OutDepthMeters) { h += dH_acc; }
			}
		}

		if (melt_m > 0.0f)
		{
			for (float& h : OutDepthMeters) { h = FMath::Max(0.0f, h - melt_m); }
		}
	}

};

