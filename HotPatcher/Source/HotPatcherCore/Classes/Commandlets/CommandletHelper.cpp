#pragma once
#include "CommandletHelper.h"

#include "ETargetPlatform.h"
#include "FlibPatchParserHelper.h"
#include "ThreadUtils/FProcWorkerThread.hpp"
#include "HotPatcherLog.h"
#include "Engine/Engine.h"

void CommandletHelper::ReceiveMsg(const FString& InMsgType,const FString& InMsg)
{
	UE_LOG(LogHotPatcherCommandlet,Display,TEXT("%s:%s"),*InMsgType,*InMsg);
}

void CommandletHelper::ReceiveShowMsg(const FString& InMsg)
{
	UE_LOG(LogHotPatcherCommandlet,Display,TEXT("%s"),*InMsg);
}
	
TArray<FString> CommandletHelper::ParserPatchConfigByCommandline(const FString& Commandline,const FString& Token)
{
	TArray<FString> result;
	TMap<FString, FString> KeyValues = THotPatcherTemplateHelper::GetCommandLineParamsMap(Commandline);
	if(KeyValues.Find(Token))
	{
		FString AddPakListInfo = *KeyValues.Find(Token);
		AddPakListInfo.ParseIntoArray(result,TEXT(","));
	}
	return result;
}
	
TArray<ETargetPlatform> CommandletHelper::ParserPatchPlatforms(const FString& Commandline)
{
	TArray<ETargetPlatform> result;
	for(auto& PlatformName:ParserPatchConfigByCommandline(Commandline,ADD_PATCH_PLATFORMS))
	{
		ETargetPlatform Platform = ETargetPlatform::None;
		THotPatcherTemplateHelper::GetEnumValueByName(PlatformName,Platform);
		if(Platform != ETargetPlatform::None)
		{
			result.AddUnique(Platform);
		}
	}
	return result;
}

TArray<FDirectoryPath> CommandletHelper::ParserPatchFilters(const FString& Commandline,const FString& FilterName)
{
	TArray<FDirectoryPath> Result;
	for(auto& FilterPath:ParserPatchConfigByCommandline(Commandline,FString::Printf(TEXT("Add%s"),*FilterName)))
	{
		FDirectoryPath Path;
		Path.Path = FilterPath;
		Result.Add(Path);
	}
	return Result;
}

static bool IsRequestingExit()
{
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 23)
	return IsEngineExitRequested();
#else
	return GIsRequestingExit;
#endif
}

static void RequestEngineExit()
{
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 23)
	RequestEngineExit(TEXT("GeneralCommandlet ComWrapperShutdownEvent"));
#else
	GIsRequestingExit  = true;
#endif
}

void CommandletHelper::MainTick(TFunction<bool()> IsRequestExit)
{
	GIsRunning = true;

	//@todo abstract properly or delete
	#if PLATFORM_WINDOWS// Windows only
	// Used by the .com wrapper to notify that the Ctrl-C handler was triggered.
	// This shared event is checked each tick so that the log file can be cleanly flushed.
	FEvent* ComWrapperShutdownEvent = FPlatformProcess::GetSynchEventFromPool(true);
#endif
		
	// main loop
	FDateTime LastConnectionTime = FDateTime::UtcNow();

	while (GIsRunning && !IsRequestingExit() && !IsRequestExit())
	{
		GEngine->UpdateTimeAndHandleMaxTickRate();
		GEngine->Tick(FApp::GetDeltaTime(), false);

		// update task graph
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

		// execute deferred commands
		for (int32 DeferredCommandsIndex=0; DeferredCommandsIndex<GEngine->DeferredCommands.Num(); DeferredCommandsIndex++)
		{
			GEngine->Exec( GWorld, *GEngine->DeferredCommands[DeferredCommandsIndex], *GLog);
		}

		GEngine->DeferredCommands.Empty();

		// flush log
		GLog->FlushThreadedLogs();

#if PLATFORM_WINDOWS
		if (ComWrapperShutdownEvent->Wait(0))
		{
			RequestEngineExit();
		}
#endif
	}
		
	//@todo abstract properly or delete 
	#if PLATFORM_WINDOWS
	FPlatformProcess::ReturnSynchEventToPool(ComWrapperShutdownEvent);
	ComWrapperShutdownEvent = nullptr;
#endif

	GIsRunning = false;
}

#define ADD_PLATFORM_PAK_LIST TEXT("AddPlatformPakList")
static TArray<FPlatformPakListFiles> ParserPlatformPakList(const FString& Commandline)
{
	TArray<FPlatformPakListFiles> result;
	TMap<FString, FString> KeyValues = THotPatcherTemplateHelper::GetCommandLineParamsMap(Commandline);
	if(KeyValues.Find(ADD_PLATFORM_PAK_LIST))
	{
		FString AddPakListInfo = *KeyValues.Find(ADD_PLATFORM_PAK_LIST);
		TArray<FString> PlatformPakLists;
		AddPakListInfo.ParseIntoArray(PlatformPakLists,TEXT(","));

		for(auto& PlatformPakList:PlatformPakLists)
		{
			TArray<FString> PlatformPakListToken;
			PlatformPakList.ParseIntoArray(PlatformPakListToken,TEXT("+"));
			if(PlatformPakListToken.Num() >= 2)
			{
				FString PlatformName = PlatformPakListToken[0];
				FPlatformPakListFiles PlatformPakListItem;
				THotPatcherTemplateHelper::GetEnumValueByName(PlatformName,PlatformPakListItem.TargetPlatform);
				for(int32 index=1;index<PlatformPakListToken.Num();++index)
				{
					FString PakListPath = PlatformPakListToken[index];
					if(FPaths::FileExists(PakListPath))
					{
						FFilePath PakFilePath;
						PakFilePath.FilePath = PakListPath;
						PlatformPakListItem.PakResponseFiles.Add(PakFilePath);
					}
				}
				result.Add(PlatformPakListItem);
			}
		}
	}
	return result;
}