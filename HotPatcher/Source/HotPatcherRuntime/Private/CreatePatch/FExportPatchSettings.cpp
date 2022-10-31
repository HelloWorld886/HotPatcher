#include "CreatePatch/FExportPatchSettings.h"
#include "FlibAssetManageHelper.h"
#include "HotPatcherLog.h"
#include "FlibPatchParserHelper.h"
#include "ReleaseParser/FReleasePaklistParser.h"
#include "ReleaseParser/FReleasePakParser.h"
#include "ReleaseParser/IReleaseParser.h"


FExportPatchSettings::FExportPatchSettings()
	:bEnableExternFilesDiff(true),
	DefaultPakListOptions{ TEXT("-compress") },
	DefaultCommandletOptions{ TEXT("-compress") ,TEXT("-compressionformats=Zlib")}
{
	// IoStoreSettings.IoStoreCommandletOptions.Add(TEXT("-CreateGlobalContainer="));
	PakVersionFileMountPoint = FPaths::Combine(
		TEXT("../../../"),
		UFlibPatchParserHelper::GetProjectName(),
		TEXT("Versions/version.json")
	);
}

void FExportPatchSettings::Init()
{
	Super::Init();
}

FString FExportPatchSettings::GetBaseVersion() const
{
	return UFlibPatchParserHelper::ReplaceMarkPath(BaseVersion.FilePath); 
}

FPakVersion FExportPatchSettings::GetPakVersion(const FHotPatcherVersion& InHotPatcherVersion, const FString& InUtcTime)
{
	FPakVersion PakVersion;
	PakVersion.BaseVersionId = InHotPatcherVersion.BaseVersionId;
	PakVersion.VersionId = InHotPatcherVersion.VersionId;
	PakVersion.Date = InUtcTime;

	// encode BaseVersionId_VersionId_Data to SHA1
	PakVersion.CheckCode = UFlibPatchParserHelper::HashStringWithSHA1(
		FString::Printf(
			TEXT("%s_%s_%s"),
			*PakVersion.BaseVersionId,
			*PakVersion.VersionId,
			*PakVersion.Date
		)
	);

	return PakVersion;
}

FString FExportPatchSettings::GetSavePakVersionPath(const FString& InSaveAbsPath, const FHotPatcherVersion& InVersion)
{
	FString PatchSavePath(InSaveAbsPath);
	FPaths::MakeStandardFilename(PatchSavePath);

	FString SavePakVersionFilePath = FPaths::Combine(
		PatchSavePath,
		FString::Printf(
			TEXT("%s_PakVersion.json"),
			*InVersion.VersionId
		)
	);
	return SavePakVersionFilePath;
}

FString FExportPatchSettings::GetPakCommandsSaveToPath(const FString& InSaveAbsPath,const FString& InPlatfornName, const FHotPatcherVersion& InVersion)
{
	FString SavePakCommandPath = FPaths::Combine(
		InSaveAbsPath,
		InPlatfornName,
		!InVersion.BaseVersionId.IsEmpty()?
		FString::Printf(TEXT("PakList_%s_%s_%s_PakCommands.txt"), *InVersion.BaseVersionId, *InVersion.VersionId, *InPlatfornName):
		FString::Printf(TEXT("PakList_%s_%s_PakCommands.txt"), *InVersion.VersionId, *InPlatfornName)	
	);

	return SavePakCommandPath;
}

FHotPatcherVersion FExportPatchSettings::GetNewPatchVersionInfo()
{
	FHotPatcherVersion BaseVersionInfo;
	GetBaseVersionInfo(BaseVersionInfo);

	FHotPatcherVersion CurrentVersion = UFlibPatchParserHelper::ExportReleaseVersionInfo(
        GetVersionId(),
        BaseVersionInfo.VersionId,
        FDateTime::UtcNow().ToString(),
        UFlibAssetManageHelper::DirectoryPathsToStrings(GetAssetIncludeFilters()),
			 UFlibAssetManageHelper::DirectoryPathsToStrings(GetAssetIgnoreFilters()),
        GetAllSkipContents(),
        GetForceSkipClasses(),
        GetAssetRegistryDependencyTypes(),
        GetIncludeSpecifyAssets(),
        GetAddExternAssetsToPlatform(),
        IsIncludeHasRefAssetsOnly()
    );

	return CurrentVersion;
}

