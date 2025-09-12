#include "CsvWeatherProvider.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Internationalization/Regex.h"

UCsvWeatherProvider::UCsvWeatherProvider()
{
}

void UCsvWeatherProvider::Initialize(FDateTime StartTime, FDateTime EndTime)
{
	WeatherRecords.Empty();

	if (!LoadCsvData())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Weather] Failed to load CSV data from %s"), *CsvFilePath.FilePath);
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("[Weather] CSV provider initialized with %d records from %s"),
		   WeatherRecords.Num(), *CsvFilePath.FilePath);

	// Log statistics
	if (WeatherRecords.Num() > 0)
	{
		float MinTemp = FLT_MAX, MaxTemp = -FLT_MAX;
		float MinPrecip = FLT_MAX, MaxPrecip = -FLT_MAX;
		float MinSW = FLT_MAX, MaxSW = -FLT_MAX;

		for (const auto& Record : WeatherRecords)
		{
			MinTemp = FMath::Min(MinTemp, Record.Temperature_K);
			MaxTemp = FMath::Max(MaxTemp, Record.Temperature_K);
			MinPrecip = FMath::Min(MinPrecip, Record.PrecipRate_kgm2s);
			MaxPrecip = FMath::Max(MaxPrecip, Record.PrecipRate_kgm2s);
			MinSW = FMath::Min(MinSW, Record.SWdown_Wm2);
			MaxSW = FMath::Max(MaxSW, Record.SWdown_Wm2);
		}

		float MeanTemp = (MinTemp + MaxTemp) / 2.0f;
		float MeanPrecip = (MinPrecip + MaxPrecip) / 2.0f;
		float MeanSW = (MinSW + MaxSW) / 2.0f;

		UE_LOG(LogTemp, Display, TEXT("[Weather] Stats: T=%.1f-%.1f°C (mean %.1f°C), Precip=%.3f-%.3f kg/m²/s (mean %.3f), SW=%.0f-%.0f W/m² (mean %.0f)"),
			   MinTemp - 273.15f, MaxTemp - 273.15f, MeanTemp - 273.15f,
			   MinPrecip, MaxPrecip, MeanPrecip,
			   MinSW, MaxSW, MeanSW);
	}
}

bool UCsvWeatherProvider::LoadCsvData()
{
	if (CsvFilePath.FilePath.IsEmpty())
	{
		return false;
	}

	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *CsvFilePath.FilePath))
	{
		return false;
	}

	TArray<FString> Lines;
	FileContent.ParseIntoArray(Lines, TEXT("\n"), true);

	if (Lines.Num() < 2) // Need at least header + one data line
	{
		return false;
	}

	// Skip header line
	for (int32 i = 1; i < Lines.Num(); ++i)
	{
		FWeatherForcingData Record;
		if (ParseCsvLine(Lines[i], Record))
		{
			WeatherRecords.Add(Record);
		}
	}

	// Sort by timestamp
	WeatherRecords.Sort([](const FWeatherForcingData& A, const FWeatherForcingData& B) {
		return A.Timestamp < B.Timestamp;
	});

	return WeatherRecords.Num() > 0;
}

bool UCsvWeatherProvider::ParseCsvLine(const FString& Line, FWeatherForcingData& OutData)
{
	if (Line.IsEmpty())
	{
		return false;
	}

	TArray<FString> Columns;
	Line.ParseIntoArray(Columns, TEXT(","), true);

	if (Columns.Num() < 8) // Minimum required columns
	{
		return false;
	}

	// Parse timestamp
	FDateTime Timestamp;
	if (!FDateTime::ParseIso8601(*Columns[0], Timestamp))
	{
		return false;
	}

	// Parse weather data
	float TempC = FCString::Atof(*Columns[1]);
	float RH_pct = FCString::Atof(*Columns[2]);
	float Wind_mps = FCString::Atof(*Columns[3]);
	float SWdown_Wm2 = FCString::Atof(*Columns[4]);
	float LWdown_Wm2 = FCString::Atof(*Columns[5]);
	float Precip_mmph = FCString::Atof(*Columns[6]);
	float SnowFrac = FCString::Atof(*Columns[7]);

	// Convert units
	float TempK = TempC + 273.15f;
	float RH_01 = FMath::Clamp(RH_pct / 100.0f, 0.0f, 1.0f);
	float Precip_kgm2s = Precip_mmph * 1000.0f / 3600.0f;

	OutData = FWeatherForcingData(Timestamp, TempK, SWdown_Wm2, LWdown_Wm2, Wind_mps, RH_01, Precip_kgm2s, SnowFrac);
	return true;
}

