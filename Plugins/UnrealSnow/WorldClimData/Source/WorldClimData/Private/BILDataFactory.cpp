#include "BILDataFactory.h"
#include "BILData.h"

UBILDataFactory::UBILDataFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UBILData::StaticClass();

	bCreateNew = false;
	bEditorImport = true;

	Formats.Add("bil;Binary Interleaved by Line");
}

UObject* UBILDataFactory::FactoryCreateBinary(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn)
{
	UBILData* BILData = NewObject<UBILData>(InParent, SupportedClass, Name, Flags | RF_Transactional);

	SIZE_T DataSize = BufferEnd - Buffer;

	BILData->Data.Reset(DataSize / 2); // Divide by 2 because 16 to 8 bit
	BILData->Data.AddUninitialized(DataSize / 2);
	FMemory::Memcpy(BILData->Data.GetData(), Buffer, DataSize);

	// Set up asset import data for reimport support
	BILData->AssetImportData = NewObject<UAssetImportData>(BILData);
	if (Context && Context->GetOutermost()->IsA<UPackage>())
	{
		FString AssetFilename = Context->GetOutermost()->GetName();
		BILData->AssetImportData->Update(AssetFilename);
	}

	return BILData;
}

bool UBILDataFactory::FactoryCanImport(const FString& Filename)
{
	return true;
}

bool UBILDataFactory::ConfigureProperties()
{
	return true;
}
