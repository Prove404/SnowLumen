// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "WorldClimDataModule.h"
#include "HDRDataAssetTypeActions.h"
#include "BILDataAssetTypeActions.h"
#include "AssetToolsModule.h"

class FWorldClimDataModule : public IModuleInterface
{
private:
	/** The collection of registered asset type actions. */
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;

public:
	virtual void StartupModule() override
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		TSharedRef<IAssetTypeActions> BILData = MakeShareable(new FBILDataAssetTypeActions);
		TSharedRef<IAssetTypeActions> HDRData = MakeShareable(new FHDRDataAssetTypeActions);

		AssetTools.RegisterAssetTypeActions(BILData);
		RegisteredAssetTypeActions.Add(BILData);

		AssetTools.RegisterAssetTypeActions(HDRData);
		RegisteredAssetTypeActions.Add(HDRData);
	}

	virtual void ShutdownModule() override
	{
		FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");

		if (AssetToolsModule != nullptr)
		{
			IAssetTools& AssetTools = AssetToolsModule->Get();

			for (auto Action : RegisteredAssetTypeActions)
			{
				AssetTools.UnregisterAssetTypeActions(Action);
			}
		}
	}
};

IMPLEMENT_MODULE(FWorldClimDataModule, WorldClimData)