TResourceArray<FClimateData>* UCsvWeatherProvider::CreateRawClimateDataResourceArray(FDateTime StartTime, FDateTime EndTime)
{
	auto* ResourceArray = new TResourceArray<FClimateData>();

	// Create dummy climate data for compatibility with existing system
	if (WeatherRecords.Num() > 0)
	{
		for (const auto& Record : WeatherRecords)
		{
			float TempC = Record.Temperature_K - 273.15f;
			float Precip_m = Record.PrecipRate_kgm2s * 3600.0f / 1000.0f; // Convert back to m/h
			ResourceArray->Add(FClimateData(Precip_m, TempC));
		}
	}

	return ResourceArray;
}

FWeatherForcingData UCsvWeatherProvider::GetWeatherForcing(FDateTime Time, int32 GridX, int32 GridY)
{
	if (WeatherRecords.Num() == 0)
	{
		return FWeatherForcingData();
	}

	if (WeatherRecords.Num() == 1)
	{
		return WeatherRecords[0];
	}

	// Find bracketing records
	int32 Index1, Index2;
	float Alpha;
	FindBracketingRecords(Time, Index1, Index2, Alpha);

	if (Index1 == INDEX_NONE || Index2 == INDEX_NONE)
	{
		// Extrapolate using the closest record
		int32 ClosestIndex = (Time < WeatherRecords[0].Timestamp) ? 0 : WeatherRecords.Num() - 1;
		return WeatherRecords[ClosestIndex];
	}

	// Interpolate between bracketing records
	return InterpolateRecords(WeatherRecords[Index1], WeatherRecords[Index2], Alpha);
}

void UCsvWeatherProvider::FindBracketingRecords(FDateTime Time, int32& OutIndex1, int32& OutIndex2, float& OutAlpha)
{
	OutIndex1 = INDEX_NONE;
	OutIndex2 = INDEX_NONE;
	OutAlpha = 0.0f;

	if (WeatherRecords.Num() < 2)
	{
		return;
	}

	// Binary search for the right insertion point
	int32 Left = 0;
	int32 Right = WeatherRecords.Num() - 1;

	while (Left <= Right)
	{
		int32 Mid = Left + (Right - Left) / 2;

		if (WeatherRecords[Mid].Timestamp < Time)
		{
			Left = Mid + 1;
		}
		else if (WeatherRecords[Mid].Timestamp > Time)
		{
			Right = Mid - 1;
		}
		else
		{
			// Exact match
			OutIndex1 = Mid;
			OutIndex2 = Mid;
			OutAlpha = 0.0f;
			return;
		}
	}

	// Left is now the insertion point
	if (Left == 0)
	{
		// Before first record
		OutIndex1 = 0;
		OutIndex2 = 0;
		OutAlpha = 0.0f;
	}
	else if (Left >= WeatherRecords.Num())
	{
		// After last record
		OutIndex1 = WeatherRecords.Num() - 1;
		OutIndex2 = WeatherRecords.Num() - 1;
		OutAlpha = 0.0f;
	}
	else
	{
		// Between two records
		OutIndex1 = Left - 1;
		OutIndex2 = Left;

		FTimespan TimeSpan = WeatherRecords[OutIndex2].Timestamp - WeatherRecords[OutIndex1].Timestamp;
		FTimespan TargetSpan = Time - WeatherRecords[OutIndex1].Timestamp;

		if (TimeSpan.GetTotalSeconds() > 0)
		{
			OutAlpha = TargetSpan.GetTotalSeconds() / TimeSpan.GetTotalSeconds();
		}
		else
		{
			OutAlpha = 0.0f;
		}
	}
}

FWeatherForcingData UCsvWeatherProvider::InterpolateRecords(const FWeatherForcingData& Record1, const FWeatherForcingData& Record2, float Alpha)
{
	return FWeatherForcingData(
		Record1.Timestamp + (Record2.Timestamp - Record1.Timestamp) * Alpha,
		FMath::Lerp(Record1.Temperature_K, Record2.Temperature_K, Alpha),
		FMath::Lerp(Record1.SWdown_Wm2, Record2.SWdown_Wm2, Alpha),
		FMath::Lerp(Record1.LWdown_Wm2, Record2.LWdown_Wm2, Alpha),
		FMath::Lerp(Record1.Wind_mps, Record2.Wind_mps, Alpha),
		FMath::Lerp(Record1.RH_01, Record2.RH_01, Alpha),
		FMath::Lerp(Record1.PrecipRate_kgm2s, Record2.PrecipRate_kgm2s, Alpha),
		FMath::Lerp(Record1.SnowFrac_01, Record2.SnowFrac_01, Alpha)
	);
}