bool FExportPatchSettings::GetBaseVersionInfo(FHotPatcherVersion& OutBaseVersion) const
{
	FString BaseVersionContent;

	bool bDeserializeStatus = false;
	if (IsByBaseVersion())
	{
		if (UFlibAssetManageHelper::LoadFileToString(GetBaseVersion(), BaseVersionContent))
		{
			bDeserializeStatus = THotPatcherTemplateHelper::TDeserializeJsonStringAsStruct(BaseVersionContent, OutBaseVersion);
		}
	}

	return bDeserializeStatus;
}


FString FExportPatchSettings::GetCurrentVersionSavePath() const
{
	FString CurrentVersionSavePath = FPaths::Combine(GetSaveAbsPath(), /*const_cast<FExportPatchSettings*>(this)->GetNewPatchVersionInfo().*/VersionId);
	return CurrentVersionSavePath;
}

TArray<FString> FExportPatchSettings::GetPakTargetPlatformNames() const
{
	TArray<FString> Resault;
	for (const auto &Platform : GetPakTargetPlatforms())
	{
		Resault.Add(THotPatcherTemplateHelper::GetEnumNameByValue(Platform));
	}
	return Resault;
}

void FExportPatchSettings::ImportPakLists()
{
	UE_LOG(LogHotPatcher,Log,TEXT("FExportReleaseSettings::ImportPakList"));
	
	if(!GetPlatformsPakListFiles().Num())
	{
		return;
	}
	
	TArray<FReleaseParserResult> PlatformAssets;
	for(const auto& PlatformPakList:GetPlatformsPakListFiles())
	{
		TSharedPtr<FReleasePakListConf> PakListConf = MakeShareable(new FReleasePakListConf);
		PakListConf->TargetPlatform = PlatformPakList.TargetPlatform;
		for(const auto& PakFile:PlatformPakList.PakResponseFiles)
		{
			PakListConf->PakResponseFiles.AddUnique(PakFile.FilePath);
		}
		if(!!PakListConf->PakResponseFiles.Num())
		{
			FReleasePaklistParser PakListParser;
			PakListParser.Parser(PakListConf);
			PlatformAssets.Add(PakListParser.GetParserResult());
		}

		TSharedPtr<FReleasePakFilesConf> PakFileConf = MakeShareable(new FReleasePakFilesConf);
		PakFileConf->TargetPlatform = PlatformPakList.TargetPlatform;
		for(const auto& PakFile:PlatformPakList.PakFiles)
		{
			PakFileConf->PakFiles.AddUnique(FPaths::ConvertRelativePathToFull(PakFile.FilePath));
		}
		if(!!PakFileConf->PakFiles.Num())
		{
			FReleasePakParser PakFileParser;
			PakFileParser.Parser(PakFileConf);
			PlatformAssets.Add(PakFileParser.GetParserResult());
		}
	}

	PlatformAssets.Sort([](const FReleaseParserResult& l,const FReleaseParserResult& r)->bool{return l.Assets.Num()<r.Assets.Num();});
	
	for(auto& PlatformAsset:PlatformAssets)
	{
		for(const auto& Asset:PlatformAsset.Assets)
		{
			GetIncludeSpecifyAssets().Add(Asset);
		}
	}

	PlatformAssets.Sort([](const FReleaseParserResult& l,const FReleaseParserResult& r)->bool{return l.ExternFiles.Num()<r.ExternFiles.Num();});

	// 分析全平台都包含的外部文件添加至AllPlatform
	if(PlatformAssets.Num() > 1)
	{
		FReleaseParserResult AllPlatform;
		AllPlatform.Platform = ETargetPlatform::AllPlatforms;
		for(int32 FileIndex=0;FileIndex<PlatformAssets[0].ExternFiles.Num();)
		{
			FExternFileInfo& MinPlatformFileItem = PlatformAssets[0].ExternFiles[FileIndex];
			bool IsAllPlatformFile = true;
			for(int32 Internalindex =1;Internalindex<PlatformAssets.Num();++Internalindex)
			{
				int32 FindIndex = PlatformAssets[Internalindex].ExternFiles.FindLastByPredicate([&MinPlatformFileItem](const FExternFileInfo& file)
                {
                    return MinPlatformFileItem.IsSameMount(file);
                });
			
				if(FindIndex != INDEX_NONE)
				{
					PlatformAssets[Internalindex].ExternFiles.RemoveAt(FindIndex);				
				}
				else
				{
					IsAllPlatformFile = false;
					break;
				}
			}
			if(IsAllPlatformFile)
			{
				AllPlatform.ExternFiles.AddUnique(MinPlatformFileItem);
				PlatformAssets[0].ExternFiles.RemoveAt(FileIndex);
				continue;
			}
			++FileIndex;
		}
		PlatformAssets.Add(AllPlatform);
	}
	
	// for not-uasset file
	for(const auto& Platform:PlatformAssets)
	{
		auto CheckIsExisitPlatform = [this](ETargetPlatform InPlatform)->bool
		{
			bool result = false;
			for(auto& ExistPlatform:AddExternAssetsToPlatform)
			{
				if(ExistPlatform.TargetPlatform == InPlatform)
				{
					result = true;
					break;
				}
			}
			return result;
		};

		if(CheckIsExisitPlatform(Platform.Platform))
		{
			for(auto& ExistPlatform:AddExternAssetsToPlatform)
			{
				if(ExistPlatform.TargetPlatform == Platform.Platform)
				{
					ExistPlatform.AddExternFileToPak.Append(Platform.ExternFiles);
				}
			}
		}
		else
		{
			FPlatformExternAssets NewPlatform;
			NewPlatform.TargetPlatform = Platform.Platform;
			NewPlatform.AddExternFileToPak = Platform.ExternFiles;
			AddExternAssetsToPlatform.Add(NewPlatform);
		}	
	}

	if(AutoChunkNum > 1)
	{
		bEnableChunk = true;
		bCreateDefaultChunk = false;
		int32 Index = 0;
		int32 Space = FMath::CeilToInt(IncludeSpecifyAssets.Num() / AutoChunkNum);
		for (int32 i = 0; i < AutoChunkNum; ++i)
		{
			if(Index > IncludeSpecifyAssets.Num() - 1)
			{
				continue;;
			}
			
			int32 End = Index + Space - 1 > IncludeSpecifyAssets.Num() - 1 ? IncludeSpecifyAssets.Num() - 1 : Index + Space - 1;
			FChunkInfo ChunkInfo;
			ChunkInfo.bStorageUnrealPakList = true;
			ChunkInfo.ChunkName = FString::Printf(TEXT("Chunk%d"), i);
			while (Index <= End)
			{
				ChunkInfo.IncludeSpecifyAssets.Add(IncludeSpecifyAssets[Index]);
				++Index;
			}
			
			ChunkInfos.Add(ChunkInfo);
		}

		if(bEnableExternFilesDiff)
		{
			FChunkInfo ChunkInfo;
			ChunkInfo.bStorageUnrealPakList = true;
			ChunkInfo.ChunkName = FString::Printf(TEXT("Chunk%d"), AutoChunkNum);
			ChunkInfo.AddExternAssetsToPlatform.Append(AddExternAssetsToPlatform);
			ChunkInfos.Add(ChunkInfo);
		}
	}
}