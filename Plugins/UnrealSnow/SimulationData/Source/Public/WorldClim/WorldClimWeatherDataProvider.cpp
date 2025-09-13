#include "WorldClimWeatherDataProvider.h"
#include "SimulationData.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"

TResourceArray<FClimateData>* UWorldClimWeatherDataProvider::CreateRawClimateDataResourceArray(FDateTime StartTime, FDateTime EndTime)
{
	// Optional: produce a simple climate array from HourlySeries for legacy consumers
	if (HourlySeries.Num() == 0)
	{
		return nullptr;
	}
	TResourceArray<FClimateData>* Arr = new TResourceArray<FClimateData>();
	Arr->Reserve(HourlySeries.Num());
	for (const auto& F : HourlySeries)
	{
		float TempC = F.Temperature_K - 273.15f;
		float Precip_m_per_h = F.PrecipRate_kgm2s * 3600.0f / 1000.0f; // convert back for legacy
		Arr->Add(FClimateData(Precip_m_per_h, TempC));
	}
	return Arr;
}

void UWorldClimWeatherDataProvider::Initialize(FDateTime StartTime, FDateTime EndTime)
{
	HourlySeries.Reset();
	SeriesStart = StartTime;
	SeriesHours = static_cast<int32>((EndTime - StartTime).GetTotalHours());
	bUseCsv = false;

	// If CSV override is set, load it and use directly
	if (!CsvFilePath.FilePath.IsEmpty() && FPaths::FileExists(CsvFilePath.FilePath))
	{
		if (LoadCsvOverride())
		{
			bUseCsv = true;
			UE_LOG(LogTemp, Display, TEXT("[Weather] WorldClim using CSV override: %s (%d records)"), *CsvFilePath.FilePath, HourlySeries.Num());
			return;
		}
		UE_LOG(LogTemp, Warning, TEXT("[Weather] WorldClim CSV override failed to load: %s"), *CsvFilePath.FilePath);
	}

	// Otherwise, prepare to sample monthly data assets → hourly forcing
	if (MonthlyData.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Weather] WorldClim MonthlyData is empty; provider will return defaults."));
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("[Weather] WorldClim provider initialized with %d monthly assets"), MonthlyData.Num());
}

bool UWorldClimWeatherDataProvider::LoadCsvOverride()
{
	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *CsvFilePath.FilePath))
	{
		return false;
	}
	TArray<FString> Lines;
	FileContent.ParseIntoArray(Lines, TEXT("\n"), true);
	if (Lines.Num() < 2)
	{
		return false;
	}
	// Parse records (reuse CsvWeatherProvider format)
	HourlySeries.Reset();
	for (int32 i = 1; i < Lines.Num(); ++i)
	{
		TArray<FString> Columns;
		Lines[i].ParseIntoArray(Columns, TEXT(","), true);
		if (Columns.Num() < 8) { continue; }
		FDateTime Timestamp;
		if (!FDateTime::ParseIso8601(*Columns[0], Timestamp)) { continue; }
		float TempC = FCString::Atof(*Columns[1]);
		float RH_pct = FCString::Atof(*Columns[2]);
		float Wind_mps = FCString::Atof(*Columns[3]);
		float SWdown_Wm2 = FCString::Atof(*Columns[4]);
		float LWdown_Wm2 = FCString::Atof(*Columns[5]);
		float Precip_mmph = FCString::Atof(*Columns[6]);
		float SnowFrac = FCString::Atof(*Columns[7]);
		float TempK = TempC + 273.15f;
		float RH_01 = FMath::Clamp(RH_pct / 100.0f, 0.0f, 1.0f);
		float Precip_kgm2s = Precip_mmph / 3600.0f; // 1 mm == 1 kg/m²
		HourlySeries.Add(FWeatherForcingData(Timestamp, TempK, SWdown_Wm2, LWdown_Wm2, Wind_mps, RH_01, Precip_kgm2s, SnowFrac));
	}
	HourlySeries.Sort([](const FWeatherForcingData& A, const FWeatherForcingData& B){ return A.Timestamp < B.Timestamp; });
	SeriesStart = (HourlySeries.Num() > 0) ? HourlySeries[0].Timestamp : SeriesStart;
	SeriesHours = HourlySeries.Num();
	return HourlySeries.Num() > 0;
}

FWeatherForcingData UWorldClimWeatherDataProvider::SampleMonthlyToHourly(FDateTime Time) const
{
	// Minimal viable downscaler: linear interpolate between monthly means, assume hourly const within month
	// MonthlyData should have 12 assets; we use MeanTemperature (°C) and Precipitation (mm/month)
	if (MonthlyData.Num() < 12)
	{
		return FWeatherForcingData();
	}
	int32 Month = Time.GetMonth(); // 1..12
	int32 NextMonth = (Month % 12) + 1;
	float Alpha = (Time.GetDay() - 1) / 30.0f; // coarse within-month position

	auto GetAt = [&](int32 M, float& OutTempC, float& OutPrecip_mm_per_month)
	{
		int32 Index = FMath::Clamp(M - 1, 0, MonthlyData.Num() - 1);
		UMonthlyWorldClimDataAsset* A = MonthlyData[Index];
		if (!A || !A->MeanTemperature || !A->Precpipitation)
		{
			OutTempC = 0.0f; OutPrecip_mm_per_month = 0.0f; return;
		}
		// Sample nearest grid cell by lat/long
		int16 TempC10 = A->MeanTemperature->GetDataAt(SampleLatitude, SampleLongitude); // tenths °C?
		int16 Prec10 = A->Precpipitation->GetDataAt(SampleLatitude, SampleLongitude);   // mm?
		OutTempC = static_cast<float>(TempC10) / 10.0f;
		OutPrecip_mm_per_month = static_cast<float>(Prec10);
	};

	float T1=0, P1=0, T2=0, P2=0;
	GetAt(Month, T1, P1);
	GetAt(NextMonth, T2, P2);
	float TempC = FMath::Lerp(T1, T2, Alpha);
	float Precip_mm_per_month = FMath::Lerp(P1, P2, Alpha);
	// Distribute monthly precip to hourly uniformly (simple baseline)
	float HoursInMonth = 24.0f * 30.0f; // coarse
	float Precip_mmph = Precip_mm_per_month / HoursInMonth;

	float TempK = TempC + 273.15f;
	float Precip_kgm2s = Precip_mmph / 3600.0f;
	float RH_01 = 0.6f;
	float Wind_mps = 2.0f;
	float SWdown_Wm2 = 230.0f;
	float LWdown_Wm2 = 210.0f;
	float SnowFrac = bUseSimpleSnowFrac ? (TempC <= 0.0f ? 1.0f : 0.0f) : 0.0f;
	return FWeatherForcingData(Time, TempK, SWdown_Wm2, LWdown_Wm2, Wind_mps, RH_01, Precip_kgm2s, SnowFrac);
}

FWeatherForcingData UWorldClimWeatherDataProvider::GetWeatherForcing(FDateTime Time, int32 GridX, int32 GridY)
{
	if (bUseCsv && HourlySeries.Num() > 0)
	{
		// Use closest record
		// Binary search
		int32 L=0, R=HourlySeries.Num()-1, Best=0;
		while (L<=R)
		{
			int32 M = (L+R)/2;
			if (HourlySeries[M].Timestamp < Time) { Best=M; L=M+1; }
			else { R=M-1; }
		}
		return HourlySeries[FMath::Clamp(Best, 0, HourlySeries.Num()-1)];
	}
	return SampleMonthlyToHourly(Time);
}
