#include "HotReleaseCommandlet.h"
#include "CreatePatch/FExportReleaseSettings.h"
#include "CreatePatch/ReleaseProxy.h"
#include "CommandletHelper.h"

// engine header
#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogHotReleaseCommandlet);

int32 UHotReleaseCommandlet::Main(const FString& Params)
{
	Super::Main(Params);
	UE_LOG(LogHotReleaseCommandlet, Display, TEXT("UHotReleaseCommandlet::Main:%s"), *Params);

	FString config_path;
	bool bStatus = FParse::Value(*Params, *FString(PATCHER_CONFIG_PARAM_NAME).ToLower(), config_path);
	if (!bStatus)
	{
		UE_LOG(LogHotReleaseCommandlet, Warning, TEXT("not -config=xxxx.json params."));
		// return -1;
	}

	if (bStatus && !FPaths::FileExists(config_path))
	{
		UE_LOG(LogHotReleaseCommandlet, Error, TEXT("config file %s not exists."), *config_path);
		return -1;
	}
	if(IsRunningCommandlet())
	{
		// load asset registry
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.Get().SearchAllAssets(true);
	}

	TSharedPtr<FExportReleaseSettings> ExportReleaseSetting = MakeShareable(new FExportReleaseSettings);
	
	FString JsonContent;
	if (FPaths::FileExists(config_path) && FFileHelper::LoadFileToString(JsonContent, *config_path))
	{
		THotPatcherTemplateHelper::TDeserializeJsonStringAsStruct(JsonContent,*ExportReleaseSetting);
	}

	TMap<FString, FString> KeyValues = THotPatcherTemplateHelper::GetCommandLineParamsMap(Params);
	THotPatcherTemplateHelper::ReplaceProperty(*ExportReleaseSetting, KeyValues);

	// 从命令行分析PlatformPakList
	TArray<FPlatformPakListFiles> ReadPakList = ParserPlatformPakList(Params);
	if(ReadPakList.Num())
	{
		ExportReleaseSetting->PlatformsPakListFiles = ReadPakList;
	}

	if(ReadPakList.Num() && ExportReleaseSetting->IsBackupMetadata())
	{
		for(const auto& PlatformPakList:ReadPakList)
		{
			ExportReleaseSetting->BackupMetadataPlatforms.AddUnique(PlatformPakList.TargetPlatform);
        }
	}
	
	FString FinalConfig;
	THotPatcherTemplateHelper::TSerializeStructAsJsonString(*ExportReleaseSetting,FinalConfig);
	UE_LOG(LogHotReleaseCommandlet, Display, TEXT("%s"), *FinalConfig);
		
	UReleaseProxy* ReleaseProxy = NewObject<UReleaseProxy>();
	ReleaseProxy->AddToRoot();
	ReleaseProxy->Init(ExportReleaseSetting.Get());
	ReleaseProxy->OnPaking.AddStatic(&::CommandletHelper::ReceiveMsg);
	ReleaseProxy->OnShowMsg.AddStatic(&::CommandletHelper::ReceiveShowMsg);
	bool bExportStatus = ReleaseProxy->DoExport();
		
	UE_LOG(LogHotReleaseCommandlet,Display,TEXT("Export Release Misstion is %s!"),bExportStatus?TEXT("Successed"):TEXT("Failure"));
	
	if(FParse::Param(FCommandLine::Get(), TEXT("wait")))
	{
		system("pause");
	}
	
	return (int32)!bExportStatus;
}
