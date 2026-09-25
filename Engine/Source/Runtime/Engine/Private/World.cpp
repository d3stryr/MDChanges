Warning: truncated output (original token count: 88024)
Total output lines: 11017

// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	World.cpp: UWorld implementation
=============================================================================*/

#include "Engine/World.h"

#include "Engine/ChildConnection.h"
#include "Engine/GameInstance.h"
#include "Engine/CollisionProfile.h"
#include "Engine/PawnIterator.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "Engine/PendingNetGame.h"
#include "Misc/Paths.h"
#include "Logging/LogScopedCategoryAndVerbosityOverride.h"
#include "Logging/LogScopedVerbosityOverride.h"
#include "Particles/ParticleSystemComponent.h"
#include "StateStreamManager.h"
#include "Stats/StatsMisc.h"
#include "Misc/ScopedSlowTask.h"
#include "SceneInterface.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/ObjectRedirector.h"
#include "SceneView.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UObjectAnnotation.h"
#include "Misc/PackageName.h"
#include "Misc/DataValidation.h"
#include "GameMapsSettings.h"
#include "TimerManager.h"
#include "AI/NavigationSystemBase.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Model.h"
#include "Engine/LevelBounds.h"
#include "UObject/MetaData.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Engine/Canvas.h"
#include "GameFramework/DefaultPhysicsVolume.h"
#include "RendererInterface.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingGCHelper.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstance.h"
#include "Engine/LocalPlayer.h"
#include "ComponentReregisterContext.h"
#include "UnrealEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/CullDistanceVolume.h"
#include "Engine/Console.h"
#include "Engine/WorldComposition.h"
#include "ExternalPackageHelper.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/WorldPartitionActorDescUtils.h"
#include "GameFramework/GameNetworkManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/NetworkProfiler.h"
#include "TickTaskManagerInterface.h"
#include "FXSystem.h"
#include "AudioDevice.h"
#include "VisualLogger/VisualLogger.h"
#include "LevelUtils.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "AI/AISystemBase.h"
#include "Camera/CameraActor.h"
#include "Engine/NetworkObjectList.h"
#include "GameFramework/GameStateBase.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Physics/Experimental/ChaosEventRelay.h"
#include "Components/BrushComponent.h"
#include "Engine/Polys.h"
#include "Components/ModelComponent.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/DemoNetDriver.h"
#include "VT/LightmapVirtualTexture.h" 
#include "Materials/MaterialParameterCollectionInstance.h"
#include "ParameterCollection.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Streaming/ServerStreamingLevelsVisibility.h"
#include "Streaming/LevelStreamingDelegates.h"
#include "Streaming/StreamingWorldSubsystemInterface.h"
#include "Components/UnregisterComponentContext.h"
#include "Misc/ScopeRWLock.h"
#include "Chaos/ChaosDebugNameDefines.h"

#if WITH_EDITOR
	#include "DerivedDataCacheInterface.h"
	#include "ThumbnailRendering/WorldThumbnailInfo.h"
	#include "Editor/UnrealEdTypes.h"
	#include "HierarchicalLOD.h"
	#include "IHierarchicalLODUtilities.h"
	#include "HierarchicalLODUtilitiesModule.h"
	#include "ObjectTools.h"
	#include "Engine/LODActor.h"
	#include "StaticMeshCompiler.h"
	#include "WorldPartition/DataLayer/WorldDataLayers.h"
	#include "PieFixupSerializer.h"
	#include "ActorFolder.h"
	#include "ActorDeferredScriptManager.h"
	#include "AssetCompilingManager.h"
	#include "DeletedObjectPlaceholder.h"
#endif


#include "PhysicsField/PhysicsFieldComponent.h"
#include "EngineModule.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "Net/DataChannel.h"
#include "Engine/LevelStreamingPersistent.h"
#include "AI/Navigation/AvoidanceManager.h"
#include "PhysicsEngine/PhysicsConstraintActor.h"
#include "PhysicsEngine/PhysicsCollisionHandler.h"
#include "Engine/ShadowMapTexture2D.h"
#include "Components/LineBatchComponent.h"
#include "Materials/MaterialParameterCollection.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Engine/LightMapTexture2D.h"
#include "UObject/UObjectThreadContext.h"
#include "Engine/CoreSettings.h"
#include "Net/PerfCountersHelpers.h"
#include "InGamePerformanceTracker.h"
#include "Engine/AssetManager.h"
#include "Templates/GuardValueAccessors.h"
#include "Engine/HLODProxy.h"
#include "MoviePlayerProxy.h"
#include "ObjectTrace.h"
#include "ReplaySubsystem.h"
#include "Net/NetPing.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ProfilingDebugging/CountersTrace.h"

#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"

#include "ChaosSolversModule.h"
#include "HAL/LowLevelMemStats.h"
#include "ProfilingDebugging/AssetMetadataTrace.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(World)

DEFINE_LOG_CATEGORY_STATIC(LogWorld, Log, All);
DEFINE_LOG_CATEGORY(LogSpawn);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(CORE_API, Basic);
CSV_DEFINE_CATEGORY(LevelStreamingProfiling, true);
CSV_DEFINE_CATEGORY(LevelStreamingAdaptive, true);
CSV_DEFINE_CATEGORY(LevelStreamingAdaptiveDetail, false);
CSV_DEFINE_CATEGORY(LevelStreamingDetail, false);

TRACE_DECLARE_INT_COUNTER(NumStreamingLevelsToConsider, TEXT("LevelStreamingProfiling/NumStreamingLevelsToConsider"));

#define LOCTEXT_NAMESPACE "World"

#if WITH_EDITOR
static FAutoConsoleCommand CheckExternalActorsConsistency(
	TEXT("World.CheckExternalActorsConsistency"),
	TEXT("Make sure worlds that don't use one file per actors don't have external actors."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.WaitForCompletion();

		TArray<FAssetData> WorldAssets;
		{
			FARFilter Filter;
			Filter.bIncludeOnlyOnDiskAssets = true;
			Filter.ClassPaths.Add(FTopLevelAssetPath(UWorld::StaticClass()));
			Filter.bRecursiveClasses = true;

			IAssetRegistry::GetChecked().GetAssets(Filter, WorldAssets);
		}

		TArray<TPair<FName, uint32>> InconsistentWorlds;

		int32 Percentage = 0;
		int32 WorldAssetIndex = 0;
		for (const FAssetData& WorldAsset : WorldAssets)
		{
			if (!ULevel::GetIsLevelUsingExternalActorsFromAsset(WorldAsset))
			{
				FARFilter Filter;
				Filter.bIncludeOnlyOnDiskAssets = true;
				Filter.PackagePaths.Add(*ULevel::GetExternalActorsPath(WorldAsset.PackageName.ToString()));
				Filter.bRecursivePaths = true;
	
				TArray<FAssetData> ActorAssets;
				IAssetRegistry::GetChecked().GetAssets(Filter, ActorAssets);

				if (!ActorAssets.IsEmpty())
				{
					InconsistentWorlds.Add({ WorldAsset.PackageName, ActorAssets.Num()});
				}
			}

			const int32 CurrentPercentage = (++WorldAssetIndex * 100) / WorldAssets.Num();
			if (Percentage != CurrentPercentage)
			{
				Percentage = CurrentPercentage;
				UE_LOGF(LogWorld, Log, "Processing world packages (%02d%%)", Percentage);
			}
		}

		if (InconsistentWorlds.Num())
		{
			UE_LOGF(LogWorld, Warning, "Found %d inconsistent worlds:", InconsistentWorlds.Num());

			for (TPair<FName, uint32> InconsistentWorld : InconsistentWorlds)
			{
				UE_LOGF(LogWorld, Warning, "\t %ls (%d)", *InconsistentWorld.Key.ToString(), InconsistentWorld.Value);
			}
		}
	})
);
#endif //  WITH_EDITOR

static int32 GMaximumMakingVisibleLevels = 1;
static FAutoConsoleVariableRef CVarMaximumMakingVisibleLevels(
	TEXT("LevelStreaming.MaximumMakingVisibleLevels"),
	GMaximumMakingVisibleLevels,
	TEXT("Sets the maximum number of levels in the process of being made visible (default is 1). \n")
	TEXT("Can be used in conjunction with incremental component pre-registration & asynchronous physics state creation \n")
	TEXT("and set to a value greater than 1 to maximize the usage of the time limit allowed by s.LevelStreamingActorsUpdateTimeLimit \n")
	TEXT("(see p.Chaos.EnableAsyncInitBody and LevelStreaming.AllowIncrementalPreRegisterComponents)."),
	ECVF_Default);

static int32 GMaximumMakingInvisibleLevels = 1;
static FAutoConsoleVariableRef CVarMaximumMakingInvisibleLevels(
	TEXT("LevelStreaming.MaximumMakingInvisibleLevels"),
	GMaximumMakingInvisibleLevels,
	TEXT("Sets the maximum number of levels in the process of being made invisible (default is 1). \n")
	TEXT("Can be used in conjunction with incremental component pre-unregistration & asynchronous physics state destruction \n")
	TEXT("and set to a value greater than 1 to maximize the usage of the time limit allowed by s.UnregisterComponentsTimeLimit \n")
	TEXT("(see p.Chaos.EnableAsyncInitBody and LevelStreaming.AllowIncrementalPreUnregisterComponents)."),
	ECVF_Default);

static int32 GLevelVisibilityPrioritySort = 0;
FAutoConsoleVariableRef CVarLevelVisibilityPrioritySort(TEXT("LevelStreaming.VisibilityPrioritySort"), GLevelVisibilityPrioritySort,
	TEXT("Defines how to sort the priority of handling different streaming level requests\n")
	TEXT("-1 - Do not sort at all\n")
	TEXT("0 - Default behavior of sorting all add to world requests before removes\n")
	TEXT("1 - Sort removes before adds, this is better for memory usage\n")
	TEXT("2 - Interleave adds and removes, starting with add\n")
	TEXT("3 - Interleave adds and removes, starting with remove\n"));

static int32 bDisableRemapScriptActors = 0;
FAutoConsoleVariableRef CVarDisableRemapScriptActors(TEXT("net.DisableRemapScriptActors"), bDisableRemapScriptActors, TEXT("When set, disables name remapping of compiled script actors (for networking)"));

static bool bDisableInGamePerfTrackersForUninitializedWorlds = true;
FAutoConsoleVariableRef CVarDisableInGamePerfTrackersForUninitializedWorlds(TEXT("s.World.SkipPerfTrackerForUninitializedWorlds"), bDisableInGamePerfTrackersForUninitializedWorlds, TEXT("When set, disables allocation of InGamePerformanceTrackers for Worlds that aren't initialized."));

static bool bCreateStaticLevelCollection = false;
FAutoConsoleVariableRef CVarCreateStaticLevelCollection(TEXT("s.World.CreateStaticLevelCollection"), bCreateStaticLevelCollection,
	TEXT("When set, create a separate level collection for static streaming levels that will not be duplicated by DuplicateRequestedLevels.\n")
	TEXT("If this is 0, static streaming levels will be part of the main DynamicSourceLevels collection."));

static bool GShouldInitWorldReleaseScene = true;
FAutoConsoleVariableRef CVarShouldInitWorldReleaseScene(
	TEXT("s.World.ShouldInitWorldReleaseScene"), 
	GShouldInitWorldReleaseScene,
	TEXT("Set to false to revert to earlier behavior (causes leaking Scene and associated resources in certain situations)."),
	ECVF_Default);

UE::FTimeout UWorld::GetAddToWorldTimeout() const
{
	check(IsInGameThread());
	return AddToWorldTimeout.Get(UE::FTimeout::Never());
}

UE::FTimeout UWorld::GetRemoveFromWorldTimeout() const
{
	check(IsInGameThread());
	return RemoveFromWorldTimeout.Get(UE::FTimeout::Never());
}

// This cvar activates detection of incorrectly unregistered components after a world is cleaned-up.
// This is important since components left as registered/renderstatecreated after their world is cleaned up will 
// crash in FGlobalComponentRecreateRenderStateContext unless a GC happens before the recreate.
//
// Current intended config across builds....
// Debug + Dev = LogError + an ensure() to prompt devs to fix
// Test = LogFatalError so that hopefully it doesn't make it out of testing broken
// Ship = LogError, and hope for a GC pass before usage of a FGlobalComponentRecreateRenderStateContext, or LogFatalError if turned on in the config
#if UE_BUILD_TEST
static bool bIncorrectComponentUnregistrationIsFatal = true;
#else
static bool bIncorrectComponentUnregistrationIsFatal = false;
#endif

FAutoConsoleVariableRef CVarIncorrectComponentUnregistrationIsFatal(TEXT("s.World.IncorrectComponentUnregistrationIsFatal"), bIncorrectComponentUnregistrationIsFatal, TEXT("When set, incorrect component unregistration will log a fatal error."));

// Now that it's possible for subclasses of ULevelStreaming to indicate which async loads are necessary for loading,
// it's possible existing subclasses haven't added their required loads to their StreamingLevel->GetAsyncRequestIDs() array. As a fallback,
// allow users to force flushing of all async loads during level streaming, as was done in UE 5.4 and lower.
static bool bForceFlushAllAsyncLoadsDuringLevelStreaming = false;
FAutoConsoleVariableRef CVarForceFlushAllAsyncLoadsDuringLevelStreaming(TEXT("s.World.ForceFlushAllAsyncLoadsDuringLevelStreaming"), bForceFlushAllAsyncLoadsDuringLevelStreaming, TEXT("When set, level streaming will wait for all outstanding async loads globally."));

static TAutoConsoleVariable<int32> CVarPurgeEditorSceneDuringPIE(
	TEXT("r.PurgeEditorSceneDuringPIE"),
	0,
	TEXT("0 to keep editor scene fully initialized during PIE (default)\n")
	TEXT("1 to purge editor scene from memory during PIE and restore when the session finishes."));

#if RHI_RESOURCE_PROVENANCE_ENABLED
static TAutoConsoleVariable<int32> CVarRHIResourceProvenanceMaxMPCGCInstancesPerWorld(
	TEXT("r.RHI.ResourceProvenance.MaxMPCGCInstancesPerWorld"),
	256,
	TEXT("Maximum MPC instance resources recorded per world in each pre/post-GC phase. ")
	TEXT("The recorder emits GCCoverageOmitted when additional instances are skipped."),
	ECVF_Default);
#endif

static int32 GGroupedComponentMovementBufferSize = 20;
static FAutoConsoleVariableRef CVarGroupedComponentMovementBufferSize(
	TEXT("s.GroupedComponentMovement.BufferSize"),
	GGroupedComponentMovementBufferSize,
	TEXT("If s.GroupedComponentMovement.Enable is enabled, sets the max number of grouped component moves that can accumulate before it immediately flushes them.\n")
	TEXT("It will always flush any remaining updates at the end of the current tick group"),
	ECVF_Default);

namespace UE::Private::World
{
	struct FStreamingLevelsToConsiderIterationScope
	{
		FStreamingLevelsToConsiderIterationScope(FStreamingLevelsToConsider& InStreamingLevelsToConsider)
			: StreamingLevelsToConsider(InStreamingLevelsToConsider)
		{
			StreamingLevelsToConsider.BeginConsideration();
		}

		~FStreamingLevelsToConsiderIterationScope()
		{
			StreamingLevelsToConsider.EndConsideration();
		}

		FStreamingLevelsToConsider& StreamingLevelsToConsider;
	};

	void ParseAndSetClientHandshakeId(FURL& InUrl, UNetConnection* Connection)
	{
		if (const TCHAR* HandshakeIdStr = InUrl.GetOption(TEXT("HandshakeId="), nullptr))
		{
			TCHAR* EndStr = nullptr;
			uint32 HandshakeId = FCString::Strtoi(HandshakeIdStr, &EndStr, 10);
			if (HandshakeIdStr != EndStr && HandshakeId )
			{
				Connection->SetClientHandshakeId(HandshakeId);
			}
			else
			{
				UE_LOGF(LogNet, Warning, "Connection %ls received an invalid client handshake id.", *Connection->GetName());
			}
		}
	}

	int32 GetLocalPlayerIdentifier(FURL& InUrl)
	{
		if (const TCHAR* LocalPlayerIdStr = InUrl.GetOption(TEXT("LocalPlayerId="), nullptr))
		{
			TCHAR* EndStr = nullptr;
			uint32 LocalPlayerId = FCString::Strtoi(LocalPlayerIdStr, &EndStr, 10);
			if (LocalPlayerIdStr != EndStr)
			{
				return LocalPlayerId;
			}
			else
			{
				UE_LOGF(LogNet, Warning, "Invalid local player id.");
			}
		}

		return 0;
	}
}

namespace UE::Gameplay::CVars
{
	extern bool bDelayOnActorSpawnedUntilFinishedSpawning;
}

/*-----------------------------------------------------------------------------
	FAdaptiveAddToWorld implementation.
-----------------------------------------------------------------------------*/
static int32 GAdaptiveAddToWorldEnabled = 0;
FAutoConsoleVariableRef CVarAdaptiveAddToWorldEnabled(TEXT("s.AdaptiveAddToWorld.Enabled"), GAdaptiveAddToWorldEnabled, 
	TEXT("Enables the adaptive AddToWorld timeslice (replaces s.LevelStreamingActorsUpdateTimeLimit) (default: off)"));

static int32 GAdaptiveAddToWorldMethod = 1;
FAutoConsoleVariableRef CVarAdaptiveAddToWorldMethod(TEXT("s.AdaptiveAddToWorld.Method"), GAdaptiveAddToWorldMethod,
	TEXT("Heuristic to use for the adaptive timeslice\n")
	TEXT("0 - compute the target timeslice based on remaining work time\n")
	TEXT("1 - compute the target timeslice based on total work time for levels in flight (this avoids slowing before a level completes)\n"));

static float GAdaptiveAddToWorldTimeSliceMin = 1.0f;
FAutoConsoleVariableRef CVarAdaptiveAddToWorldTimeSliceMin(TEXT("s.AdaptiveAddToWorld.AddToWorldTimeSliceMin"), GAdaptiveAddToWorldTimeSliceMin, 
	TEXT("Minimum adaptive AddToWorld timeslice"));

static float GAdaptiveAddToWorldTimeSliceMax = 6.0f;
FAutoConsoleVariableRef CVarAdaptiveAddToWorldTimeSliceMax(TEXT("s.AdaptiveAddToWorld.AddToWorldTimeSliceMax"), GAdaptiveAddToWorldTimeSliceMax, 
	TEXT("Maximum adaptive AddToWorld timeslice"));

static float GAdaptiveAddToWorldTargetMaxTimeRemaining = 6.0f;
FAutoConsoleVariableRef CVarAdaptiveAddToWorldTargetMaxTimeRemaining(TEXT("s.AdaptiveAddToWorld.TargetMaxTimeRemaining"), GAdaptiveAddToWorldTargetMaxTimeRemaining, 
	TEXT("Target max time remaining in seconds. If our estimated completion time is longer than this, the timeslice will increase. Lower values are more aggressive"));

static float GAdaptiveAddToWorldTimeSliceMaxIncreasePerSecond = 0.0f;
FAutoConsoleVariableRef CVarAdaptiveAddToWorldTimeSliceMaxIncreasePerSecond(TEXT("s.AdaptiveAddToWorld.TimeSliceMaxIncreasePerSecond"), GAdaptiveAddToWorldTimeSliceMaxIncreasePerSecond,
	TEXT("Max rate at which the adptive AddToWorld timeslice will increase. Set to 0 to increase instantly"));

static float GAdaptiveAddToWorldTimeSliceMaxReducePerSecond = 0.0f;
FAutoConsoleVariableRef CVarAdaptiveAddToWorldTimesliceReductionSpeedPerSecond(TEXT("s.AdaptiveAddToWorld.TimeSliceMaxReducePerSecond"), GAdaptiveAddToWorldTimeSliceMaxReducePerSecond,
	TEXT("Max rate at which the adptive AddToWorld timeslice will reduce. Set to 0 to reduce instantly"));

class FAdaptiveAddToWorld
{
	struct FHistoryEntry
	{
		float DeltaT = 0.0f;
		float TimeSlice = 0.0f;
		int32 WorkUnitsProcessed = 0;
	};
public:
	FAdaptiveAddToWorld()
	{
	}

	bool IsEnabled() const
	{
		return bEnabled;
	} 

	void SetEnabled(bool bInEnabled)
	{
		bEnabled = bInEnabled;
	}

	// Called when level streaming update begins. 
	// Note: multiple updates per frame are supported, e.g for multiple views/splitscreen. Each update is counted as a separate history entry
	void BeginUpdate()
	{
		if (bEnabled)
		{
		    WorkUnitsProcessedThisUpdate = 0;
		    TotalWorkUnitsRemaining = 0;
			TotalWorkUnitsForLevelsInFlight = 0;
		}
	}

	// Called when level streaming update ends 
	void EndUpdate()
	{
		if ( !bEnabled )
		{
			return;
		}
		QUICK_SCOPE_CYCLE_COUNTER(STAT_LevelStreamingAdaptiveUpdate);
		CSV_SCOPED_TIMING_STAT(LevelStreamingAdaptiveDetail, Update);
		// Only count frames where we have a reasonable number of work units to process
		double CurrentTimestamp = FPlatformTime::Seconds();
		float DeltaT = float(CurrentTimestamp - LastUpdateTimestamp);
		// Only consider frames where we're using the full timeslice
		if (TotalWorkUnitsRemaining > 0)
		{
			FHistoryEntry& HistoryEntry = UpdateHistory[HistoryFrameIndex];
			HistoryEntry.WorkUnitsProcessed = WorkUnitsProcessedThisUpdate;
			HistoryEntry.DeltaT = DeltaT;
			HistoryEntry.TimeSlice = GetTimeSlice();
			HistoryFrameIndex = (HistoryFrameIndex + 1) % HistorySize;

			// Make sure we have a complete history buffer before calculating
			if (ValidFrameCount < HistorySize)
			{
				BaseTimeSlice = GAdaptiveAddToWorldTimeSliceMin;
				ValidFrameCount++;
			}
			else
			{
				// Compute the rolling averages
				int32 TotalWorkUnits = 0;
				float TotalTime = 0.0f;
				float TotalTimeSliceMs = 0.0f;
				for (int i = 0; i < HistorySize; i++)
				{
					TotalWorkUnits += UpdateHistory[i].WorkUnitsProcessed;
					TotalTime += UpdateHistory[i].DeltaT;
					TotalTimeSliceMs += UpdateHistory[i].TimeSlice;
				}

				float AverageTimeSliceMs = TotalTimeSliceMs / float(HistorySize);

				float AverageWorkUnitsPerSecondWith1MsTimeSlice = (float)TotalWorkUnits / ( TotalTime * AverageTimeSliceMs );
				CSV_CUSTOM_STAT(LevelStreamingAdaptiveDetail, AverageWorkUnitsPerSecondWith1MsTimeSlice, AverageWorkUnitsPerSecondWith1MsTimeSlice, ECsvCustomStatOp::Set);

				// Work out what the timeslice should be
				float TargetTimeSlice = 0.0f;
				if (GAdaptiveAddToWorldMethod == 1)
				{
					float EstimatedTotalTimeForCurrentLevelsWith1MsTimeSlice = (float)TotalWorkUnitsForLevelsInFlight / AverageWorkUnitsPerSecondWith1MsTimeSlice;
					CSV_CUSTOM_STAT(LevelStreamingAdaptiveDetail, EstimatedTotalTimeForCurrentLevelsWith1MsTimeSlice, EstimatedTotalTimeForCurrentLevelsWith1MsTimeSlice, ECsvCustomStatOp::Set);
					TargetTimeSlice = EstimatedTotalTimeForCurrentLevelsWith1MsTimeSlice / GAdaptiveAddToWorldTargetMaxTimeRemaining - ExtraTimeSlice;
				}
				else
				{
					float EstimatedTimeRemainingWith1MsTimeSlice = (float)TotalWorkUnitsRemaining / AverageWorkUnitsPerSecondWith1MsTimeSlice;
					CSV_CUSTOM_STAT(LevelStreamingAdaptiveDetail, EstimatedTimeRemainingWith1MsTimeSlice, EstimatedTimeRemainingWith1MsTimeSlice, ECsvCustomStatOp::Set);
					TargetTimeSlice = EstimatedTimeRemainingWith1MsTimeSlice / GAdaptiveAddToWorldTargetMaxTimeRemaining - ExtraTimeSlice;
				}

				float DesiredBaseTimeSlice = FMath::Clamp(TargetTimeSlice, GAdaptiveAddToWorldTimeSliceMin, GAdaptiveAddToWorldTimeSliceMax);

				// Limit the timeslice change based on MaxIncreasePerSecond/ MaxReducePerSecond
				float NewBaseTimeSlice = DesiredBaseTimeSlice;
				if (DesiredBaseTimeSlice > BaseTimeSlice && GAdaptiveAddToWorldTimeSliceMaxIncreasePerSecond > 0.0)
				{
					NewBaseTimeSlice = FMath::Min(NewBaseTimeSlice, BaseTimeSlice + GAdaptiveAddToWorldTimeSliceMaxIncreasePerSecond * DeltaT);
				}
				if (DesiredBaseTimeSlice < BaseTimeSlice && GAdaptiveAddToWorldTimeSliceMaxReducePerSecond > 0.0)
				{
					NewBaseTimeSlice = FMath::Max(NewBaseTimeSlice, BaseTimeSlice - GAdaptiveAddToWorldTimeSliceMaxReducePerSecond * DeltaT);
				}
				BaseTimeSlice = NewBaseTimeSlice;
			}
		}
		else
		{
			BaseTimeSlice = GAdaptiveAddToWorldTimeSliceMin;
		}

		CSV_CUSTOM_STAT(LevelStreamingAdaptiveDetail, WorkUnitsProcessedPerSecond, float(WorkUnitsProcessedThisUpdate)/DeltaT, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(LevelStreamingAdaptiveDetail, WorkUnitsProcessedThisUpdate, WorkUnitsProcessedThisUpdate, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(LevelStreamingAdaptive, TimeSlice, GetTimeSlice(), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(LevelStreamingAdaptive, WorkUnitsRemaining, TotalWorkUnitsRemaining, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(LevelStreamingAdaptive, TotalWorkUnitsForLevelsInFlight, TotalWorkUnitsForLevelsInFlight, ECsvCustomStatOp::Set);
		LastUpdateTimestamp = CurrentTimestamp;
	}

	void RegisterAddToWorldWork(int32 StartWorkUnits, int32 EndWorkUnits, int32 TotalWorkUnits)
	{
		WorkUnitsProcessedThisUpdate += StartWorkUnits - EndWorkUnits;
		TotalWorkUnitsRemaining += EndWorkUnits;
		TotalWorkUnitsForLevelsInFlight += TotalWorkUnits;
	}

	float GetTimeSlice() const
	{
		return ExtraTimeSlice + BaseTimeSlice;
	}

	void SetExtraTimeSlice(float InExtraTimeSlice)
	{
		ExtraTimeSlice = InExtraTimeSlice;
	}


private:
	static const int32 HistorySize = 16;

	int32 WorkUnitsProcessedThisUpdate = 0;
	int32 TotalWorkUnitsRemaining = 0;
	int32 TotalWorkUnitsForLevelsInFlight = 0;
	int32 HistoryFrameIndex = 0;
	FHistoryEntry UpdateHistory[HistorySize];
	float BaseTimeSlice = 1.0f;
	float ExtraTimeSlice = 0.0f;
	double LastUpdateTimestamp = 0.0;
	int32 ValidFrameCount = 0;
    bool bEnabled = false;
};

static FAdaptiveAddToWorld GAdaptiveAddToWorld;


template<class Function>
static void ForEachNetDriver(UEngine* Engine, const UWorld* const World, const Function InFunction)
{
	if (Engine == nullptr || World == nullptr)
	{
		return;
	}

	FWorldContext* const Context = Engine->GetWorldContextFromWorld(World);
	if (Context != nullptr)
	{
		for (FNamedNetDriver& Driver : Context->ActiveNetDrivers)
		{
			InFunction(Driver.NetDriver);
		}
	}
}

FActorSpawnParameters::FActorSpawnParameters()
: Name(NAME_None)
, Template(NULL)
, Owner(NULL)
, Instigator(NULL)
, OverrideLevel(NULL)
#if WITH_EDITOR
, OverridePackage(nullptr)
#endif
, OverrideParentComponent(nullptr)
, SpawnCollisionHandlingOverride(ESpawnActorCollisionHandlingMethod::Undefined)
, bRemoteOwned(false)
, bNoFail(false)
, bDeferConstruction(false)
, bAllowDuringConstructionScript(false)
#if !WITH_EDITOR
, bForceGloballyUniqueName(false)
#else
, bTemporaryEditorActor(false)
, bHideFromSceneOutliner(false)
, bCreateActorPackage(true)
#endif
, NameMode(ESpawnActorNameMode::Required_Fatal)
, ObjectFlags(RF_Transactional)
{
}

FLevelCollection::FLevelCollection()
	: CollectionType(ELevelCollectionType::DynamicSourceLevels)
	, bIsVisible(true)
	, GameState(nullptr)
	, NetDriver(nullptr)
	, DemoNetDriver(nullptr)
	, PersistentLevel(nullptr)
{
}

FLevelCollection::~FLevelCollection()
{
	for (ULevel* Level : Levels)
	{
		if (Level)
		{
			check(Level->GetCachedLevelCollection() == this);
			Level->SetCachedLevelCollection(nullptr);
		}
	}
}

FLevelCollection::FLevelCollection(FLevelCollection&& Other)
	: CollectionType(Other.CollectionType)
	, bIsVisible(Other.bIsVisible)
	, GameState(Other.GameState)
	, NetDriver(Other.NetDriver)
	, DemoNetDriver(Other.DemoNetDriver)
	, PersistentLevel(Other.PersistentLevel)
	, Levels(MoveTemp(Other.Levels))
{
	for (ULevel* Level : Levels)
	{
		if (Level)
		{
			check(Level->GetCachedLevelCollection() == &Other);
			Level->SetCachedLevelCollection(this);
		}
	}
}

FLevelCollection& FLevelCollection::operator=(FLevelCollection&& Other)
{
	if (this != &Other)
	{
		CollectionType = Other.CollectionType;
		GameState = Other.GameState;
		NetDriver = Other.NetDriver;
		DemoNetDriver = Other.DemoNetDriver;
		PersistentLevel = Other.PersistentLevel;
		Levels = MoveTemp(Other.Levels);
		bIsVisible = Other.bIsVisible;

		for (ULevel* Level : Levels)
		{
			if (Level)
			{
				check(Level->GetCachedLevelCollection() == &Other);
				Level->SetCachedLevelCollection(this);
			}
		}
	}

	return *this;
}

void FLevelCollection::SetPersistentLevel(ULevel* const Level)
{
	PersistentLevel = Level;
}

void FLevelCollection::AddLevel(ULevel* const Level)
{
	if (Level)
	{
		// Sanity check that Level isn't already in another collection.
		check(Level->GetCachedLevelCollection() == nullptr);
		Levels.Add(Level);
		Level->SetCachedLevelCollection(this);
	}
}

void FLevelCollection::RemoveLevel(ULevel* const Level)
{
	if (Level)
	{
		check(Level->GetCachedLevelCollection() == this);
		Level->SetCachedLevelCollection(nullptr);
		Levels.Remove(Level);
	}
}

FScopedLevelCollectionContextSwitch::FScopedLevelCollectionContextSwitch(const FLevelCollection* const InLevelCollection, UWorld* const InWorld)
	: World(InWorld)
	, SavedTickingCollectionIndex(InWorld ? InWorld->GetActiveLevelCollectionIndex() : INDEX_NONE)
{
	if (World)
	{
		const int32 FoundIndex = World->GetLevelCollections().IndexOfByPredicate([InLevelCollection](const FLevelCollection& Collection)
		{
			return &Collection == InLevelCollection;
		});

		World->SetActiveLevelCollection(FoundIndex);
	}
}

FScopedLevelCollectionContextSwitch::FScopedLevelCollectionContextSwitch(int32 InLevelCollectionIndex, UWorld* const InWorld)
	: World(InWorld)
	, SavedTickingCollectionIndex(InWorld ? InWorld->GetActiveLevelCollectionIndex() : INDEX_NONE)
{
	if (World)
	{
		World->SetActiveLevelCollection(InLevelCollectionIndex);
	}
}

FScopedLevelCollectionContextSwitch::~FScopedLevelCollectionContextSwitch()
{
	if (World)
	{
		World->SetActiveLevelCollection(SavedTickingCollectionIndex);
	}
}

void FWorldPartitionEvents::BroadcastWorldPartitionInitialized(UWorld* InWorld, UWorldPartition* InWorldPartition)
{
	check(InWorld);
	InWorld->BroadcastWorldPartitionInitialized(InWorldPartition);
}

void FWorldPartitionEvents::BroadcastWorldPartitionUninitialized(UWorld* InWorld, UWorldPartition* InWorldPartition)
{
	check(InWorld);
	InWorld->BroadcastWorldPartitionUninitialized(InWorldPartition);
}

FWorldSubsystemCollection::FWorldSubsystemCollection() = default;
FWorldSubsystemCollection::~FWorldSubsystemCollection() = default;

void FWorldSubsystemCollection::OnSubsystemAdded(TNonNullPtr<USubsystem> Subsystem)
{
	Super::OnSubsystemAdded(Subsystem);

	UWorldSubsystem* WorldSubsystem = CastChecked<UWorldSubsystem>(Subsystem.Get());
	UWorld* World = CastChecked<UWorld>(GetOuter());

	//run relevant functions we may have missed if we are initialized after the world has set up
	if (!WorldSubsystem->HasCalledPostInitialize() && World->bIsWorldInitialized)
	{
		WorldSubsystem->PostInitialize();
		WorldSubsystem->EnsureHasCalledPostInitialize();
	}

	if (!WorldSubsystem->HasCalledBeginPlay() && World->HasBegunPlay())
	{
		WorldSubsystem->OnWorldBeginPlay(*World);
		WorldSubsystem->EnsureHasCalledBeginPlay();
	}
}


FAudioDeviceWorldDelegates::FOnWorldRegisteredToAudioDevice FAudioDeviceWorldDelegates::OnWorldRegisteredToAudioDevice;

FAudioDeviceWorldDelegates::FOnWorldUnregisteredWithAudioDevice FAudioDeviceWorldDelegates::OnWorldUnregisteredWithAudioDevice;

/*-----------------------------------------------------------------------------
	UWorld implementation.
-----------------------------------------------------------------------------*/

/** Global world pointer */
UWorldProxy GWorld;

TMap<FName, EWorldType::Type> UWorld::WorldTypePreLoadMap;

FWorldDelegates::FWorldEvent FWorldDelegates::OnPostWorldCreation;
FWorldDelegates::FWorldInitializationEvent FWorldDelegates::OnPreWorldInitialization;
FWorldDelegates::FWorldInitializationEvent FWorldDelegates::OnPostWorldInitialization;
#if WITH_EDITOR
FWorldDelegates::FWorldPreRenameEvent FWorldDelegates::OnPreWorldRename;
FWorldDelegates::FWorldPostRenameEvent FWorldDelegates::OnPostWorldRename;
FWorldDelegates::FWorldCurrentLevelChangedEvent FWorldDelegates::OnCurrentLevelChanged;
FWorldDelegates::FWorldCollectSaveReferencesEvent FWorldDelegates::OnCollectSaveReferences;
FWorldDelegates::FWorldEvent FWorldDelegates::OnPreRecreateScene;
FWorldDelegates::FWorldEvent FWorldDelegates::OnPostRecreateScene;
#endif // WITH_EDITOR
FWorldDelegates::FWorldPostDuplicateEvent FWorldDelegates::OnPostDuplicate;
FWorldDelegates::FWorldCleanupEvent FWorldDelegates::OnWorldCleanup;
FWorldDelegates::FWorldCleanupEvent FWorldDelegates::OnPostWorldCleanup;
FWorldDelegates::FWorldEvent FWorldDelegates::OnPreWorldFinishDestroy;
FWorldDelegates::FOnLevelChanged FWorldDelegates::LevelAddedToWorld;
FWorldDelegates::FOnLevelChanged FWorldDelegates::PreLevelRemovedFromWorld;
FWorldDelegates::FOnLevelChanged FWorldDelegates::LevelRemovedFromWorld;
FWorldDelegates::FLevelComponentsEvent FWorldDelegates::LevelComponentsCleared;
FWorldDelegates::FLevelComponentsEvent FWorldDelegates::LevelComponentsUpdated;
FWorldDelegates::FLevelOffsetEvent FWorldDelegates::PostApplyLevelOffset;
FWorldDelegates::FLevelTransformEvent FWorldDelegates::PostApplyLevelTransform;
FWorldDelegates::FWorldGetAssetTagsWithContext FWorldDelegates::GetAssetTagsWithContext;
FWorldDelegates::FOnWorldTickStart FWorldDelegates::OnWorldTickStart;
FWorldDelegates::FOnWorldTickEnd FWorldDelegates::OnWorldTickEnd;
FWorldDelegates::FOnWorldPreActorTick FWorldDelegates::OnWorldPreActorTick;
FWorldDelegates::FOnWorldPostActorTick FWorldDelegates::OnWorldPostActorTick;
FWorldDelegates::FOnWorldPreSendAllEndOfFrameUpdates FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates;
FWorldDelegates::FWorldEvent FWorldDelegates::OnWorldBeginTearDown;
#if WITH_EDITOR
FWorldDelegates::FRefreshLevelScriptActionsEvent FWorldDelegates::RefreshLevelScriptActions;
FWorldDelegates::FOnWorldPIEStarted FWorldDelegates::OnPIEStarted;
FWorldDelegates::FOnWorldPIEReady FWorldDelegates::OnPIEReady;
FWorldDelegates::FOnWorldPIEMapCreated FWorldDelegates::OnPIEMapCreated;
FWorldDelegates::FOnWorldPIEMapReady FWorldDelegates::OnPIEMapReady;
FWorldDelegates::FOnWorldPIEEnded FWorldDelegates::OnPIEEnded;
#endif // WITH_EDITOR
FWorldDelegates::FOnSeamlessTravelStart FWorldDelegates::OnSeamlessTravelStart;
FWorldDelegates::FOnSeamlessTravelTransition FWorldDelegates::OnSeamlessTravelTransition;
FWorldDelegates::FOnNetDriverCreated FWorldDelegates::OnNetDriverCreated;
FWorldDelegates::FOnCopyWorldData FWorldDelegates::OnCopyWorldData;
FWorldDelegates::FGameInstanceEvent FWorldDelegates::OnStartGameInstance;
FWorldDelegates::FGameInstanceWorldChangedEvent FWorldDelegates::OnGameInstanceWorldChanged;

UWorld::FOnWorldInitializedActors FWorldDelegates::OnWorldInitializedActors;

uint32 UWorld::CleanupWorldGlobalTag = 0;

UWorld::UWorld( const FObjectInitializer& ObjectInitializer )
: UObject(ObjectInitializer)
, bAllowDeferredPhysicsStateCreation(false)
, FeatureLevel(GMaxRHIFeatureLevel)
, bIsBuilt(false)
, bIsWorldInitialized(false)
#if WITH_EDITOR
, bDebugFrameStepExecutedThisFrame(false)
, bToggledBetweenPIEandSIEThisFrame(false)
, bPurgedScene(false)
#endif
, bShouldTick(true)
, bHasEverBeenInitialized(false)
, bAllowLumenPrimitiveTrackingInPreviewWorld(false)
, bIsThePostGCDelegateRegistered(false)
, bIsBeingCleanedUp(false)
, ActiveLevelCollectionIndex(INDEX_NONE)
, AudioDeviceHandle()
#if WITH_EDITOR
, HierarchicalLODBuilder(new FHierarchicalLODBuilder(this))
#endif
, URL(FURL(NULL))
,	FXSystem(NULL)
,	FlushLevelStreamingType(EFlushLevelStreamingType::None)
,	NextTravelType(TRAVEL_Relative)
#if WITH_EDITOR
,	bWorldWasCleanedUp(false)
#endif
,	CleanupWorldTag(0)

{
	TimerManager = new FTimerManager();
#if WITH_EDITOR
	SetPlayInEditorInitialNetMode(ENetMode::NM_Standalone);
	bBroadcastSelectionChange = true; //Ed Only
	EditorViews.SetNum(ELevelViewportType::LVT_MAX);
#endif // WITH_EDITOR

	FWorldDelegates::OnPostWorldCreation.Broadcast(this);

	if (!bDisableInGamePerfTrackersForUninitializedWorlds)
	{
		PerfTrackers = new FWorldInGamePerformanceTrackers();
	}
	else
	{
		PerfTrackers = nullptr;
	}

	IsInBlockTillLevelStreamingCompleted = 0;
	BlockTillLevelStreamingCompletedEpoch = 0;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
UWorld::~UWorld()
{
#if WITH_STATE_STREAM
	check(!StateStreamManager);
#endif

	if (PerfTrackers)
	{
		delete PerfTrackers;
	}
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void UWorld::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );

	Ar << PersistentLevel;

	if (Ar.UEVer() < VER_UE4_ADD_EDITOR_VIEWS)
	{
#if WITH_EDITOR
		EditorViews.SetNum(4);
#endif
		for (int32 i = 0; i < 4; ++i)
		{
			FLevelViewportInfo TempViewportInfo;
			Ar << TempViewportInfo;
#if WITH_EDITOR
			if (Ar.IsLoading())
			{
				EditorViews[i] = TempViewportInfo;
			}
#endif
		}
	}
#if WITH_EDITOR
	if ( Ar.IsLoading() )
	{
		for (FLevelViewportInfo& ViewportInfo : EditorViews)
		{
			ViewportInfo.CamUpdated = true;

			if ( ViewportInfo.CamOrthoZoom < MIN_ORTHOZOOM || ViewportInfo.CamOrthoZoom > MAX_ORTHOZOOM )
			{
				ViewportInfo.CamOrthoZoom = DEFAULT_ORTHOZOOM;
			}
		}
		EditorViews.SetNum(ELevelViewportType::LVT_MAX);
	}
#endif

	if (Ar.UEVer() < VER_UE4_REMOVE_SAVEGAMESUMMARY)
	{
		UObject* DummyObject;
		Ar << DummyObject;
	}

	if( !Ar.IsLoading() && !Ar.IsSaving() )
	{
		Ar << Levels;
#if WITH_EDITORONLY_DATA
		Ar << CurrentLevel;
#endif
		Ar << URL;

		Ar << NetDriver;
		
		for (TObjectPtr<ULineBatchComponent>& LineBatcher : LineBatchers)
		{
			Ar << LineBatcher;
		}

		Ar << PhysicsField;

		Ar << MyParticleEventManager;
		Ar << GameState;
		Ar << AuthorityGameMode;
		Ar << NetworkManager;

		Ar << NavigationSystem;
		Ar << AvoidanceManager;
	}

	Ar << ExtraReferencedObjects;

#if WITH_EDITOR
	if (Ar.IsSaving() && Ar.IsPersistent())
	{
		TArray<ULevelStreaming*> PersistedStreamingLevels;
		PersistedStreamingLevels.Reserve(StreamingLevels.Num());
		Algo::CopyIf(StreamingLevels, PersistedStreamingLevels, [&](ULevelStreaming* LevelStreaming) { return LevelStreaming && !LevelStreaming->HasAnyFlags(RF_Transient); });
		Ar << PersistedStreamingLevels;

		if (Ar.IsObjectReferenceCollector())
		{
			FWorldDelegates::OnCollectSaveReferences.Broadcast(this, Ar);
		}
	}
	else
#endif
	{
		Ar << StreamingLevels;
	}
		
	// Mark archive and package as containing a map if we're serializing to disk.
	if( !HasAnyFlags( RF_ClassDefaultObject ) && Ar.IsPersistent() )
	{
		Ar.ThisContainsMap();
		GetOutermost()->ThisContainsMap();
		GetOutermost()->ThisShouldLoadUncooked(Ar);
	}

#if WITH_EDITOR
	// Serialize for PIE
	if (Ar.GetPortFlags() & PPF_DuplicateForPIE)
	{
		Ar << OriginLocation;
		Ar << RequestedOriginLocation;
		Ar << OriginalWorldName;
	}
	
	// UWorlds loaded/duplicated for PIE must lose RF_Public and RF_Standalone since they should not be referenced by objects in other packages and they should be GCed normally
	if (GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor))
	{
		ClearFlags(RF_Public|RF_Standalone);
	}
#endif
}

void UWorld::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{	
	UWorld* This = CastChecked<UWorld>(InThis);

#if WITH_EDITOR
	if( GIsEditor )
	{
		Collector.AddReferencedObject( This->PersistentLevel, This );
		Collector.AddReferencedObjects(This->Levels, This);
		Collector.AddReferencedObject( This->CurrentLevel, This );
		Collector.AddReferencedObject( This->NetDriver, This );
		Collector.AddReferencedObject( This->DemoNetDriver, This );
		for (TObjectPtr<ULineBatchComponent>& LineBatcher : This->LineBatchers)
		{
			Collector.AddReferencedObject(LineBatcher, This);
		}
		Collector.AddReferencedObject( This->PhysicsField, This);
		Collector.AddReferencedObject( This->MyParticleEventManager, This );
		Collector.AddReferencedObject( This->GameState, This );
		Collector.AddReferencedObject( This->AuthorityGameMode, This );
		Collector.AddReferencedObject( This->NetworkManager, This );
		Collector.AddReferencedObject( This->NavigationSystem, This );
		Collector.AddReferencedObject( This->AvoidanceManager, This );
	}
#endif

	This->StreamingLevelsToConsider.AddReferencedObjects(InThis, Collector);

	This->SubsystemCollection.AddReferencedObjects(InThis, Collector);

	Super::AddReferencedObjects( InThis, Collector );
}

#if WITH_EDITOR
EDataValidationResult UWorld::IsDataValid(FDataValidationContext& Context) const 
{
	EDataValidationResult Result = EDataValidationResult::NotValidated;
	// If the validation system wishes to validate specific actors, it will pass them in the context as associated external objects
	// Otherwise we should assume that we came from a function such as UEditorValidatorSubsystem::IsObjectValid and we should validate whatever is loaded in the user's session
	if (Context.GetAssociatedExternalObjects().Num() != 0)
	{
		 for (const FAssetData& Asset : Context.GetAssociatedExternalObjects())
		 {
			if (AActor* Actor = Cast<AActor>(Asset.FastGetAsset(false)))
			{
				ensureMsgf(Actor->GetWorld() == this, TEXT("Attempting to validate actor %s whose owning world is %s and not this (%s)"), 
					*GetPathNameSafe(Actor),
					*GetPathNameSafe(Actor->GetWorld()),
					*GetPathName());
				Result = CombineDataValidationResults(Result, Actor->IsDataValid(Context));
			}
		 }
	}
	else
	{
		// Pass flags explicitly so we iterate non-active levels for data validation but continue to skip destroyed/deleted actors.
		for (FActorIterator It(this, EActorIteratorFlags::SkipPendingKill); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor)
			{
				Result = CombineDataValidationResults(Result, Actor->IsDataValid(Context));
			}
		}
	}
	return CombineDataValidationResults(Result, Super::IsDataValid(Context));
}

bool UWorld::Rename(const TCHAR* InName, UObject* NewOuter, ERenameFlags Flags)
{
	check(PersistentLevel);

	UPackage* OldPackage = GetOutermost();

	bool bShouldFail = false;
	FWorldDelegates::OnPreWorldRename.Broadcast(this, InName, NewOuter, Flags, bShouldFail);

	// Make sure our legacy lightmap data is initialized so it can be renamed
	PersistentLevel->HandleLegacyMapBuildData();

	const bool bTestRename = (Flags & REN_Test) != 0;

	FHierarchicalLODUtilitiesModule& Module = FModuleManager::LoadModuleChecked<FHierarchicalLODUtilitiesModule>("HierarchicalLODUtilities");
	IHierarchicalLODUtilities* Utilities = Module.GetUtilities();

	TArray<UPackage*> OldHLODPackages;
	const int32 NumHLODLevels = PersistentLevel->GetWorldSettings()->GetNumHierarchicalLODLevels();

	if (!bTestRename)
	{
		OldHLODPackages.SetNumZeroed(NumHLODLevels);

		for (AActor* Actor : PersistentLevel->Actors)
		{
			if (ALODActor* LODActor = Cast<ALODActor>(Actor))
			{
				if (UHLODProxy* HLODProxy = LODActor->GetProxy())
				{
					OldHLODPackages[LODActor->LODLevel - 1] = HLODProxy->GetPackage();
				}
			}
		}
	}

	if (bShouldFail)
	{
		return false;
	}

	const FWorldRenameFromRootContext PreRenameFromRootContext({ InName, NewOuter, Flags });
	const FWorldRenameFromRootContext PostRenameFromRootContext({ GetFName(), GetOuter(), Flags });

	if (!bTestRename)
	{
		PersistentLevel->PreRenameFromRoot(PreRenameFromRootContext);
	}

	// Rename the world itself
	if ( !Super::Rename(InName, NewOuter, Flags) )
	{
		return false;
	}

	if (!bTestRename)
	{
		PersistentLevel->PostRenameFromRoot(PostRenameFromRootContext);
	}

	// We're moving the world to a new package, rename UObjects which are map data but don't have the UWorld in their Outer chain.  There are two cases:
	// 1) legacy lightmap textures and MapBuildData object will be in the same package as the UWorld.  We need to move these to the new world package.
	// 2) MapBuildData will be in a separate package with lightmap textures underneath it.  We need to move these to an appropriate build data package.
	if (PersistentLevel->MapBuildData)
	{
		FName NewMapBuildDataName = PersistentLevel->MapBuildData->GetFName();
		UObject* NewMapBuildDataOuter = nullptr;

		if (PersistentLevel->MapBuildData->IsLegacyBuildData())
		{
			NewMapBuildDataOuter = NewOuter;

			TArray<UTexture2D*> LightMapsAndShadowMaps;
			GetLightMapsAndShadowMaps(PersistentLevel, LightMapsAndShadowMaps);

			for (UTexture2D* Tex : LightMapsAndShadowMaps)
			{
				if (Tex)
				{
					if (!Tex->Rename(*Tex->GetName(), NewOuter, Flags))
					{
						return false;
					}
				}
			}
			NewMapBuildDataOuter = NewOuter;
		}
		else
		{
			FString NewPackageName = NewOuter ? NewOuter->GetOutermost()->GetName() : GetOutermost()->GetName();
			NewPackageName += TEXT("_BuiltData");
			NewMapBuildDataName = FPackageName::GetShortFName(*NewPackageName);
			UPackage* BuildDataPackage = PersistentLevel->MapBuildData->GetOutermost();

			if (!BuildDataPackage->Rename(*NewPackageName, nullptr, Flags))
			{
				return false;
			}

			NewMapBuildDataOuter = BuildDataPackage;
		}

		if (!PersistentLevel->MapBuildData->Rename(*NewMapBuildDataName.ToString(), NewMapBuildDataOuter, Flags))
		{
			return false;
		}
	}


	if (!bTestRename)
	{
		// We also need to rename any external actor packages we have since the search for them is based on the world name
		// Make a Copy to iterate as this could modify PersistentLevel->Actors
		TArray<AActor*> CopyActors;
		CopyActors.Reserve(PersistentLevel->Actors.Num());
		Algo::CopyIf(PersistentLevel->Actors, CopyActors, [](AActor* InActor) { return InActor && InActor->IsMainPackageActor(); });
				
		// Instead of just renaming the package, re-embed and re-externalize the actor
		// this will leave dirty empty actor packages being which will be cleaned up though SaveAll, although SaveCurrentLevel won't pick them up
		for (AActor* Actor : CopyActors)
		{
			UPackage* ExternalPackage = Actor->GetPackage();
			Actor->SetPackageExternal(false);
			
			TArray<UObject*> DependantObjects;
			ForEachObjectWithPackage(ExternalPackage, [&DependantObjects](UObject* Object)
			{
				if (!Cast<UDeletedObjectPlaceholder>(Object))
				{
					DependantObjects.Add(Object);
				}
				return true;
			}, EGetObjectsFlags::None);
						
			Actor->SetPackageExternal(true);

			// Move dependant objects into the new actor package
			for (UObject* DependantObject : DependantObjects)
			{
				DependantObject->Rename(nullptr, Actor->GetExternalPackage(), Flags);
			}
		}

		// Process external objects other than actors
		if (PersistentLevel->IsUsingExternalObjects())
		{
			ForEachObjectWithOuter(PersistentLevel, [this](UObject* Object)
			{
				if (Object->IsPackageExternal() && !Object->IsA<AActor>())
				{
					FExternalPackageHelper::SetPackagingMode(Object, PersistentLevel, false);
					FExternalPackageHelper::SetPackagingMode(Object, PersistentLevel, true);
				}
				return true;
			}, EGetObjectsFlags::IncludeNestedObjects);
		}
	}

	// Rename the level script blueprint now, unless we are in PostLoad. ULevel::PostLoad should handle renaming this at load time.
	if (!FUObjectThreadContext::Get().IsRoutingPostLoad)
	{
		const bool bDontCreate = true;
		UBlueprint* LevelScriptBlueprint = PersistentLevel->GetLevelScriptBlueprint(bDontCreate);
		if ( LevelScriptBlueprint )
		{
			// See if we are just testing for a rename. When testing, the world hasn't actually changed outers, so we need to test for name collisions at the target outer.
			if ( bTestRename )
			{
				// We are just testing. Check for name collisions in the new package. This is only needed because these objects use the supplied outer's Outermost() instead of the outer itself
				if (!LevelScriptBlueprint->RenameGeneratedClasses(InName, NewOuter, Flags))
				{
					return false;
				}
			}
			else
			{
				// The level blueprint must be named the same as the level/world.
				// If there is already something there with that name, rename it to something else.
				if (UObject* ExistingObject = StaticFindObject(nullptr, LevelScriptBlueprint->GetOuter(), InName))
				{
					ExistingObject->Rename(nullptr, nullptr, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional | REN_AllowPackageLinkerMismatch);
				}

				// This is a normal rename. Use LevelScriptBlueprint->GetOuter() instead of NULL to make sure the generated top level objects are moved appropriately
				if ( !LevelScriptBlueprint->Rename(InName, LevelScriptBlueprint->GetOuter(), Flags) )
				{
					return false;
				}
			}
		}
	}

	// Update the PKG_ContainsMap package flag
	UPackage* NewPackage = GetOutermost();
	if ( !bTestRename && NewPackage != OldPackage )
	{
		// If this is the last world removed from a package, clear the PKG_ContainsMap flag
		if ( UWorld::FindWorldInPackage(OldPackage) == NULL )
		{
			OldPackage->ClearPackageFlags(PKG_ContainsMap);
		}

		// Set the PKG_ContainsMap flag in the new package
		NewPackage->ThisContainsMap();
	}

	// Move over HLOD assets to new _HLOD Package
	if (!bTestRename && OldHLODPackages.FindByPredicate([](UPackage* InPackage) -> bool { return InPackage != nullptr; }))
	{
		TArray<UObject*> DeleteObjects;

		for (int32 HLODIndex = 0; HLODIndex < NumHLODLevels; ++HLODIndex)
		{
			if (OldHLODPackages[HLODIndex] != nullptr)
			{
				UPackage* NewHLODPackage = Utilities->CreateOrRetrieveLevelHLODPackage(PersistentLevel, HLODIndex);

				TArray<UObject*> Objects;
				// Retrieve all of the HLOD objects 
				ForEachObjectWithOuter(OldHLODPackages[HLODIndex], [&Objects](UObject* Obj)
				{
					if (ObjectTools::IsObjectBrowsable(Obj))
					{
						Objects.Add(Obj);
					}
				});
				// Rename them 'into' the new HLOD package
				for (UObject* Object : Objects)
				{
					if(UHLODProxy* HLODProxy = Cast<UHLODProxy>(Object))
					{
						// HLOD proxy gets the same name as the package
						HLODProxy->Rename(*FPackageName::GetShortName(*NewHLODPackage->GetName()), NewHLODPackage);
						HLODProxy->SetMap(this);
					}
					else
					{
						Object->Rename(*Object->GetName(), NewHLODPackage);
					}
				}
				
				DeleteObjects.Add(Cast<UObject>(OldHLODPackages[HLODIndex]));
			}
		}
		
		// Delete the old HLOD packages
		ObjectTools::DeleteObjectsUnchecked(DeleteObjects);		
	}
	

	if (!bTestRename)
	{
		FWorldDelegates::OnPostWorldRename.Broadcast(this);
	}

	return true;
}
#endif

void UWorld::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	TArray<UObject*> ObjectsToFixReferences;
	TMap<UObject*, UObject*> ReplacementMap;

	// If we are not duplicating for PIE, fix up objects that travel with the world.
	// Note that these objects should really be inners of the world, so if they become inners later, most of this code should not be necessary
	if ( !bDuplicateForPIE )
	{
		check(PersistentLevel);

		// Update the persistent level's owning world. This is needed for some initialization
		if ( !PersistentLevel->OwningWorld )
		{
			PersistentLevel->OwningWorld = this;
		}

#if WITH_EDITORONLY_DATA
		// Update the current level as well
		if ( !CurrentLevel )
		{
			CurrentLevel = PersistentLevel;
		}
#endif

		UPackage* MyPackage = GetOutermost();

		// Make sure PKG_ContainsMap is set
		MyPackage->ThisContainsMap();

#if WITH_EDITOR
		// Add the world to the list of objects in which to fix up references.
		ObjectsToFixReferences.Add(this);

		// We're duplicating the world, also duplicate UObjects which are map data but don't have the UWorld in their Outer chain.  There are two cases:
		// 1) legacy lightmap textures and MapBuildData object will be in the same package as the UWorld
		// 2) MapBuildData will be in a separate package with lightmap textures underneath it
		if (PersistentLevel->MapBuildData)
		{
			UPackage* BuildDataPackage = MyPackage;
			FName NewMapBuildDataName = PersistentLevel->MapBuildData->GetFName();
			
			if (!PersistentLevel->MapBuildData->IsLegacyBuildData())
			{
				BuildDataPackage = PersistentLevel->CreateMapBuildDataPackage();
				NewMapBuildDataName = FPackageName::GetShortFName(BuildDataPackage->GetFName());
			}
			
			UObject* NewBuildData = StaticDuplicateObject(PersistentLevel->MapBuildData, BuildDataPackage, NewMapBuildDataName);
			NewBuildData->MarkPackageDirty();
			ReplacementMap.Add(PersistentLevel->MapBuildData, NewBuildData);
			ObjectsToFixReferences.Add(NewBuildData);

			UObject* NewTextureOuter = MyPackage;

			if (!PersistentLevel->MapBuildData->IsLegacyBuildData())
			{
				NewTextureOuter = NewBuildData;
			}

			TArray<UTexture2D*> LightMapsAndShadowMaps;
			GetLightMapsAndShadowMaps(PersistentLevel, LightMapsAndShadowMaps);

			// Duplicate the textures, if any
			for (UTexture2D* Tex : LightMapsAndShadowMaps)
			{
				if (Tex && Tex->GetOutermost() != NewTextureOuter)
				{
					UObject* NewTex = StaticDuplicateObject(Tex, NewTextureOuter, Tex->GetFName());
					ReplacementMap.Add(Tex, NewTex);
				}
			}
		}
#endif // WITH_EDITOR
	}

	FWorldDelegates::OnPostDuplicate.Broadcast(this, bDuplicateForPIE, ReplacementMap, ObjectsToFixReferences);

#if WITH_EDITOR
	// Now replace references from the old textures/classes to the new ones, if any were duplicated
	if (ReplacementMap.Num() > 0)
	{
		for (UObject* Obj : ObjectsToFixReferences)
		{
			FArchiveReplaceObjectRef<UObject> ReplaceAr(Obj, ReplacementMap, EArchiveReplaceObjectFlags::IgnoreOuterRef);
		}
		// PostEditChange is required for some objects to react to the change, e.g. update render-thread proxies
		for (UObject* Obj : ObjectsToFixReferences)
		{
			Obj->PostEditChange();
		}
	}

	if (bDuplicateForPIE)
	{
		// We use a weak ptr here in case the level gets destroyed before asset
		// compilation finishes.
		TWeakObjectPtr<ULevel> PersistentLevelPtr(PersistentLevel);
		auto ValidateTextureOverridesForPIE =
			[PersistentLevelPtr, FeatureLevel = GetFeatureLevel()]()
			{
				ULevel* Level = PersistentLevelPtr.Get();
				if (Level)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(ValidateTextureOverridesForPIE);

					TSet<UMaterialInterface*> ProcessedMaterials; 
					TArray<UMaterialInterface*> Materials;
					TArray<UPrimitiveComponent*> Components;

					// When duplicating for PIE, duplicated objects will have RF_NeedPostLoad, and GetUsedMaterials might invoke ConditionalPostLoad on its MID, triggering a
					// PostLoad chain from MID->Component->Actor->Level, which will remove null entries from the Actors array, resulting in an assert in the iterator.
					TArray<TObjectPtr<AActor>> LocalActors(Level->Actors);

					for (const TObjectPtr<AActor>& Actor : LocalActors)
					{
						if (Actor != nullptr)
						{
							Components.Reset();
							Actor->GetComponents<UPrimitiveComponent>(Components);
							for (UPrimitiveComponent* Component : Components)
							{
								Materials.Reset();
								Component->GetUsedMaterials(Materials);
								for (UMaterialInterface* Material : Materials)
								{
									bool bIsAlreadyInSet = false;
									ProcessedMaterials.FindOrAdd(Material, &bIsAlreadyInSet);
									if (!bIsAlreadyInSet)
									{
										if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Material))
										{
											MaterialInstance->ValidateTextureOverrides(GetFeatureLevelShaderPlatform_Checked(FeatureLevel));
										}
									}
								}
							}
						}
					}
				}
			};

		// When PIE begins, check/log any problems with textures assigned to material instances
		// but wait until all assets are properly compiled to to so.
		if (FAssetCompilingManager::Get().GetNumRemainingAssets() == 0)
		{
			ValidateTextureOverridesForPIE();
		}
		else
		{
			// Some assets are still being compiled, register to the event so we can do the validation
			// once the compilation is finished.
			TSharedPtr<FDelegateHandle> DelegateHandle = MakeShareable(new FDelegateHandle());
			*DelegateHandle = FAssetCompilingManager::Get().OnAssetPostCompileEvent().AddWeakLambda(this,
				[DelegateHandle, ValidateTextureOverridesForPIE](const TArray<FAssetCompileData>&)
				{
					if (FAssetCompilingManager::Get().GetNumRemainingAssets() == 0)
					{
						ValidateTextureOverridesForPIE();

						// Must be the last line because it will destroy the lambda along with the capture
						verify(FAssetCompilingManager::Get().OnAssetPostCompileEvent().Remove(*DelegateHandle));
					}
				}
			);
		}
	}
#endif // WITH_EDITOR
}

void UWorld::BeginDestroy()
{
	Super::BeginDestroy();

#if WITH_EDITOR
	// Make sure we catch worlds that where initialized through UEditorEngine::OnAssetLoaded/OnAssetCreated
	// (This can happen if World was loaded, then its RF_Standalone flag was removed and a GC happened) 
	if (WorldType == EWorldType::Inactive && IsInitialized())
	{
		CleanupWorld();
	}
#endif

	for (FLevelCollection& LevelCollection : LevelCollections)
	{
		TSet<TObjectPtr<ULevel>> CollectionLevels = LevelCollection.GetLevels();
		for (ULevel* CollectionLevel : CollectionLevels)
		{
			LevelCollection.RemoveLevel(CollectionLevel);
		}
	}

	if (PhysicsScene != nullptr)
	{
		// Tell PhysicsScene to stop kicking off async work so we can cleanup after pending work is complete.
		PhysicsScene->BeginDestroy();
	}

	if (Scene)
	{
		Scene->UpdateParameterCollections(TArray<FMaterialParameterCollectionInstanceResource*>());
	}

	FAudioDeviceHandle EmptyHandle;
	SetAudioDevice(EmptyHandle);
	check(!AudioDeviceDestroyedHandle.IsValid());
}

void UWorld::ReleasePhysicsScene()
{
	if (PhysicsScene)
	{
		delete PhysicsScene;
		PhysicsScene = NULL;

		if (GPhysCommandHandler)
		{
			GPhysCommandHandler->Flush();
		}
	}
}

void UWorld::FinishDestroy()
{
	if (bIsWorldInitialized)
	{
		UE_LOGF(LogWorld, Warning, "UWorld::FinishDestroy called after InitWorld without calling CleanupWorld first.");
	}

	// Avoid cleanup if the world hasn't been initialized, e.g., the default object or a world object that got loaded
	// due to level streaming.
	if (bHasEverBeenInitialized)
	{
		FWorldDelegates::OnPreWorldFinishDestroy.Broadcast(this);

		// Wait for Async Trace data to finish and reset global variable
		WaitForAllAsyncTraceTasks();

		// navigation system should be removed already by UWorld::CleanupWorld
		// unless it wanted to keep resources but got destroyed now
		SetNavigationSystem(nullptr);

		if (FXSystem)
		{
			FFXSystemInterface::MarkPendingKill( FXSystem.Get() );
			FXSystem = NULL;
		}

		ReleasePhysicsScene();

#if WITH_STATE_STREAM
		StateStreamManager->Game_DestroyLane(LaneId);
#endif

		if (Scene)
		{
			Scene->Release();
			Scene = NULL;

#if WITH_STATE_STREAM
			StateStreamManager = nullptr;
#endif

		}
	}

#if WITH_STATE_STREAM
	if (StateStreamManager)
	{
		StateStreamManager = nullptr;
	}
#endif

	// Clear GWorld pointer if it's pointing to this object.
	if( GWorld == this )
	{
		GWorld = NULL;
	}

	if (TimerManager)
	{
		delete TimerManager;
	}

#if WITH_EDITOR
	if (HierarchicalLODBuilder)
	{
		delete HierarchicalLODBuilder;
	}
#endif // WITH_EDITOR

	// Remove the PKG_ContainsMap flag from packages that no longer contain a world
	{
		UPackage* WorldPackage = GetOutermost();

		if (WorldPackage->HasAnyPackageFlags(PKG_ContainsMap))
		{
			bool bContainsAnotherWorld = false;
			TArray<UObject*> PotentialWorlds;
			GetObjectsWithPackage(WorldPackage, PotentialWorlds, EGetObjectsFlags::None);
			for (UObject* PotentialWorld : PotentialWorlds)
			{
				UWorld* World = Cast<UWorld>(PotentialWorld);
				if (World && World != this)
				{
					bContainsAnotherWorld = true;
					break;
				}
			}

			if ( !bContainsAnotherWorld )
			{
				WorldPackage->ClearPackageFlags(PKG_ContainsMap);
			}
		}
	}

	Super::FinishDestroy();
}

bool UWorld::IsReadyForFinishDestroy()
{
	// In single threaded, task will never complete unless we wait on it, allow FinishDestroy so we can wait on task, otherwise this will hang GC.
	// In multi threaded, we cannot wait in FinishDestroy, as this may schedule another task that is unsafe during GC.
	const bool bIsSingleThreadEnvironment = FPlatformProcess::SupportsMultithreading() == false;
	if (bIsSingleThreadEnvironment == false)
	{
		if (PhysicsScene != nullptr)
		{
			PhysicsScene->KillSafeAsyncTasks();
			PhysicsScene->WaitSolverTasks();

			if (PhysicsScene->AreAnyTasksPending())
			{
				return false;
			}
		}
	}

	return Super::IsReadyForFinishDestroy();
}

void UWorld::PostLoad()
{
	// By default, assume the world was loaded from a package to persist in memory
	WorldType = EWorldType::Inactive;

	if (!UWorld::WorldTypePreLoadMap.IsEmpty())
	{
		EWorldType::Type * PreLoadWorldType = UWorld::WorldTypePreLoadMap.Find(GetOuter()->GetFName());
		if (PreLoadWorldType)
		{
			WorldType = *PreLoadWorldType;
		}
		else
		{			
			for (const TPair<FName, EWorldType::Type>& WorldTypePair : UWorld::WorldTypePreLoadMap)
			{
				if (UPackage* WorldPackage = FindPackage(nullptr, *WorldTypePair.Key.ToString()))
				{
					if (FollowWorldRedirectorInPackage(WorldPackage) == this)
					{
						check(WorldType == WorldTypePair.Value);
						break;
					}
				}
			}
		}
	}

	Super::PostLoad();
#if WITH_EDITORONLY_DATA
	CurrentLevel = PersistentLevel;
#endif
#if WITH_EDITOR
	RepairSingletonActors();
	RepairStreamingLevels();
#endif

	for (auto It = StreamingLevels.CreateIterator(); It; ++It)
	{
		if (ULevelStreaming* const StreamingLevel = *It)
		{
			// Make sure that the persistent level isn't in this world's list of streaming levels.  This should
			// never really happen, but was needed in at least one observed case of corrupt map data.
			if (PersistentLevel && (StreamingLevel->GetWorldAsset() == this || StreamingLevel->GetLoadedLevel() == PersistentLevel))
			{
				// Remove this streaming level
				It.RemoveCurrent();
				MarkPackageDirty();
			}
			else
			{
				FStreamingLevelPrivateAccessor::OnLevelAdded(StreamingLevel);
				if (FStreamingLevelPrivateAccessor::UpdateTargetState(StreamingLevel))
				{
					StreamingLevelsToConsider.Add(StreamingLevel);
				}
			}
		}
		else
		{
			// Remove null streaming level entries (could be if level was saved with transient level streaming objects)
			It.RemoveCurrent();
		}
	}

	// Add the garbage collection callbacks
	FLevelStreamingGCHelper::AddGarbageCollectorCallback();

	// Initially set up the parameter collection list. This may be run again in UWorld::InitWorld but it's required here for some editor and streaming cases
	SetupParameterCollectionInstances();

	// Make sure the OnPostGC callback is registed for all worlds to avoid leaking items in the ParameterCollectionInstances array. Also done in InitWorld so all types of worlds (loaded & created) are covered. 
	if (!bIsThePostGCDelegateRegistered)
	{
		bIsThePostGCDelegateRegistered = true;
#if RHI_RESOURCE_PROVENANCE_ENABLED
		FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddUObject(this, &UWorld::OnPreGC);
#endif
		FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &UWorld::OnPostGC);
	}

#if WITH_EDITOR
	if (GIsEditor)
	{
		// Avoid renaming PIE worlds and Instanced worlds except for the new maps (/Temp/Untitled) to preserve naming behavior
		const bool bLoadedForDiff = GetPackage()->HasAnyPackageFlags(PKG_ForDiffing);
		if (!GetPackage()->HasAnyPackageFlags(PKG_PlayInEditor) && !bLoadedForDiff && (!IsInstanced() || GetPackage()->GetPathName().StartsWith(TEXT("/Temp/Untitled"))))
		{
			// Needed for VER_UE4_WORLD_NAMED_AFTER_PACKAGE. If this file was manually renamed outside of the editor, this is needed anyway
			const FString ShortPackageName = FPackageName::GetLongPackageAssetName(GetPackage()->GetName());
			OriginalWorldName = GetFName();
			if (GetName() != ShortPackageName)
			{
				// Do not go through UWorld::Rename as we do not want to go through map build data/external actors or hlod renaming in post load
				UObject::Rename(*ShortPackageName, NULL, REN_NonTransactional | REN_DontCreateRedirectors | REN_AllowPackageLinkerMismatch);
			}

			// Worlds are assets so they need RF_Public and RF_Standalone (for the editor)
			SetFlags(RF_Public | RF_Standalone);
		}

		// Ensure the DefaultBrush's model has the same outer as the default brush itself. Older packages erroneously stored this object as a top-level package
		if (ABrush* DefaultBrush = PersistentLevel->Actors.Num() < 2 ? NULL : Cast<ABrush>(PersistentLevel->Actors[1]))
		{
			if (UModel* Model = DefaultBrush->Brush)
			{
				if (Model->GetOuter() != DefaultBrush->GetOuter())
				{
					Model->Rename(TEXT("Brush"), DefaultBrush->GetOuter(), REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional | REN_AllowPackageLinkerMismatch);
				}
			}
		}

		// Make sure thumbnail info exists
		if ( !ThumbnailInfo )
		{
			ThumbnailInfo = NewObject<UWorldThumbnailInfo>(this, NAME_None, RF_Transactional);
		}
	}
#endif

	// Reset between worlds so that the metric is only relevant to the current world.
	ResetAverageRequiredTexturePoolSize();
}

#if WITH_EDITORONLY_DATA
void UWorld::AppendToClassSchema(FAppendToClassSchemaContext& Context)
{
	Super::AppendToClassSchema(Context);
	constexpr FGuid WorldCookGuid(0x4b58bae1, 0x85e04e88, 0xa8511c76, 0x1d656890);
	Context.Update(&WorldCookGuid, sizeof(WorldCookGuid));
}

void UWorld::DeclareConstructClasses(TArray<FTopLevelAssetPath>& OutConstructClasses, const UClass* SpecificSubclass)
{
	Super::DeclareConstructClasses(OutConstructClasses, SpecificSubclass);
	OutConstructClasses.Add(FTopLevelAssetPath(GEngine->WorldSettingsClass));
	OutConstructClasses.Add(FTopLevelAssetPath(UWorldThumbnailInfo::StaticClass()));
}
#endif

void UWorld::PreDuplicate(FObjectDuplicationParameters& DupParams)
{
	if (PersistentLevel)
	{
		PersistentLevel->PreDuplicate(DupParams);
	}
}

void UWorld::PreSaveRoot(FObjectPreSaveRootContext ObjectSaveContext)
{
	if (!ObjectSaveContext.IsFirstConcurrentSave())
	{
		return;
	}

#if WITH_EDITOR
	// Flush outstanding static mesh compilation to ensure that construction scripts are properly ran and not deferred prior to saving
	FStaticMeshCompilingManager::Get().FinishAllCompilation();

	// Execute all pending actor construction scripts
	FActorDeferredScriptManager::Get().FinishAllCompilation();

	if(!PersistentLevel->bAreComponentsCurrentlyRegistered)
	{
		// Even if the World has been CleanedUp we find ourselves in a state where we'll add the components back again (and clean it up afterwards), so we reset the CleanedUp state to allow registration of components to occur.
		bWorldWasCleanedUp = IsCleanedUp();
		ResetCleanedUpState();
	}
#endif

	PersistentLevel->PreSaveFromRoot(ObjectSaveContext);
}

void UWorld::PostSaveRoot( FObjectPostSaveRootContext ObjectSaveContext )
{
	Super::PostSaveRoot(ObjectSaveContext);
	if (!ObjectSaveContext.IsLastConcurrentSave())
	{
		return;
	}

	PersistentLevel->PostSaveFromRoot(ObjectSaveContext);

#if WITH_EDITOR
	if( ObjectSaveContext.IsCleanupRequired() )
	{
		if (!bWorldWasCleanedUp)
		{
			// If we resetted the CleanUp state to accommodate save we restore it here. 
			CleanupWorldTag = ++CleanupWorldGlobalTag;
		}
	}

	if (!ObjectSaveContext.IsProceduralSave() && !ObjectSaveContext.IsFromAutoSave())
	{
		// Once saved, OriginalWorldName must match World's name
		OriginalWorldName = GetFName();
	}
#endif
}

UWorld* UWorld::GetWorld() const
{
	// Arg... rather hacky, but it seems conceptually ok because the object passed in should be able to fetch the
	// non-const world it's part of.  That's not a mutable action (normally) on the object, as we haven't changed
	// anything.
	return const_cast<UWorld*>(this);
}

#if RHI_RESOURCE_PROVENANCE_ENABLED
namespace
{
	constexpr uint32 MPCGC_CollectionValid = 1u << 0;
	constexpr uint32 MPCGC_CollectionRooted = 1u << 1;
	constexpr uint32 MPCGC_CollectionStandalone = 1u << 2;
	constexpr uint32 MPCGC_CollectionPublic = 1u << 3;
	constexpr uint32 MPCGC_CollectionTransient = 1u << 4;
	constexpr uint32 MPCGC_CollectionBeginDestroyed = 1u << 5;
	constexpr uint32 MPCGC_CollectionFinishDestroyed = 1u << 6;
	constexpr uint32 MPCGC_InstanceRooted = 1u << 7;
	constexpr uint32 MPCGC_PartitionedWorld = 1u << 8;
	constexpr uint32 MPCGC_RuntimeCellWorld = 1u << 9;
	constexpr uint32 MPCGC_GameWorld = 1u << 10;
	constexpr uint32 MPCGC_InstanceBeginDestroyed = 1u << 11;

	uint32 BuildMPCGCProvenanceState(
		const UWorld* World,
		const UMaterialParameterCollectionInstance* Instance)
	{
		uint32 State = 0;
		const UMaterialParameterCollection* Collection = Instance ? Instance->GetCollection() : nullptr;

		if (Collection)
		{
			State |= MPCGC_CollectionValid;
			if (Collection->IsRooted()) State |= MPCGC_CollectionRooted;
			if (Collection->HasAnyFlags(RF_Standalone)) State |= MPCGC_CollectionStandalone;
			if (Collection->HasAnyFlags(RF_Public)) State |= MPCGC_CollectionPublic;
			if (Collection->HasAnyFlags(RF_Transient)) State |= MPCGC_CollectionTransient;
			if (Collection->HasAnyFlags(RF_BeginDestroyed)) State |= MPCGC_CollectionBeginDestroyed;
			if (Collection->HasAnyFlags(RF_FinishDestroyed)) State |= MPCGC_CollectionFinishDestroyed;
		}

		if (Instance)
		{
			if (Instance->IsRooted()) State |= MPCGC_InstanceRooted;
			if (Instance->HasAnyFlags(RF_BeginDestroyed)) State |= MPCGC_InstanceBeginDestroyed;
		}

		if (World)
		{
			if (World->IsPartitionedWorld()) State |= MPCGC_PartitionedWorld;
			if (World->IsGameWorld()) State |= MPCGC_GameWorld;
			if (World->PersistentLevel && World->PersistentLevel->IsWorldPartitionRuntimeCell())
			{
				State |= MPCGC_RuntimeCellWorld;
			}
		}

		return State;
	}

	int32 GetMPCGCProvenanceLimit()
	{
		return FMath::Clamp(
			CVarRHIResourceProvenanceMaxMPCGCInstancesPerWorld.GetValueOnGameThread(),
			1,
			4096);
	}
}
#endif

void UWorld::SetupParameterCollectionInstances()
{
	ULevel* Level = PersistentLevel;
	const bool bIsWorldPartitionRuntimeCell = Level && Level->IsWorldPartitionRuntimeCell();
	if (!bIsWorldPartitionRuntimeCell)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_World_SetupParameterCollectionInstances);

		// Create an instance for each parameter collection in memory
		for (UMaterialParameterCollection* CurrentCollection : TObjectRange<UMaterialParameterCollection>())
		{
			AddParameterCollectionInstance(CurrentCollection, false);
		}

		UpdateParameterCollectionInstances(false, false);
	}
}

void UWorld::AddParameterCollectionInstance(UMaterialParameterCollection* Collection, bool bUpdateScene)
{
	check(Collection);

	QUICK_SCOPE_CYCLE_COUNTER(STAT_World_AddParameterCollectionInstance);

	int32 ExistingIndex = INDEX_NONE;

	for (int32 InstanceIndex = 0; InstanceIndex < ParameterCollectionInstances.Num(); InstanceIndex++)
	{
		const UMaterialParameterCollectionInstance* Instance = ParameterCollectionInstances[InstanceIndex];
		if (Instance != nullptr && Instance->GetCollection() == Collection)
		{
			ExistingIndex = InstanceIndex;
			break;
		}
	}

	CreateParameterCollectionInstance(ExistingIndex, Collection, bUpdateScene);
}

UMaterialParameterCollectionInstance* UWorld::GetParameterCollectionInstance(const UMaterialParameterCollection* Collection) const
{
	if (!Collection)
	{
		return nullptr;
	}

	for (int32 InstanceIndex = 0; InstanceIndex < ParameterCollectionInstances.Num(); InstanceIndex++)
	{
		if (ParameterCollectionInstances[InstanceIndex]->GetCollection() == Collection)
		{
			return ParameterCollectionInstances[InstanceIndex];
		}
	}

	// Lazy create one if not found
	return const_cast<UWorld*>(this)->CreateParameterCollectionInstance(INDEX_NONE, const_cast<UMaterialParameterCollection*>(Collection), true);
}

void UWorld::UpdateParameterCollectionInstances(bool bUpdateInstanceUniformBuffers, bool bRecreateUniformBuffer)
{
	if (Scene)
	{
		TArray<FMaterialParameterCollectionInstanceResource*> InstanceResources;

		for (int32 InstanceIndex = 0; InstanceIndex < ParameterCollectionInstances.Num(); InstanceIndex++)
		{
			UMaterialParameterCollectionInstance* Instance = ParameterCollectionInstances[InstanceIndex];

			if (bUpdateInstanceUniformBuffers)
			{
				Instance->UpdateRenderState(bRecreateUniformBuffer);
			}
			else
			{
				checkf(!bRecreateUniformBuffer, TEXT("Recreate Uniform Buffer was requested but cannot be executed because bUpdateInstanceUniformBuffers was false"));
			}

			InstanceResources.Add(Instance->GetResource());
		}

		Scene->UpdateParameterCollections(InstanceResources);
	}
}

UMaterialParameterCollectionInstance* UWorld::CreateParameterCollectionInstance(int32 ExistingIndex, UMaterialParameterCollection* Collection, bool bUpdateScene)
{
	UMaterialParameterCollectionInstance* NewInstance = NewObject<UMaterialParameterCollectionInstance>();
	NewInstance->SetCollection(Collection, this);

	if (ExistingIndex != INDEX_NONE)
	{
		// Overwrite an existing instance
#if RHI_RESOURCE_PROVENANCE_ENABLED
		if (UMaterialParameterCollectionInstance* PreviousInstance = ParameterCollectionInstances[ExistingIndex])
		{
			if (FMaterialParameterCollectionInstanceResource* PreviousResource = PreviousInstance->GetResource())
			{
				PreviousResource->GameThread_RecordProvenanceReleaseCause(
					UE::RHI::ResourceProvenance::EReleaseCause::WorldReplacedMPCInstance,
					UE::RHI::ResourceProvenance::CaptureCallerAddress());
				PreviousResource->GameThread_RecordProvenanceReleaseOwner(FString::Printf(
					TEXT("cause=WorldReplacedMPCInstance world=%s old_instance=%s collection=%s"),
					*GetPathName(),
					*PreviousInstance->GetPathName(),
					*GetPathNameSafe(Collection)));
			}
		}
#endif
		ParameterCollectionInstances[ExistingIndex] = NewInstance;
	}
	else
	{
		// Add a new instance
		ParameterCollectionInstances.Add(NewInstance);
	}

	// Ensure the new instance creates initial render thread resources
	// This needs to happen right away, so they can be picked up by any cached shader bindings
	NewInstance->UpdateRenderState(true);

	if (bUpdateScene)
	{
		// Update the scene's list of instances, needs to happen to prevent a race condition with GC 
		// (rendering thread still uses the FMaterialParameterCollectionInstanceResource when GC deletes the UMaterialParameterCollectionInstance)
		// However, if UpdateParameterCollectionInstances is going to be called after many AddParameterCollectionInstance's, this can be skipped for now.
		UpdateParameterCollectionInstances(false, false);
	}

	return NewInstance;
}


void UWorld::OnPreGC()
{
#if RHI_RESOURCE_PROVENANCE_ENABLED
	const int32 RecordLimit = GetMPCGCProvenanceLimit();
	const uint64 CallerAddress = UE::RHI::ResourceProvenance::CaptureCallerAddress();
	int32 RecordedCount = 0;
	FMaterialParameterCollectionInstanceResource* FirstRecordedResource = nullptr;

	for (UMaterialParameterCollectionInstance* Instance : ParameterCollectionInstances)
	{
		if (RecordedCount >= RecordLimit || !Instance)
		{
			continue;
		}

		if (FMaterialParameterCollectionInstanceResource* Resource = Instance->GetResource())
		{
			Resource->GameThread_RecordProvenanceEvent(
				UE::RHI::ResourceProvenance::EOperation::GCPreSnapshot,
				BuildMPCGCProvenanceState(this, Instance),
				CallerAddress);
			FirstRecordedResource = FirstRecordedResource ? FirstRecordedResource : Resource;
			++RecordedCount;
		}
	}

	const int32 OmittedCount = ParameterCollectionInstances.Num() - RecordedCount;
	if (OmittedCount > 0 && FirstRecordedResource)
	{
		FirstRecordedResource->GameThread_RecordProvenanceEvent(
			UE::RHI::ResourceProvenance::EOperation::GCCoverageOmitted,
			static_cast<uint32>(OmittedCount),
			CallerAddress);
	}
#endif
}

void UWorld::OnPostGC()
{
#if RHI_RESOURCE_PROVENANCE_ENABLED
	const int32 RecordLimit = GetMPCGCProvenanceLimit();
	const uint64 CallerAddress = UE::RHI::ResourceProvenance::CaptureCallerAddress();
	int32 RecordedCount = 0;
	FMaterialParameterCollectionInstanceResource* FirstRecordedResource = nullptr;

	// Record invalid collections first so the bounded phase never spends its budget on
	// survivors while omitting the collection transition that caused a removal.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bRecordValidCollections = Pass == 1;
		for (UMaterialParameterCollectionInstance* Instance : ParameterCollectionInstances)
		{
			if (RecordedCount >= RecordLimit || !Instance)
			{
				continue;
			}

			const bool bCollectionValid = Instance->IsCollectionValid();
			if (bCollectionValid != bRecordValidCollections)
			{
				continue;
			}

			if (FMaterialParameterCollectionInstanceResource* Resource = Instance->GetResource())
			{
				Resource->GameThread_RecordProvenanceEvent(
					bCollectionValid
						? UE::RHI::ResourceProvenance::EOperation::GCPostSurvived
						: UE::RHI::ResourceProvenance::EOperation::GCPostCollected,
					BuildMPCGCProvenanceState(this, Instance),
					CallerAddress);
				FirstRecordedResource = FirstRecordedResource ? FirstRecordedResource : Resource;
				++RecordedCount;
			}
		}
	}

	const int32 OmittedCount = ParameterCollectionInstances.Num() - RecordedCount;
	if (OmittedCount > 0 && FirstRecordedResource)
	{
		FirstRecordedResource->GameThread_RecordProvenanceEvent(
			UE::RHI::ResourceProvenance::EOperation::GCCoverageOmitted,
			static_cast<uint32>(OmittedCount),
			CallerAddress);
	}
#endif

	bool bRemovedSomething = false;
	for (int32 InstanceIndex = ParameterCollectionInstances.Num()-1; InstanceIndex >= 0; InstanceIndex--)
	{
		if (!ParameterCollectionInstances[InstanceIndex]->IsCollectionValid())
		{
#if RHI_RESOURCE_PROVENANCE_ENABLED
			UMaterialParameterCollectionInstance* InvalidInstance = ParameterCollectionInstances[InstanceIndex];
			if (FMaterialParameterCollectionInstanceResource* InvalidResource = InvalidInstance->GetResource())
			{
				InvalidResource->GameThread_RecordProvenanceReleaseCause(
					UE::RHI::ResourceProvenance::EReleaseCause::WorldPostGCInvalidCollection,
					UE::RHI::ResourceProvenance::CaptureCallerAddress());
				InvalidResource->GameThread_RecordProvenanceReleaseOwner(FString::Printf(
					TEXT("cause=WorldPostGCInvalidCollection world=%s instance=%s index=%d"),
					*GetPathName(),
					*InvalidInstance->GetPathName(),
					InstanceIndex));
			}
#endif
			ParameterCollectionInstances.RemoveAt(InstanceIndex);
			bRemovedSomething = true;
		}
	}
	if (bRemovedSomething)
	{
		UpdateParameterCollectionInstances(false, false);
	}
}



UCanvas* UWorld::GetCanvasForRenderingToTarget()
{
	if (!CanvasForRenderingToTarget)
	{
		CanvasForRenderingToTarget = NewObject<UCanvas>(GetTransientPackage(), NAME_None);
	}

	return CanvasForRenderingToTarget;
}

UCanvas* UWorld::GetCanvasForDrawMaterialToRenderTarget()
{
	if (!CanvasForDrawMaterialToRenderTarget)
	{
		CanvasForDrawMaterialToRenderTarget = NewObject<UCanvas>(GetTransientPackage(), NAME_None);
	}

	return CanvasForDrawMaterialToRenderTarget;
}

void UWorld::GetCollisionProfileChannelAndResponseParams(FName ProfileName, ECollisionChannel& CollisionChannel, FCollisionResponseParams& ResponseParams)
{
	if (UCollisionProfile::GetChannelAndResponseParams(ProfileName, CollisionChannel, ResponseParams))
	{
		return;
	}

	// No profile found
	UE_LOGF(LogPhysics, Warning, "COLLISION PROFILE [%ls] is not found", *ProfileName.ToString());

	CollisionChannel = ECC_WorldStatic;
	ResponseParams = FCollisionResponseParams::DefaultResponseParam;
}

UAISystemBase* UWorld::CreateAISystem()
{
	const bool bShouldHaveAISystemInThisNetMode = UAISystemBase::ShouldInstantiateInNetMode(GetNetMode());

	// If we should not have an AI System in this NetMode, clean up possible one that could have been created already for this world BEFORE getting assigned a proper NetMode (could happens during Replays as follow:)	
	// 1. During replay, a client World gets initialized and as its NetMode still unset and a call to CreateAISystem() is made to create it's AISystem.
	// 2. Replay (demo) loads and calls UEngine::MovePendingLevel(FWorldContext &Context) which sets the Demo Net Driver that would have prevented the AISystem to get created initially.
	// 3. CreateAISystem() gets called again and will correctly not create AI System because conditions aren't met this time but we should be clearing any previously created AISystem.
	if (AISystem && !bShouldHaveAISystemInThisNetMode)
	{
		AISystem->CleanupWorld(/*bSessionEnded*/false, /*bCleanupResources*/true);
		AISystem = nullptr;
	}

	// create navigation system for editor and server targets, but remove it from game clients
	if (AISystem == nullptr && bShouldHaveAISystemInThisNetMode && PersistentLevel)
	{
		const FName AIModuleName = UAISystemBase::GetAISystemModuleName();
		const AWorldSettings* WorldSettings = PersistentLevel->GetWorldSettings(false);
		if (AIModuleName.IsNone() == false && WorldSettings && WorldSettings->IsAISystemEnabled())
		{
			IAISystemModule* AISystemModule = FModuleManager::LoadModulePtr<IAISystemModule>(AIModuleName);
			if (AISystemModule)
			{
				AISystem = AISystemModule->CreateAISystemInstance(this);
			}
		}
	}

	return AISystem; 
}

void UWorld::RepairChaosActors()
{
	if (!PhysicsScene_Chaos)
	{
		// Streamed levels need to find the persistent level's owning world to fetch chaos scene.
		UWorld *OwningWorld = ULevel::StreamedLevelsOwningWorld.FindRef(PersistentLevel->GetOutermost()->GetFName()).Get();
		if (OwningWorld)
		{
			PersistentLevel->OwningWorld = OwningWorld;
			PhysicsScene_Chaos = PersistentLevel->OwningWorld->PhysicsScene_Chaos;
		}
	}

	if (!PhysicsScene_Chaos)
	{
		FChaosSolversModule* ChaosModule = FChaosSolversModule::GetModule();
		check(ChaosModule);
		bool bHasChaosActor = false;
		for (int32 i = 0; i < PersistentLevel->Actors.Num(); ++i)
		{
			if (PersistentLevel->Actors[i] && ChaosModule->IsValidSolverActorClass(PersistentLevel->Actors[i]->GetClass()))
			{
				bHasChaosActor = true;

				bool bClearOwningWorld = false;

				if (PersistentLevel->OwningWorld == nullptr)
				{
					bClearOwningWorld = true;
					PersistentLevel->OwningWorld = this;
				}

				PersistentLevel->Actors[i]->SetHasActorRegisteredAllComponents();
				PersistentLevel->Actors[i]->PostRegisterAllComponents();

				if (bClearOwningWorld)
				{
					PersistentLevel->OwningWorld = nullptr;
				}

				break;
			}
		}
		if (!bHasChaosActor)
		{
			bool bClearOwningWorld = false;

			if (PersistentLevel->OwningWorld == nullptr)
			{
				bClearOwningWorld = true;
				PersistentLevel->OwningWorld = this;
			}

			FActorSpawnParameters ChaosSpawnInfo;
			ChaosSpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ChaosSpawnInfo.Name = TEXT("DefaultChaosActor");
			SpawnActor(ChaosModule->GetSolverActorClass(), nullptr, nullptr, ChaosSpawnInfo);
			check(PhysicsScene_Chaos);

			if (bClearOwningWorld)
			{
				PersistentLevel->OwningWorld = nullptr;
			}
		}
	}

	// make the current scene the default scene
	DefaultPhysicsScene_Chaos = PhysicsScene_Chaos;
}

UChaosEventRelay* UWorld::GetChaosEventRelay()
{
	if (PhysicsScene)
	{
		return PhysicsScene->GetChaosEventRelay();
	}
	return nullptr;
}

void UWorld::RepairStreamingLevels()
{
	for (int32 Index = 0; Index < StreamingLevels.Num(); )
	{
		ULevelStreaming* StreamingLevel = StreamingLevels[Index];
		if (StreamingLevel && !StreamingLevel->IsValidStreamingLevel())
		{
			StreamingLevels.RemoveAtSwap(Index);
		}
		else
		{
			++Index;
		}
	}
}

void UWorld::RepairSingletonActorOfClass(TSubclassOf<AActor> ActorClass)
{
	AActor* FoundActor = nullptr;

	for (int32 i=0; i<PersistentLevel->Actors.Num(); i++)
	{
		if (AActor* CurrentActor = PersistentLevel->Actors[i])
		{
			if (CurrentActor->IsA(ActorClass))
			{
				if (FoundActor)
				{
					if (FoundActor == CurrentActor)
					{
						UE_LOGF(LogWorld, Log, "Extra '%ls' actor found. Resave level %ls to clean up.", *CurrentActor->GetPathName(), *PersistentLevel->GetPathName());
						PersistentLevel->Actors[i] = nullptr;
					}
					else
					{
						UE_LOGF(LogWorld, Log, "Extra '%ls' actor found. Resave level %ls and actor to cleanup.", *CurrentActor->GetPathName(), *PersistentLevel->GetPathName());
						CurrentActor->Destroy();						
					}
				}
				else
				{
					FoundActor = CurrentActor;
				}
			}
		}
	}
}

void UWorld::RepairWorldSettings()
{
	AWorldSettings* ExistingWorldSettings = PersistentLevel->GetWorldSettings(false);

	if (ExistingWorldSettings == nullptr && PersistentLevel->Actors.Num() > 0)
	{
		ExistingWorldSettings = Cast<AWorldSettings>(PersistentLevel->Actors[0]);
		if (ExistingWorldSettings)
		{
			// This means the WorldSettings member just wasn't initialized, get that resolved
			PersistentLevel->SetWorldSettings(ExistingWorldSettings);
		}
	}

	// If for some reason we don't have a valid WorldSettings object go ahead and spawn one to avoid crashing.
	// This will generally happen if a map is being moved from a different project.
	if (ExistingWorldSettings == nullptr || ExistingWorldSettings->GetClass() != GEngine->WorldSettingsClass)
	{
		// Rename invalid WorldSettings to avoid name collisions
		if (ExistingWorldSettings)
		{
			ExistingWorldSettings->Rename(nullptr, PersistentLevel, REN_AllowPackageLinkerMismatch);
		}
		
		bool bClearOwningWorld = false;

		if (PersistentLevel->OwningWorld == nullptr)
		{
			bClearOwningWorld = true;
			PersistentLevel->OwningWorld = this;
		}

		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnInfo.Name = GEngine->WorldSettingsClass->GetFName();
		AWorldSettings* const NewWorldSettings = SpawnActor<AWorldSettings>( GEngine->WorldSettingsClass, SpawnInfo );

		// If there was an existing actor, copy its properties to the new actor (the it will be destroyed by SetWorldSettings)
		if (ExistingWorldSettings)
		{
			NewWorldSettings->UnregisterAllComponents();
			UEngine::FCopyPropertiesForUnrelatedObjectsParams CopyParams;
			CopyParams.bNotifyObjectReplacement = true;
			UEngine::CopyPropertiesForUnrelatedObjects(ExistingWorldSettings, NewWorldSettings, CopyParams);
			NewWorldSettings->RegisterAllComponents();
		}

		PersistentLevel->SetWorldSettings(NewWorldSettings);

		// Re-sort actor list as we just shuffled things around.
		PersistentLevel->SortActorList();

		if (bClearOwningWorld)
		{
			PersistentLevel->OwningWorld = nullptr;
		}
	}

	// Now that we have set the proper world settings, clean up any other stay that may have accumulated due to legacy behaviors
	if (PersistentLevel->Actors.Num() > 1)
	{
		for (int32 Index = 1, ActorNum = PersistentLevel->Actors.Num(); Index < ActorNum; ++Index)
		{
			AActor* Actor = PersistentLevel->Actors[Index];
			if (Actor != nullptr && Actor->IsA<AWorldSettings>())
			{
				UE_LOGF(LogWorld, Warning, "Extra World Settings '%ls' actor found. Resave level %ls to clean up.", *Actor->GetPathName(), *PersistentLevel->GetPathName());
				Actor->Destroy();
			}
		}
	}

	check(GetWorldSettings());
}

void UWorld::RepairSingletonActors()
{
	RepairWorldSettings();
}

#if WITH_EDITOR
void UWorld::RepairDefaultBrush()
{
	// See whether we're missing the default brush. It was possible in earlier builds to accidentally delete the default
	// brush of sublevels so we simply spawn a new one if we encounter it missing.
	ABrush* DefaultBrush = PersistentLevel->Actors.Num()<2 ? NULL : Cast<ABrush>(PersistentLevel->Actors[1]);
	if (GIsEditor)
	{
		if (!DefaultBrush || !DefaultBrush->IsStaticBrush() || DefaultBrush->BrushType != Brush_Default || !DefaultBrush->GetBrushComponent() ||
			!DefaultBrush->Brush || !DefaultBrush->Brush->Polys || DefaultBrush->Brush->Polys->Element.IsEmpty())
		{
			// Spawn the default brush.
			DefaultBrush = SpawnBrush();
			check(DefaultBrush->GetBrushComponent());
			DefaultBrush->Brush = NewObject<UModel>(DefaultBrush->GetOuter(), TEXT("Brush"));
			DefaultBrush->Brush->Initialize(DefaultBrush, true);
			DefaultBrush->GetBrushComponent()->Brush = DefaultBrush->Brush;
			DefaultBrush->SetNotForClientOrServer();
			DefaultBrush->Brush->SetFlags( RF_Transactional );
			DefaultBrush->Brush->Polys->SetFlags( RF_Transactional );

			// Create cube geometry.
			// We effectively replicate what is done in UEditorEngine::InitBuilderBrush() since we can't just use this function or the underlying
			// function UCubeBuilder::BuildCube() here directly.
			{
				constexpr float HalfSize = 128.0f;
				static const FVector3f Vertices[8] = {
					{-HalfSize, -HalfSize, -HalfSize},
					{-HalfSize, -HalfSize,  HalfSize},
					{-HalfSize,  HalfSize, -HalfSize},
					{-HalfSize,  HalfSize,  HalfSize},
					{ HalfSize, -HalfSize, -HalfSize},
					{ HalfSize, -HalfSize,  HalfSize},
					{ HalfSize,  HalfSize, -HalfSize},
					{ HalfSize,  HalfSize,  HalfSize}
				};

				constexpr int32 Indices[24] = {
					0, 1, 3, 2,
					2, 3, 7, 6,
					6, 7, 5, 4,
					4, 5, 1, 0,
					3, 1, 5, 7,
					0, 2, 6, 4
				};

				for (int32 i = 0; i < 6; ++i)
				{
					FPoly Poly;
					Poly.Init();
					Poly.Base = Vertices[0];

					for (int32 j = 0; j < 4; ++j)
					{
						new(Poly.Vertices) FVector3f(Vertices[Indices[i * 4 + j]]);
					}
					if (Poly.Finalize(DefaultBrush, 1) == 0)
					{
						new(DefaultBrush->Brush->Polys->Element)FPoly(Poly);
					}
				}

				DefaultBrush->Brush->BuildBound();
			}

			// The default brush is legacy but has to exist for some old bsp operations.  However it should not be interacted with in the editor. 
			DefaultBrush->SetIsTemporarilyHiddenInEditor(true);

			// Find the index in the array the default brush has been spawned at. Not necessarily
			// the last index as the code might spawn the default physics volume afterwards.
			const int32 DefaultBrushActorIndex = PersistentLevel->Actors.Find( DefaultBrush );

			// The default brush needs to reside at index 1.
			Exchange(PersistentLevel->Actors[1],PersistentLevel->Actors[DefaultBrushActorIndex]);

			// Re-sort actor list as we just shuffled things around.
			PersistentLevel->SortActorList();
		}
		else
		{
			// Ensure that the Brush and BrushComponent both point to the same model
			DefaultBrush->GetBrushComponent()->Brush = DefaultBrush->Brush;
		}

		// Reset the lightmass settings on the default brush; they can't be edited by the user but could have
		// been tainted if the map was created during a window where the memory was uninitialized
		if (DefaultBrush->Brush != NULL)
		{
			UModel* Model = DefaultBrush->Brush;

			const FLightmassPrimitiveSettings DefaultSettings;

			for (int32 i = 0; i < Model->LightmassSettings.Num(); ++i)
			{
				Model->LightmassSettings[i] = DefaultSettings;
			}

			if (Model->Polys != NULL) 
			{
				for (int32 i = 0; i < Model->Polys->Element.Num(); ++i)
				{
					Model->Polys->Element[i].LightmassSettings = DefaultSettings;
				}
			}
		}
	}
}
#endif

void UWorld::InitWorld(const InitializationValues IVS)
{
	if (!ensure(!bIsWorldInitialized))
	{
		return;
	}
	
	// Skip Rescan on any cooked build, and also on PIE worlds in editor as Rescan is already computed and its result duplicated in the PIE world
	if (WorldComposition && WorldType != EWorldType::PIE && !FPlatformProperties::RequiresCookedData())
	{
		WorldComposition->Rescan();
	}

	// This condition has to match the path that leads to GetRendererModule().AllocateScene below as that clobbers the Scene pointer
	// However, we must call this before starting to create subsystem uobjects and such.
	if (IVS.bInitializeScenes && GShouldInitWorldReleaseScene)
	{
		ReleaseScene();
	}
	// Reset flags in case of world reuse
	bIsLevelStreamingFrozen = false;
	bShouldForceUnloadStreamingLevels = false;
#if WITH_EDITOR
	ResetCleanedUpState();
#endif

	// Make sure the OnPostGC callback is registed for all worlds to avoid leaking items in the ParameterCollectionInstances array. Also done in PostLoad so all types of worlds (loaded & created) are covered. 
	if (!bIsThePostGCDelegateRegistered)
	{
		bIsThePostGCDelegateRegistered = true;
#if RHI_RESOURCE_PROVENANCE_ENABLED
		FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddUObject(this, &UWorld::OnPreGC);
#endif
		FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &UWorld::OnPostGC);
	}

	InitializeSubsystems();

	FWorldDelegates::OnPreWorldInitialization.Broadcast(this, IVS);

	AWorldSettings* WorldSettings = GetWorldSettings();
	if (IVS.bInitializeScenes)
	{

	#if WITH_EDITOR
		bEnableTraceCollision = IVS.bEnableTraceCollision;
		bForceUseMovementComponentInNonGameWorld = IVS.bForceUseMovementComponentInNonGameWorld;
	#endif


		if (IVS.bCreatePhysicsScene)
		{
			// Create the physics scene
			CreatePhysicsScene(WorldSettings);
		}

		bShouldSimulatePhysics = IVS.bShouldSimulatePhysics;
		
		// Save off the value used to create the scene, so this UWorld can recreate its scene later
		bRequiresHitProxies = IVS.bRequiresHitProxies;
		bAllowLumenPrimitiveTrackingInPreviewWorld = IVS.bAllowLumenPrimitiveTrackingInPreviewWorld;
		GetRendererModule().AllocateScene(this, bRequiresHitProxies, IVS.bCreateFXSystem, GetFeatureLevel());
	}

#if WITH_STATE_STREAM
	StateStreamManager = GetRendererModule().GetStateStreamManager();
	LaneId = StateStreamManager->Game_CreateLane();
	StateStreamManager->Game_SetLaneUserData(LaneId, Scene);
	StateStreamManager->Game_BeginTick(LaneId);
#endif

	// Prepare AI systems
	if (WorldSettings)
	{
		if (IVS.bCreateNavigation || IVS.bCreateAISystem)
		{
			if (IVS.bCreateNavigation)
			{
				FNavigationSystem::AddNavigationSystemToWorld(*this, FNavigationSystemRunMode::InvalidMode, WorldSettings->GetNavigationSystemConfig(), /*bInitializeForWorld=*/false);
			}
			if (IVS.bCreateAISystem && WorldSettings->IsAISystemEnabled())
			{
				CreateAISystem();
			}
		}
	}
	
	if (GEngine->AvoidanceManagerClass != NULL)
	{
		AvoidanceManager = NewObject<UAvoidanceManager>(this, GEngine->AvoidanceManagerClass);
	}

	SetupParameterCollectionInstances();

	if (PersistentLevel->GetOuter() != this)
	{
		// Move persistent level into world so the world object won't get garbage collected in the multi- level
		// case as it is still referenced via the level's outer. This is required for multi- level editing to work.
		PersistentLevel->Rename(*PersistentLevel->GetName(), this, REN_AllowPackageLinkerMismatch);
	}

	Levels.Empty(1);
	Levels.Add( PersistentLevel );
	
	// If we are not Seamless Traveling remove PersistentLevel from LevelCollection if it is in a collection
	// The Level Collections will be filled already during Seamless Travel in 
	// UWorld::AsyncLoadAlwaysLoadedLevelsForSeamlessTravel()
	if (GEngine->GetWorldContextFromWorld(this) && !IsInSeamlessTravel())  
	{
		if (FLevelCollection* Collection = PersistentLevel->GetCachedLevelCollection())
		{
			Collection->RemoveLevel(PersistentLevel);
		}
	}
	
	PersistentLevel->OwningWorld = this;
	PersistentLevel->bIsVisible = true;

#if WITH_EDITOR
	RepairSingletonActors();
	RepairStreamingLevels();
#endif

	// initialize DefaultPhysicsVolume for the world
	// Spawned on demand by this function.
	DefaultPhysicsVolume = GetDefaultPhysicsVolume();

	// Find gravity
	if (GetPhysicsScene())
	{
		FVector Gravity = FVector( 0.f, 0.f, GetGravityZ() );
		GetPhysicsScene()->SetUpForFrame( &Gravity, 0, 0, 0, 0, 0, false);
	}

	// Create physics collision handler, if we have a physics scene
	if (IVS.bCreatePhysicsScene)
	{
		// First look for world override
		TSubclassOf<UPhysicsCollisionHandler> PhysHandlerClass = (WorldSettings ? WorldSettings->GetPhysicsCollisionHandlerClass() : nullptr);
		// Then fall back to engine default
		if(PhysHandlerClass == NULL)
		{
			PhysHandlerClass = GEngine->PhysicsCollisionHandlerClass;
		}

		if (PhysHandlerClass != NULL)
		{
			PhysicsCollisionHandler = NewObject<UPhysicsCollisionHandler>(this, PhysHandlerClass);
			PhysicsCollisionHandler->InitCollisionHandler();
		}
	}

	URL					= PersistentLevel->URL;
#if WITH_EDITORONLY_DATA
	CurrentLevel		= PersistentLevel;
#endif

	bAllowAudioPlayback = IVS.bAllowAudioPlayback;
	bDoDelayedUpdateCullDistanceVolumes = false;

#if WITH_EDITOR
	RepairDefaultBrush();

	if (!IsRunningCookCommandlet())
	{
		// invalidate lighting if VT is enabled but no valid VT data is present or VT is disabled and no valid non-VT data is present.
		for (auto Level : Levels) //Note: PersistentLevel is part of this array
		{
			if (Level && Level->MapBuildData)
			{
				if (Level->MapBuildData->IsLightingValid(GetFeatureLevel()) == false)
				{
					Level->MapBuildData->InvalidateSurfaceLightmaps(this);
				}
			}
		}
	}

#endif // WITH_EDITOR

	// update it's bIsDefaultLevel
	bIsDefaultLevel = (FPaths::GetBaseFilename(GetMapName()) == FPaths::GetBaseFilename(UGameMapsSettings::GetGameDefaultMap()));

	ConditionallyCreateDefaultLevelCollections();

	// We're initialized now.
	bIsWorldInitialized = true;
	bHasEverBeenInitialized = true;

	// Call the general post initialization delegates
	FWorldDelegates::OnPostWorldInitialization.Broadcast(this, IVS);

	PersistentLevel->PrecomputedVisibilityHandler.UpdateScene(Scene);
	PersistentLevel->PrecomputedVolumeDistanceField.UpdateScene(Scene);
	PersistentLevel->InitializeRenderingResources();
	PersistentLevel->OnLevelLoaded();

	IStreamingManager::Get().AddLevel(PersistentLevel);

	PostInitializeSubsystems();

	BroadcastLevelsChanged();

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().AssetTagsFinalized(*this);
}

#if WITH_EDITOR
void UWorld::ReInitWorld()
{
	if (!IsInitialized())
	{
		UE_LOGF(LogWorld, Warning, "ReInitWorld called on World %ls, but it is not yet initialized. ReInitWorld will be ignored.", *GetPathName());
		return;
	}

	InitializationValues IVS;
	IVS.bInitializeScenes = Scene != nullptr;
	IVS.bAllowAudioPlayback = bAllowAudioPlayback;
	IVS.bRequiresHitProxies = bRequiresHitProxies;
	IVS.bCreatePhysicsScene = PhysicsScene != nullptr;
	IVS.bCreateNavigation = NavigationSystem != nullptr;
	IVS.bCreateAISystem = AISystem != nullptr;
	IVS.bShouldSimulatePhysics = bShouldSimulatePhysics;
	IVS.bEnableTraceCollision = bEnableTraceCollision;
	IVS.bForceUseMovementComponentInNonGameWorld = bForceUseMovementComponentInNonGameWorld;
	IVS.bTransactional = HasAnyFlags(RF_Transactional);
	IVS.bCreateFXSystem = FXSystem != nullptr;
	IVS.bAllowLumenPrimitiveTrackingInPreviewWorld = bAllowLumenPrimitiveTrackingInPreviewWorld;
	// IVS.bCreateWorldPartition; // Only used in InitializeNewWorld, not needed by InitWorld
	// IVS.DefaultGameMode; // Only used inInitializeNewWorld, not needed by InitWorld

	CleanupWorld();
	InitWorld(IVS);
	RefreshStreamingLevels();
	UpdateWorldComponents(false /* bRerunConstructionScripts */, false /* bCurrentLevelOnly */);
}

const FName UWorld::KeepInitializedDuringLoadTag(TEXT("KeepInitializedDuringLoadTag"));
#endif

void UWorld::ConditionallyCreateDefaultLevelCollections()
{
	if (WorldType == EWorldType::Inactive)
	{
		return;
	}
	else if (bCreateStaticLevelCollection)
	{
		LevelCollections.Reserve((int32)ELevelCollectionType::MAX);
	}
	else
	{
		LevelCollections.Reserve(1);
	}

	// Always create and update the main level collection. The persistent level will always be considered dynamic.
	ActiveLevelCollectionIndex = FindOrAddCollectionByType_Index(ELevelCollectionType::DynamicSourceLevels);
	LevelCollections[ActiveLevelCollectionIndex].SetPersistentLevel(PersistentLevel);
		
	// Don't add the persistent level if it is already a member of another collection.
	// This may be the case if, for example, this world is the outer of a streaming level,
	// in which case the persistent level may be in one of the collections in the streaming level's OwningWorld.
	if (PersistentLevel->GetCachedLevelCollection() == nullptr)
	{
		LevelCollections[ActiveLevelCollectionIndex].AddLevel(PersistentLevel);
	}

	// Optionally create the static level collection, this is not needed for normal operation
	if (bCreateStaticLevelCollection && !FindCollectionByType(ELevelCollectionType::StaticLevels))
	{
		FLevelCollection& StaticCollection = FindOrAddCollectionByType(ELevelCollectionType::StaticLevels);
		StaticCollection.SetPersistentLevel(PersistentLevel);
	}
}

void UWorld::InitializeNewWorld(const InitializationValues IVS, bool bInSkipInitWorld)
{
	if (!IVS.bTransactional)
	{
		ClearFlags(RF_Transactional);
	}

	PersistentLevel = NewObject<ULevel>(this, TEXT("PersistentLevel"));
	PersistentLevel->Initialize(FURL(nullptr));
	PersistentLevel->Model = NewObject<UModel>(PersistentLevel);
	PersistentLevel->Model->Initialize(nullptr, 1);
	PersistentLevel->OwningWorld = this;

	// Create the WorldInfo actor.
	FActorSpawnParameters SpawnInfo; 

	// Mark objects are transactional for undo/ redo.
	if (IVS.bTransactional)
	{
		SpawnInfo.ObjectFlags |= RF_Transactional;
		PersistentLevel->SetFlags( RF_Transactional );
		PersistentLevel->Model->SetFlags( RF_Transactional );
	}
	else
	{
		SpawnInfo.ObjectFlags &= ~RF_Transactional;
		PersistentLevel->ClearFlags( RF_Transactional );
		PersistentLevel->Model->ClearFlags( RF_Transactional );
	}

#if WITH_EDITORONLY_DATA
	// Need to associate current level so SpawnActor doesn't complain.
	CurrentLevel = PersistentLevel;
#endif

	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Set constant name for WorldSettings to make a network replication work between new worlds on host and client
	SpawnInfo.Name = GEngine->WorldSettingsClass->GetFName();
	AWorldSettings* WorldSettings = SpawnActor<AWorldSettings>(GEngine->WorldSettingsClass, SpawnInfo );

	// Allow the world creator to override the default game mode in case they do not plan to load a level.
	if (IVS.DefaultGameMode)
	{
		WorldSettings->DefaultGameMode = IVS.DefaultGameMode;
	}

	PersistentLevel->SetWorldSettings(WorldSettings);
	check(GetWorldSettings());
#if WITH_EDITOR
	WorldSettings->SetIsTemporarilyHiddenInEditor(true);

	// Check if newly created world should be partitioned
	if (IVS.bCreateWorldPartition)
	{
		// World partition always uses actor folder objects
		FLevelActorFoldersHelper::SetUseActorFolders(PersistentLevel, true);
		PersistentLevel->ConvertAllActorsToPackaging(true);
		
		check(!GetStreamingLevels().Num());
		
		UWorldPartition* WorldPartition = UWorldPartition::CreateOrRepairWorldPartition(WorldSettings);
		WorldPartition->bEnableStreaming = IVS.bEnableWorldPartitionStreaming;
	}
#endif

	if (!bInSkipInitWorld)
	{
		// If this isn't set, the PerfTrackers are already allocated in the constructor
		if (bDisableInGamePerfTrackersForUninitializedWorlds && !PerfTrackers)
		{
			PerfTrackers = new FWorldInGamePerformanceTrackers();
		}

		// Initialize the world
		InitWorld(IVS);

		// Update components.
		const bool bRerunConstructionScripts = !FPlatformProperties::RequiresCookedData();
		UpdateWorldComponents(bRerunConstructionScripts, false);
	}
}


void UWorld::DestroyWorld( bool bInformEngineOfWorld, UWorld* NewWorld )
{
	// Clean up existing world and remove it from root set so it can be garbage collected.
	bIsLevelStreamingFrozen = false;
	SetShouldForceUnloadStreamingLevels(true);
	FlushLevelStreaming();
	CleanupWorld(true, true, NewWorld);

	ForEachNetDriver(GEngine, this, [](UNetDriver* const Driver)
	{
		if (Driver != nullptr)
		{
			check(Driver->GetNetworkObjectList().GetAllObjects().Num() == 0);
			check(Driver->GetNetworkObjectList().GetActiveObjects().Num() == 0);
		}
	});

	// Tell the engine we are destroying the world.(unless we are asked not to)
	if( ( GEngine ) && ( bInformEngineOfWorld == true ) )
	{
		GEngine->WorldDestroyed( this );
	}		
	RemoveFromRoot();
	ClearFlags(RF_Standalone);
	
	for (int32 LevelIndex=0; LevelIndex < GetNumLevels(); ++LevelIndex)
	{
		UWorld* World = CastChecked<UWorld>(GetLevel(LevelIndex)->GetOuter());
		if (World != this && World != NewWorld)
		{
			World->ClearFlags(RF_Standalone);
		}
	}
}

void UWorld::MarkObjectsPendingKill()
{
	auto MarkObjectPendingKill = [](UObject* Object)
	{
		Object->MarkAsGarbage();
	};
	ForEachObjectWithOuter(this, MarkObjectPendingKill, EGetObjectsFlags::IncludeNestedObjects, RF_NoFlags, EInternalObjectFlags::Garbage);

	MarkAsGarbage();
	bMarkedObjectsPendingKill = true;
}

UWorld* UWorld::CreateWorld(const EWorldType::Type InWorldType, bool bInformEngineOfWorld, FName WorldName, UPackage* InWorldPackage, bool bAddToRoot, ERHIFeatureLevel::Type InFeatureLevel, const InitializationValues* InIVS, bool bInSkipInitWorld)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorld::CreateWorld);

	if (InFeatureLevel >= ERHIFeatureLevel::Num)
	{
		InFeatureLevel = GMaxRHIFeatureLevel;
	}

	UPackage* WorldPackage = InWorldPackage;
	if ( !WorldPackage )
	{
		WorldPackage = CreatePackage(nullptr);
	}

	if (InWorldType == EWorldType::PIE)
	{
		WorldPackage->SetPackageFlags(PKG_PlayInEditor);
	}

	// Mark the package as containing a world.  This has to happen here rather than at serialization time,
	// so that e.g. the referenced assets browser will work correctly.
	if ( WorldPackage != GetTransientPackage() )
	{
		WorldPackage->ThisContainsMap();
	}

	// Create new UWorld, ULevel and UModel.
	const FString WorldNameString = (WorldName != NAME_None) ? WorldName.ToString() : TEXT("Untitled");
	UWorld* NewWorld = NewObject<UWorld>(WorldPackage, *WorldNameString);
	NewWorld->SetFlags(RF_Transactional);
	NewWorld->WorldType = InWorldType;
	NewWorld->SetFeatureLevel(InFeatureLevel);
	NewWorld->InitializeNewWorld(InIVS ? *InIVS : UWorld::InitializationValues().CreatePhysicsScene(InWorldType != EWorldType::Inactive).ShouldSimulatePhysics(false).EnableTraceCollision(true).CreateNavigation(InWorldType == EWorldType::Editor).CreateAISystem(InWorldType == EWorldType::Editor), bInSkipInitWorld);

	// Clear the dirty flags set during SpawnActor and UpdateLevelComponents
	WorldPackage->SetDirtyFlag(false);
	for (UPackage* ExternalPackage : WorldPackage->GetExternalPackages())
	{
		ExternalPackage->SetDirtyFlag(false);
	}

	if ( bAddToRoot )
	{
		// Add to root set so it doesn't get garbage collected.
		NewWorld->AddToRoot();
	}

	// Tell the engine we are adding a world (unless we are asked not to)
	if( ( GEngine ) && ( bInformEngineOfWorld == true ) )
	{
		GEngine->WorldAdded( NewWorld );
	}

	return NewWorld;
}

void UWorld::RemoveActor(AActor* Actor, bool bShouldModifyLevel) const
{
	if (ULevel* CheckLevel = Actor->GetLevel())
	{
		const int32 ActorListIndex = CheckLevel->Actors.Find(Actor);
		// Search the entire list.
		if (ActorListIndex != INDEX_NONE)
		{
			if (bShouldModifyLevel && GUndo)
			{
				ModifyLevel(CheckLevel);
			}

			if (!IsGameWorld())
			{
				CheckLevel->Actors[ActorListIndex]->Modify();
			}

			CheckLevel->Actors[ActorListIndex] = nullptr;

			LLM_SCOPE_DYNAMIC_STAT_OBJECTPATH(GetPackage(), ELLMTagSet::Assets);
			LLM_SCOPE_DYNAMIC_STAT_OBJECTPATH(GetClass(), ELLMTagSet::AssetClasses);
			UE_TRACE_METADATA_SCOPE_ASSET(this, GetClass());
			CheckLevel->ActorsForGC.RemoveSwap(Actor);
		}
	}

	// Remove actor from network list
	RemoveNetworkActor( Actor );
}


bool UWorld::ContainsActor( AActor* Actor ) const
{
	return (Actor && Actor->GetWorld() == this);
}

bool UWorld::AllowAudioPlayback() const
{
	return bAllowAudioPlayback;
}

#if WITH_EDITOR
void UWorld::ShrinkLevel()
{
	GetModel()->ShrinkModel();
}
#endif // WITH_EDITOR

void UWorld::ClearWorldComponents()
{
	TGuardValue<bool> IsBeingCleanedUp(bIsBeingCleanedUp, true);
	
	for( int32 LevelIndex=0; LevelIndex<Levels.Num(); LevelIndex++ )
	{
		ULevel* Level = Levels[LevelIndex];
		Level->ClearLevelComponents();
	}

	for (TObjectPtr<ULineBatchComponent>& LineBatcher : LineBatchers)
	{
		if (LineBatcher && LineBatcher->IsRegistered())
		{
			LineBatcher->UnregisterComponent();
		}
	}

	if (PhysicsField && PhysicsField->IsRegistered())
	{
		PhysicsField->UnregisterComponent();
	}
}


void UWorld::UpdateWorldComponents(bool bRerunConstructionScripts, bool bCurrentLevelOnly, FRegisterComponentContext* Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorld::UpdateWorldComponents);

#if !WITH_EDITOR
	ensure(!bRerunConstructionScripts);
#endif

	if ( !IsRunningDedicatedServer() )
	{
		for (TObjectPtr<ULineBatchComponent>& LineBatcher : LineBatchers)
		{
			if (!LineBatcher)
			{
				LineBatcher = NewObject<ULineBatchComponent>();
				LineBatcher->bCalculateAccurateBounds = false;
			}
		}

		for (TObjectPtr<ULineBatchComponent>& LineBatcher : LineBatchers)
		{
			if (!LineBatcher->IsRegistered())
			{
				LineBatcher->RegisterComponentWithWorld(this, Context);
			}
		}

		static IConsoleVariable* PhysicsFieldEnableClipmapCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PhysicsField.EnableField"));
		if (PhysicsFieldEnableClipmapCVar && PhysicsFieldEnableClipmapCVar->GetInt() == 1)
		{
			if (!PhysicsField)
			{
				PhysicsField = NewObject<UPhysicsFieldComponent>();
			}

			if (!PhysicsField->IsRegistered())
			{
				PhysicsField->RegisterComponentWithWorld(this, Context);
			}
		}
	}

	if ( bCurrentLevelOnly )
	{
#if !WITH_EDITORONLY_DATA
		ULevel* CurrentLevel = PersistentLevel;
#endif
		check( CurrentLevel );
		CurrentLevel->UpdateLevelComponents(bRerunConstructionScripts, Context);
	}
	else
	{
		for( int32 LevelIndex=0; LevelIndex<Levels.Num(); LevelIndex++ )
		{
			ULevel* Level = Levels[LevelIndex];
			ULevelStreaming* StreamingLevel = FLevelUtils::FindStreamingLevel(Level);
			// Update the level only if it is visible (or not a streamed level)
			if(!StreamingLevel || Level->bIsVisible)
			{
				Level->UpdateLevelComponents(bRerunConstru…38024 tokens truncated…ndSetNoPawnPlayerController(ChildConnection, NetPlayerIndex, LocalPlayerIdentifier);

#if WITH_EDITORONLY_DATA
						check(CurrentLevel);
#else
						ULevel* CurrentLevel = PersistentLevel;
#endif

						ChildConnection->SetClientWorldPackageName(CurrentLevel->GetOutermost()->GetFName());
					}
				}
				else
				{
					UE_LOGF(LogNet, Log, "Server doesn't support NMT_JoinNoPawnSplit (enable with UNetDriver::SetSupportForNoPawnConnection() or net.EnableSupportForNoPawnConnection).");
					FString Msg("Server doesn't support NMT_JoinNoPawnSplit.");
					Connection->SendCloseReason(ENetCloseResult::JoinFailure);
					FNetControlMessage<NMT_Failure>::Send(Connection, Msg);
					Connection->FlushNet(true);
					Connection->Close(ENetCloseResult::JoinFailure);
				}

				break;
			}
			case NMT_PCSwap:
			{
				UNetConnection* SwapConnection = Connection;
				int32 ChildIndex;

				if (FNetControlMessage<NMT_PCSwap>::Receive(Bunch, ChildIndex))
				{
					if (ChildIndex >= 0)
					{
						SwapConnection = Connection->Children.IsValidIndex(ChildIndex) ? ToRawPtr(Connection->Children[ChildIndex]) : nullptr;
					}

					bool bSuccess = false;

					if (SwapConnection != nullptr)
					{
						bSuccess = DestroySwappedPC(SwapConnection);
					}

					if (!bSuccess)
					{
						UE_LOGF(LogNet, Log, "Received invalid swap message with child index %i", ChildIndex);
					}
				}

				break;
			}
			case NMT_DebugText:
			{
				// debug text message
				FString Text;

				if (FNetControlMessage<NMT_DebugText>::Receive(Bunch, Text))
				{
					UE_LOGF(LogNet, Log, "%ls received NMT_DebugText Text=[%ls] Desc=%ls DescRemote=%ls",
							*Connection->Driver->GetDescription(), *Text, *Connection->LowLevelDescribe(),
							ToCStr(Connection->LowLevelGetRemoteAddress(true)));
				}

				break;
			}
		}
	}
}

bool UWorld::PreLoginCheckError(UNetConnection* Connection, const FString& ErrorMsg)
{
	if (Connection)
	{
		if (ErrorMsg.IsEmpty())
		{
			return true;
		}

		UE_LOGF(LogNet, Log, "PreLogin failure: %ls", *ErrorMsg);
		NETWORK_PROFILER(GNetworkProfiler.TrackEvent(TEXT("PRELOGIN FAILURE"), *ErrorMsg, Connection));
		Connection->SendCloseReason(ENetCloseResult::PreLoginFailure);
		FString ErrorMsgCopy(ErrorMsg); // Needed because Send() can't handle const FString.
		FNetControlMessage<NMT_Failure>::Send(Connection, ErrorMsgCopy);
		Connection->FlushNet(true);
		Connection->Close(ENetCloseResult::PreLoginFailure);
	}
	else
	{
		UE_LOGF(LogNet, Log, "PreLogin failure: connection was null");
	}

	return false;
}

void UWorld::PreLoginComplete(const FString& ErrorMsg, TWeakObjectPtr<UNetConnection> WeakConnection)
{
	UNetConnection* Connection = WeakConnection.Get();
	if (!PreLoginCheckError(Connection, ErrorMsg))
	{
		return;
	}

	WelcomePlayer(Connection);
}

void UWorld::PreLoginCompleteSplit(const FString& ErrorMsg, TWeakObjectPtr<UNetConnection> WeakConnection, FUniqueNetIdRepl SplitRequestUniqueIdRepl, FString SplitRequestURL)
{
	UNetConnection* Connection = WeakConnection.Get();
	if (!PreLoginCheckError(Connection, ErrorMsg))
	{
		return;
	}

	// create a child network connection using the existing connection for its parent
	check(Connection->GetUChildConnection() == NULL);
#if WITH_EDITORONLY_DATA
	check(CurrentLevel);
#else
	ULevel* CurrentLevel = PersistentLevel;
#endif

	int32 NetPlayerIndex = 0;
	UChildConnection* ChildConn = Connection->CreateChild(&NetPlayerIndex);
	ChildConn->PlayerId = SplitRequestUniqueIdRepl;
	ChildConn->SetPlayerOnlinePlatformName(Connection->GetPlayerOnlinePlatformName());
	ChildConn->RequestURL = SplitRequestURL;
	ChildConn->SetClientWorldPackageName(CurrentLevel->GetOutermost()->GetFName());

	// create URL from string
	FURL JoinSplitURL(NULL, *SplitRequestURL, TRAVEL_Absolute);

	UE::Private::World::ParseAndSetClientHandshakeId(JoinSplitURL, ChildConn);

	const int32 LocalPlayerIdentifier = UE::Private::World::GetLocalPlayerIdentifier(JoinSplitURL);

	UE_LOGF(LogNet, Log, "JOINSPLIT: Join request: URL=%ls", *JoinSplitURL.ToString());
	FString SpawnErrorMsg;
	APlayerController* PC = SpawnPlayActor(ChildConn, ROLE_AutonomousProxy, JoinSplitURL, ChildConn->PlayerId, SpawnErrorMsg, NetPlayerIndex, LocalPlayerIdentifier);
	if (PC == NULL)
	{
		// Failed to connect.
		UE_LOGF(LogNet, Log, "JOINSPLIT: Join failure: %ls", *SpawnErrorMsg);
		NETWORK_PROFILER(GNetworkProfiler.TrackEvent(TEXT("JOINSPLIT FAILURE"), *SpawnErrorMsg, Connection));
		// remove the child connection
		Connection->Children.Remove(ChildConn);

		// if any splitscreen viewport fails to join, all viewports on that client also fail
		Connection->SendCloseReason(ENetCloseResult::JoinSplitFailure);
		FNetControlMessage<NMT_Failure>::Send(Connection, SpawnErrorMsg);
		Connection->FlushNet(true);
		Connection->Close(ENetCloseResult::JoinSplitFailure);
	}
	else
	{
		// Successfully spawned in game.
		UE_LOGF(LogNet, Log, "JOINSPLIT: Succeeded: %ls PlayerId: %ls",
			*ChildConn->PlayerController->PlayerState->GetPlayerName(),
			*ChildConn->PlayerController->PlayerState->GetUniqueId().ToDebugString());
	}
}

bool UWorld::Listen( FURL& InURL )
{
#if WITH_SERVER_CODE
	LLM_SCOPE(ELLMTag::Networking);

	if( NetDriver )
	{
		GEngine->BroadcastNetworkFailure(this, NetDriver, ENetworkFailure::NetDriverAlreadyExists);
		return false;
	}

	// Create net driver.
	FName NetDriverDefinition = NAME_GameNetDriver; 
	if (InURL.HasOption(TEXT("NetDriverDef=")))
	{
		NetDriverDefinition = InURL.GetOption(TEXT("NetDriverDef="), nullptr);
	}

	if (GEngine->CreateNamedNetDriver(this, NAME_GameNetDriver, NetDriverDefinition))
	{
		NetDriver = GEngine->FindNamedNetDriver(this, NAME_GameNetDriver);
		NetDriver->SetWorld(this);
		FLevelCollection* const SourceCollection = FindCollectionByType(ELevelCollectionType::DynamicSourceLevels);
		if (SourceCollection)
		{
			SourceCollection->SetNetDriver(NetDriver);
		}
		FLevelCollection* const StaticCollection = FindCollectionByType(ELevelCollectionType::StaticLevels);
		if (StaticCollection)
		{
			StaticCollection->SetNetDriver(NetDriver);
		}
	}

	if (NetDriver == nullptr)
	{
		GEngine->BroadcastNetworkFailure(this, NULL, ENetworkFailure::NetDriverCreateFailure);
		return false;
	}

	AWorldSettings* WorldSettings = GetWorldSettings();
	const bool bReuseAddressAndPort = WorldSettings ? WorldSettings->bReuseAddressAndPort : false;

	FString Error;
	if( !NetDriver->InitListen( this, InURL, bReuseAddressAndPort, Error ) )
	{
		GEngine->BroadcastNetworkFailure(this, NetDriver, ENetworkFailure::NetDriverListenFailure, Error);
		UE_LOGF(LogWorld, Log,  "Failed to listen: %ls", *Error );
		GEngine->DestroyNamedNetDriver(this, NetDriver->NetDriverName);
		NetDriver = nullptr;
		FLevelCollection* SourceCollection = FindCollectionByType(ELevelCollectionType::DynamicSourceLevels);
		if (SourceCollection)
		{
			SourceCollection->SetNetDriver(nullptr);
		}
		FLevelCollection* StaticCollection = FindCollectionByType(ELevelCollectionType::StaticLevels);
		if (StaticCollection)
		{
			StaticCollection->SetNetDriver(nullptr);
		}
		return false;
	}
	static const bool bLanPlay = FParse::Param(FCommandLine::Get(),TEXT("lanplay"));
	const bool bLanSpeed = bLanPlay || InURL.HasOption(TEXT("LAN"));
	if ( !bLanSpeed && (NetDriver->MaxInternetClientRate < NetDriver->MaxClientRate) && (NetDriver->MaxInternetClientRate > 2500) )
	{
		NetDriver->MaxClientRate = NetDriver->MaxInternetClientRate;
	}

	NextSwitchCountdown = NetDriver->ServerTravelPause;
	return true;
#else
	return false;
#endif // WITH_SERVER_CODE
}

bool UWorld::IsPlayingReplay() const
{
	return (DemoNetDriver && DemoNetDriver->IsPlaying());
}

bool UWorld::IsRecordingReplay() const
{
	const UGameInstance* GameInst = GetGameInstance();
	const UReplaySubsystem* ReplaySubsystem = GameInst ? GameInst->GetSubsystem<UReplaySubsystem>() : nullptr;

	if (ReplaySubsystem)
	{
		return ReplaySubsystem->IsRecording();
	}
	else
	{
		// Using IsServer() because it also calls IsRecording() internally
		return (DemoNetDriver && DemoNetDriver->IsServer());
	}
}

void UWorld::PrepareMapChange(const TArray<FName>& LevelNames)
{
	// Kick off async loading request for those maps.
	if( !GEngine->PrepareMapChange(this, LevelNames) )
	{
		UE_LOGF(LogWorld, Warning,"Preparing map change via %ls was not successful: %ls", *GetFullName(), *GEngine->GetMapChangeFailureDescription(this) );
	}
}

bool UWorld::IsPreparingMapChange() const
{
	return GEngine->IsPreparingMapChange(const_cast<UWorld*>(this));
}

bool UWorld::IsMapChangeReady() const
{
	return GEngine->IsReadyForMapChange(const_cast<UWorld*>(this));
}

void UWorld::CancelPendingMapChange()
{
	GEngine->CancelPendingMapChange(this);
}

void UWorld::CommitMapChange()
{
	if( IsPreparingMapChange() )
	{
		GEngine->SetShouldCommitPendingMapChange(this, true);
	}
	else
	{
		UE_LOGF(LogWorld, Log, "AWorldSettings::CommitMapChange being called without a pending map change!");
	}
}

FTimerManager& UWorld::GetTimerManager() const
{
	return (OwningGameInstance ? OwningGameInstance->GetTimerManager() : *TimerManager);
}

FLatentActionManager& UWorld::GetLatentActionManager()
{
	return (OwningGameInstance ? OwningGameInstance->GetLatentActionManager() : LatentActionManager);
}

void UWorld::RequestNewWorldOrigin(FIntVector InNewOriginLocation)
{
	RequestedOriginLocation = InNewOriginLocation;
}

bool UWorld::SetNewWorldOrigin(FIntVector InNewOriginLocation)
{
	if (OriginLocation == InNewOriginLocation) 
	{
		return true;
	}
	
	// We cannot shift world origin while Level is in the process to be added to a world
	// it will cause wrong positioning for this level 
	if (IsVisibilityRequestPending())
	{
		return false;
	}
	
	UE_LOGF(LogLevel, Log, "WORLD TRANSLATION BEGIN {%d, %d, %d} -> {%d, %d, %d}", 
		OriginLocation.X, OriginLocation.Y, OriginLocation.Z, InNewOriginLocation.X, InNewOriginLocation.Y, InNewOriginLocation.Z);

	const double MoveStartTime = FPlatformTime::Seconds();

	// Broadcast that we going to shift world to the new origin
	FCoreDelegates::PreWorldOriginOffset.Broadcast(this, OriginLocation, InNewOriginLocation);

	FVector Offset(OriginLocation - InNewOriginLocation);
	OriginOffsetThisFrame = Offset;

	// Send offset command to rendering thread
	Scene->ApplyWorldOffset(Offset);

	// Shift physics scene
	if (PhysicsScene && FPhysScene::SupportsOriginShifting())
	{
		PhysicsScene->ApplyWorldOffset(Offset);
	}
		
	// Apply offset to all visible levels
	for(int32 LevelIndex = 0; LevelIndex < Levels.Num(); LevelIndex++)
	{
		ULevel* LevelToShift = Levels[LevelIndex];
		
		// Only visible sub-levels need to be shifted
		// Hidden sub-levels will be shifted once they become visible in UWorld::AddToWorld
		if (LevelToShift->bIsVisible || LevelToShift->IsPersistentLevel())
		{
			LevelToShift->ApplyWorldOffset(Offset, true);
		}
	}

	// Shift navigation meshes
	if (NavigationSystem)
	{
		NavigationSystem->ApplyWorldOffset(Offset, true);
	}

	// Apply offset to components with no actor (like UGameplayStatics::SpawnEmitterAtLocation) 
	{
		TArray <UObject*> WorldChildren; 
		GetObjectsWithOuter(this, WorldChildren, EGetObjectsFlags::None);

		for (UObject* ChildObject : WorldChildren)
		{
		   UActorComponent* Component = Cast<UActorComponent>(ChildObject);
		   if (Component && Component->GetOwner() == nullptr)
		   {
				Component->ApplyWorldOffset(Offset, true);
		   }
		}
	}
			
	for (TObjectPtr<ULineBatchComponent>& LineBatcher : LineBatchers)
	{
		if(LineBatcher)
		{
			LineBatcher->ApplyWorldOffset(Offset, true);
		}
	}

	if (PhysicsField)
	{
		PhysicsField->ApplyWorldOffset(Offset, true);
	}

	FIntVector PreviosWorldOriginLocation = OriginLocation;
	// Set new world origin
	OriginLocation = InNewOriginLocation;
	RequestedOriginLocation = InNewOriginLocation;
	
	// Propagate event to a level blueprints
	for(int32 LevelIndex = 0; LevelIndex < Levels.Num(); LevelIndex++)
	{
		ULevel* Level = Levels[LevelIndex];
		if (Level->bIsVisible && 
			Level->LevelScriptActor)
		{
			Level->LevelScriptActor->WorldOriginLocationChanged(PreviosWorldOriginLocation, OriginLocation);
		}
	}

	if (AISystem != NULL)
	{
		AISystem->WorldOriginLocationChanged(PreviosWorldOriginLocation, OriginLocation);
	}

	// Broadcast that have finished world shifting
	FCoreDelegates::PostWorldOriginOffset.Broadcast(this, PreviosWorldOriginLocation, OriginLocation);

	const double CurrentTime = FPlatformTime::Seconds();
	const double TimeTaken = CurrentTime - MoveStartTime;
	UE_LOGF(LogLevel, Log, "WORLD TRANSLATION END {%d, %d, %d} took %.4f ms",
		OriginLocation.X, OriginLocation.Y, OriginLocation.Z, TimeTaken * 1000);
	
	return true;
}

void UWorld::NavigateTo(FIntVector InLocation)
{
	check(WorldComposition != NULL);

	SetNewWorldOrigin(InLocation);
	WorldComposition->UpdateStreamingState(FVector::ZeroVector);
	FlushLevelStreaming();
}

/*-----------------------------------------------------------------------------
	Seamless world traveling
-----------------------------------------------------------------------------*/

void FSeamlessTravelHandler::SetHandlerLoadedData(UObject* InLevelPackage, UWorld* InLoadedWorld)
{
	LoadedPackage = InLevelPackage;
	LoadedWorld = InLoadedWorld;
	if (LoadedWorld != NULL)
	{
		LoadedWorld->AddToRoot();
	}

}

/** callback sent to async loading code to inform us when the level package is complete */
void FSeamlessTravelHandler::SeamlessTravelLoadCallback(const FName& PackageName, UPackage* LevelPackage, EAsyncLoadingResult::Type Result)
{
	// make sure we remove the name, even if travel was canceled.
	const FName URLMapFName = FName(*PendingTravelURL.Map);
	UWorld::WorldTypePreLoadMap.Remove(URLMapFName);

#if WITH_EDITOR
	if (GIsEditor)
	{
		FWorldContext &WorldContext = GEngine->GetWorldContextFromHandleChecked(WorldContextHandle);
		if (WorldContext.WorldType == EWorldType::PIE)
		{
			FString URLMapPackageName = UWorld::ConvertToPIEPackageName(PendingTravelURL.Map, WorldContext.PIEInstance);
			UWorld::WorldTypePreLoadMap.Remove(FName(*URLMapPackageName));
		}
	}
#endif

	// defer until tick when it's safe to perform the transition
	if (IsInTransition())
	{
		UWorld* World = UWorld::FindWorldInPackage(LevelPackage);

		// If the world could not be found, follow a redirector if there is one.
		if (!World)
		{
			World = UWorld::FollowWorldRedirectorInPackage(LevelPackage);
			if (World)
			{
				LevelPackage = World->GetOutermost();
			}
		}

		SetHandlerLoadedData(LevelPackage, World);

		// Now that the p map is loaded, start async loading any always loaded levels
		if (World)
		{
			if (World->WorldType == EWorldType::PIE)
			{
				if (LevelPackage->GetPIEInstanceID() != -1)
				{
					World->StreamingLevelsPrefix = UWorld::BuildPIEPackagePrefix(LevelPackage->GetPIEInstanceID());
				}
				else
				{
					// If this is a PIE world but the PIEInstanceID is -1, that implies this world is a temporary save
					// for multi-process PIE which should have been saved with the correct StreamingLevelsPrefix.
					ensure(!World->StreamingLevelsPrefix.IsEmpty());
				}
			}

			if (World->PersistentLevel)
			{
				World->PersistentLevel->HandleLegacyMapBuildData();
			}

			World->AsyncLoadAlwaysLoadedLevelsForSeamlessTravel();
		}
	}

	STAT_ADD_CUSTOMMESSAGE_NAME( STAT_NamedMarker, *(FString( TEXT( "StartTravelComplete - " ) + PackageName.ToString() )) );
	TRACE_BOOKMARK(TEXT("StartTravelComplete - %s"), *PackageName.ToString());
}

bool FSeamlessTravelHandler::StartTravel(UWorld* InCurrentWorld, const FURL& InURL)
{
	FWorldContext &Context = GEngine->GetWorldContextFromWorldChecked(InCurrentWorld);
	WorldContextHandle = Context.ContextHandle;

	SeamlessTravelStartTime = FPlatformTime::Seconds();

	if (!InURL.Valid)
	{
		UE_LOGF(LogWorld, Error, "Invalid travel URL specified");
		return false;
	}
	else
	{
		FLoadTimeTracker::Get().ResetRawLoadTimes();
		UE_LOGF(LogWorld, Log, "SeamlessTravel to: %ls", *InURL.Map);
		FString MapName = UWorld::RemovePIEPrefix(InURL.Map);
		if (!FPackageName::DoesPackageExist(MapName))
		{
			UE_LOGF(LogWorld, Error, "Unable to travel to '%ls' - file not found", *MapName);
			return false;
			// @todo: might have to handle this more gracefully to handle downloading (might also need to send GUID and check it here!)
		}
		else
		{
			bool bCancelledExisting = false;
			if (IsInTransition())
			{
				if (PendingTravelURL.Map == InURL.Map)
				{
					// we are going to the same place so just replace the options
					PendingTravelURL = InURL;
					return true;
				}
				UE_LOGF(LogWorld, Warning, "Cancelling travel to '%ls' to go to '%ls' instead", *PendingTravelURL.Map, *InURL.Map);
				CancelTravel();
				bCancelledExisting = true;
			}

			// CancelTravel will null out CurrentWorld, so we need to assign it after that.
			CurrentWorld = InCurrentWorld;

			FWorldDelegates::OnSeamlessTravelStart.Broadcast(CurrentWorld, InURL.Map);

			checkSlow(LoadedPackage == NULL);
			checkSlow(LoadedWorld == NULL);

			PendingTravelURL = InURL;
			bSwitchedToDefaultMap = false;
			bTransitionInProgress = true;
			bPauseAtMidpoint = false;
			bNeedCancelCleanUp = false;

			FName CurrentMapName = CurrentWorld->GetOutermost()->GetFName();
			FName DestinationMapName = FName(*PendingTravelURL.Map);

			FString TransitionMap = GetDefault<UGameMapsSettings>()->TransitionMap.GetLongPackageName();
			FName DefaultMapFinalName(*TransitionMap);

			// if we're already in the default map, skip loading it and just go to the destination
			if (DefaultMapFinalName == CurrentMapName ||
				DefaultMapFinalName == DestinationMapName)
			{
				UE_LOGF(LogWorld, Log, "Already in default map or the default map is the destination, continuing to destination");
				bSwitchedToDefaultMap = true;
				if (bCancelledExisting)
				{
					// we need to fully finishing loading the old package and GC it before attempting to load the new one
					bPauseAtMidpoint = true;
					bNeedCancelCleanUp = true;
				}
				else
				{
					StartLoadingDestination();
				}
			}
			else
			{
				UNetDriver* const NetDriver = CurrentWorld->GetNetDriver();
				if (NetDriver)
				{
					for (int32 ClientIdx = 0; ClientIdx < NetDriver->ClientConnections.Num(); ClientIdx++)
					{
						UNetConnection* Connection = NetDriver->ClientConnections[ClientIdx];
						if (Connection)
						{
							// Empty the current map name on all transitions because the server could try to spawn actors 
							// before the client starts the transfer causing the server to think its loaded
							Connection->SetClientWorldPackageName(NAME_None);
						}
					}
				}
				
				if (TransitionMap.IsEmpty())
				{
					// If a default transition map doesn't exist, create a dummy World of the right type to use as the transition
					SetHandlerLoadedData(nullptr, UWorld::CreateWorld(CurrentWorld->WorldType, false));
				}
				else
				{
					// Load the transition map, possibly with PIE prefix
					STAT_ADD_CUSTOMMESSAGE_NAME( STAT_NamedMarker, *(FString( TEXT( "StartTravel - " ) + TransitionMap )) );
					TRACE_BOOKMARK(TEXT("StartTravel - %s"), *TransitionMap);

					if (!StartLoadingMap(TransitionMap))
					{
						UE_LOGF(LogWorld, Error, "StartTravel: Invalid TransitionMap \"%ls\"", *TransitionMap);
					}
				}
			}

			return true;
		}
	}
}

/** cancels transition in progress */
void FSeamlessTravelHandler::CancelTravel()
{
	LoadedPackage = NULL;
	if (LoadedWorld != NULL)
	{
		LoadedWorld->RemoveFromRoot();
		LoadedWorld->ClearFlags(RF_Standalone);
		LoadedWorld = NULL;
	}

	if (bTransitionInProgress)
	{
		UPackage* Package = CurrentWorld ? CurrentWorld->GetOutermost() : nullptr;
		if (Package)
		{
			FName CurrentPackageName = Package->GetFName();
			UNetDriver* const NetDriver = CurrentWorld->GetNetDriver();
			if (NetDriver)
			{
				for (int32 ClientIdx = 0; ClientIdx < NetDriver->ClientConnections.Num(); ClientIdx++)
				{
					UNetConnection* Connection = NetDriver->ClientConnections[ClientIdx];
					if (Connection)
					{
						UChildConnection* ChildConnection = Connection->GetUChildConnection();
						if (ChildConnection)
						{
							Connection = ChildConnection->Parent;
						}

						// Mark all clients as being where they are since this was set to None in StartTravel
						Connection->SetClientWorldPackageName(CurrentPackageName);
					}
				}
			}
		}
	
		CurrentWorld = NULL;
		bTransitionInProgress = false;
		UE_LOGF(LogWorld, Log, "----SeamlessTravel is cancelled!------");
	}
}

void FSeamlessTravelHandler::SetPauseAtMidpoint(bool bNowPaused)
{
	if (!bTransitionInProgress)
	{
		UE_LOGF(LogWorld, Warning, "Attempt to pause seamless travel when no transition is in progress");
	}
	else if (bSwitchedToDefaultMap && bNowPaused)
	{
		UE_LOGF(LogWorld, Warning, "Attempt to pause seamless travel after started loading final destination");
	}
	else
	{
		bPauseAtMidpoint = bNowPaused;
		if (!bNowPaused && bSwitchedToDefaultMap)
		{
			// load the final destination now that we're done waiting
			StartLoadingDestination();
		}
	}
}

bool FSeamlessTravelHandler::StartLoadingMap(FString MapPackageToLoadFrom)
{
	if (MapPackageToLoadFrom.IsEmpty())
	{
		return false;
	}

	// In PIE we might want to mangle MapPackageName when traveling to a map loaded in the editor
	FString MapPackageName = MapPackageToLoadFrom;
	EPackageFlags PackageFlags = PKG_None;
	int32 PIEInstanceID = INDEX_NONE;

#if WITH_EDITOR
	if (GIsEditor)
	{
		FWorldContext& WorldContext = GEngine->GetWorldContextFromHandleChecked(WorldContextHandle);

		PIEInstanceID = WorldContext.PIEInstance;
		MapPackageName = UWorld::ConvertToPIEPackageName(MapPackageName, PIEInstanceID);

		if (WorldContext.WorldType == EWorldType::PIE)
		{
			PackageFlags |= PKG_PlayInEditor;

			// Prepare soft object paths for fixup
			FSoftObjectPath::AddPIEPackageName(FName(*MapPackageName));
		}
	}
#endif

	// Set the world type in the static map, so that UWorld::PostLoad can set the world type
	FName PackageFName(*MapPackageName);
	UWorld::WorldTypePreLoadMap.FindOrAdd(PackageFName) = CurrentWorld->WorldType;

	FPackagePath PackagePath;
	if (FPackagePath::TryFromMountedName(MapPackageToLoadFrom, PackagePath))
	{
		LoadPackageAsync(
			PackagePath,
			PackageFName,
			FLoadPackageAsyncDelegate::CreateRaw(this, &FSeamlessTravelHandler::SeamlessTravelLoadCallback),
			PackageFlags,
			PIEInstanceID
		);

		return true;
	}

	return false;
}

void FSeamlessTravelHandler::StartLoadingDestination()
{
	if (bTransitionInProgress && bSwitchedToDefaultMap)
	{
		UE_LOGF(LogWorld, Log, "StartLoadingDestination to: %ls", *PendingTravelURL.Map);

		CurrentWorld->GetGameInstance()->PreloadContentForURL(PendingTravelURL);

		if (!StartLoadingMap(PendingTravelURL.Map))
		{
			UE_LOGF(LogWorld, Error, "StartLoadingDestination: Invalid destination map \"%ls\"", *PendingTravelURL.Map);
		}
	}
	else
	{
		UE_LOGF(LogWorld, Error, "Called StartLoadingDestination() when not ready! bTransitionInProgress: %u bSwitchedToDefaultMap: %u", bTransitionInProgress, bSwitchedToDefaultMap);
		checkSlow(0);
	}
}

void FSeamlessTravelHandler::CopyWorldData()
{
	FWorldDelegates::OnCopyWorldData.Broadcast(CurrentWorld, LoadedWorld);

	FLevelCollection* const CurrentCollection = CurrentWorld->FindCollectionByType(ELevelCollectionType::DynamicSourceLevels);
	FLevelCollection* const CurrentStaticCollection = CurrentWorld->FindCollectionByType(ELevelCollectionType::StaticLevels);
	FLevelCollection* const LoadedCollection = LoadedWorld->FindCollectionByType(ELevelCollectionType::DynamicSourceLevels);
	FLevelCollection* const LoadedStaticCollection = LoadedWorld->FindCollectionByType(ELevelCollectionType::StaticLevels);

	UNetDriver* const NetDriver = CurrentWorld->GetNetDriver();
	LoadedWorld->SetNetDriver(NetDriver);

	if (CurrentCollection && LoadedCollection)
	{
		LoadedCollection->SetNetDriver(NetDriver);
		CurrentCollection->SetNetDriver(nullptr);
	}
	if (CurrentStaticCollection && LoadedStaticCollection)
	{
		LoadedStaticCollection->SetNetDriver(NetDriver);
		CurrentStaticCollection->SetNetDriver(nullptr);
	}

	if (NetDriver != nullptr)
	{
		CurrentWorld->SetNetDriver(nullptr);
		NetDriver->SetWorld(LoadedWorld);
	}
	LoadedWorld->WorldType = CurrentWorld->WorldType;
	LoadedWorld->SetGameInstance(CurrentWorld->GetGameInstance());

	LoadedWorld->TimeSeconds = CurrentWorld->TimeSeconds;
	LoadedWorld->UnpausedTimeSeconds = CurrentWorld->UnpausedTimeSeconds;
	LoadedWorld->RealTimeSeconds = CurrentWorld->RealTimeSeconds;
	LoadedWorld->AudioTimeSeconds = CurrentWorld->AudioTimeSeconds;

	if (NetDriver != nullptr)
	{
		LoadedWorld->NextSwitchCountdown = NetDriver->ServerTravelPause;
	}
}

/** 
 * Version of FArchiveReplaceObjectRef that will also clear references to garbage objects not in the replacement map 
 * This does not try to recursively serialize subobjects because it was unreliable and missed ones without hard parent references
 */
template< class T >
class FArchiveReplaceOrClearGarbageReferences : public FArchiveReplaceObjectRef<T>
{
	typedef FArchiveReplaceObjectRef<T> TSuper;
public:
	FArchiveReplaceOrClearGarbageReferences
		( UObject* InSearchObject
		, const TMap<T*, T*>& InReplacementMap
		, EArchiveReplaceObjectFlags Flags = EArchiveReplaceObjectFlags::None)
		: TSuper(InSearchObject, InReplacementMap, EArchiveReplaceObjectFlags::DelayStart | Flags)
	{
		if (!(Flags & EArchiveReplaceObjectFlags::DelayStart))
		{
			this->SerializeSingleSearchObject();
		}
	}

	void SerializeSingleSearchObject()
	{
		TSuper::ReplacedReferences.Reset();

		// Difference from parent behavior is to always run even if map is empty, and to ignore subobjects
		TSuper::SerializedObjects.Add(TSuper::SearchObject);
		TSuper::SerializingObject = TSuper::SearchObject;
		TSuper::SerializeObject(TSuper::SearchObject);
	}

	FArchive& operator<<(UObject*& Obj) override
	{
		UObject* Resolved = Obj;
		TSuper::operator<<(Resolved);

		// if Resolved is garbage, just clear the reference:
		if (Resolved && !IsValid(Resolved))
		{
			Resolved = nullptr;
		}
		Obj = Resolved;
		return *this;
	}
};

UWorld* FSeamlessTravelHandler::Tick()
{
	bool bWorldChanged = false;
	if (bNeedCancelCleanUp)
	{
		if (!IsAsyncLoading())
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
			bNeedCancelCleanUp = false;
			SetPauseAtMidpoint(false);
		}
	}
	//@fixme: wait for client to verify packages before finishing transition. Is this the right fix?
	// Once the default map is loaded, go ahead and start loading the destination map
	// Once the destination map is loaded, wait until all packages are verified before finishing transition

	check(CurrentWorld);

	UNetDriver* NetDriver = CurrentWorld->GetNetDriver();

	if ( ( LoadedPackage != nullptr || LoadedWorld != nullptr ) && CurrentWorld->NextURL == TEXT( "" ) )
	{
		// Wait for async loads to finish before finishing seamless. (E.g., we've loaded the persistent map but are still loading 'always loaded' sub levels)
		if (LoadedWorld)
		{
			if (IsAsyncLoading() )
			{
				return nullptr;
			}
		}

		// First some validity checks		
		if( CurrentWorld == LoadedWorld )
		{
			// We are not going anywhere - this is the same world. 
			FString Error = FString::Printf(TEXT("Travel aborted - new world is the same as current world" ) );
			UE_LOGF(LogWorld, Error, "%ls", *Error);
			// abort
			CancelTravel();			
		}
		else if ( LoadedWorld == nullptr || LoadedWorld->PersistentLevel == nullptr)
		{
			// Package isn't a level
			FString Error = FString::Printf(TEXT("Unable to travel to '%s' - package is not a level"), LoadedPackage ? *LoadedPackage->GetName() : *LoadedWorld->GetName());
			UE_LOGF(LogWorld, Error, "%ls", *Error);
			// abort
			CancelTravel();
			GEngine->BroadcastTravelFailure(CurrentWorld, ETravelFailure::NoLevel, Error);
		}
		else
		{
			// Make sure there are no pending visibility requests.
			CurrentWorld->FlushLevelStreaming(EFlushLevelStreamingType::Visibility);
				
			if (!GIsEditor && !IsRunningDedicatedServer() && bSwitchedToDefaultMap)
			{
				// If requested, duplicate dynamic levels here after the source levels are created.
				LoadedWorld->DuplicateRequestedLevels(LoadedWorld->GetOuter()->GetFName());
			}

			if (CurrentWorld->GetGameState())
			{
				CurrentWorld->GetGameState()->SeamlessTravelTransitionCheckpoint(!bSwitchedToDefaultMap);
			}
			
			CurrentWorld->BeginTearingDown();

			FWorldDelegates::OnSeamlessTravelTransition.Broadcast(CurrentWorld);

			// mark actors we want to keep
			FUObjectAnnotationSparseBool KeepAnnotation;
			TArray<AActor*> KeepActors;

			if (AGameModeBase* AuthGameMode = CurrentWorld->GetAuthGameMode())
			{
				AuthGameMode->GetSeamlessTravelActorList(!bSwitchedToDefaultMap, KeepActors);
			}

			const bool bIsClient = (CurrentWorld->GetNetMode() == NM_Client);

			// always keep Controllers that belong to players
			if (bIsClient)
			{
				for (FLocalPlayerIterator It(GEngine, CurrentWorld); It; ++It)
				{
					if (It->PlayerController != nullptr)
					{
						KeepAnnotation.Set(It->PlayerController);
					}
				}
			}
			else
			{
				for( FConstControllerIterator Iterator = CurrentWorld->GetControllerIterator(); Iterator; ++Iterator )
				{
					if (AController* Player = Iterator->Get())
					{
						if (Player->ShouldParticipateInSeamlessTravel())
						{
							KeepAnnotation.Set(Player);
						}
					}
				}
			}

			// ask players what else we should keep
			for (FLocalPlayerIterator It(GEngine, CurrentWorld); It; ++It)
			{
				if (It->PlayerController != nullptr)
				{
					It->PlayerController->GetSeamlessTravelActorList(!bSwitchedToDefaultMap, KeepActors);
				}
			}
			// mark all valid actors specified
			for (AActor* KeepActor : KeepActors)
			{
				if (KeepActor != nullptr)
				{
					KeepAnnotation.Set(KeepActor);
				}
			} 

			TArray<AActor*> ActuallyKeptActors;
			ActuallyKeptActors.Reserve(KeepAnnotation.Num());

			// Rename dynamic actors in the old world's PersistentLevel that we want to keep into the new world
			auto ProcessActor = [this, &KeepAnnotation, &ActuallyKeptActors, NetDriver](AActor* TheActor) -> bool
			{
				const bool bIsInCurrentLevel	= TheActor->GetLevel() == CurrentWorld->PersistentLevel;
				const bool bManuallyMarkedKeep	= KeepAnnotation.Get(TheActor);
				const bool bDormant				= TheActor->GetIsReplicated() && (TheActor->NetDormancy > DORM_Awake);
				const bool bKeepNonOwnedActor	= TheActor->GetLocalRole() < ROLE_Authority && !bDormant && !TheActor->IsNetStartupActor();
				const bool bForceExcludeActor	= TheActor->IsA(ALevelScriptActor::StaticClass());

				// Keep if it's in the current level AND it isn't specifically excluded AND it was either marked as should keep OR we don't own this actor
				if (bIsInCurrentLevel && !bForceExcludeActor && (bManuallyMarkedKeep || bKeepNonOwnedActor))
				{
					if (NetDriver != nullptr)
					{
						NetDriver->NotifyActorIsTraveling(TheActor);
					}

					ActuallyKeptActors.Add(TheActor);
					return true;
				}
				else
				{
					if (bManuallyMarkedKeep)
					{
						UE_LOGF(LogWorld, Warning, "Actor '%ls' was indicated to be kept but exists in level '%ls', not the persistent level.  Actor will not travel.", *TheActor->GetName(), *TheActor->GetLevel()->GetOutermost()->GetName());
					}

					TheActor->RouteEndPlay(EEndPlayReason::LevelTransition);

					// otherwise, set to be deleted
					KeepAnnotation.Clear(TheActor);
					// close any channels for this actor
					if (NetDriver != nullptr)
					{
						NetDriver->NotifyActorLevelUnloaded(TheActor);
					}
					return false;
				}
			};

			// We move everything but the player controllers first, and then the controllers, keeping their relative order, to avoid breaking any call to GetPlayerController with a fixed index.
			for (FActorIterator It(CurrentWorld); It; ++It)
			{
				AActor* TheActor = *It;
				if (!TheActor->IsA(APlayerController::StaticClass()))
				{
					ProcessActor(TheActor);
				}
			}

			for (FConstPlayerControllerIterator Iterator = CurrentWorld->GetPlayerControllerIterator(); Iterator; ++Iterator)
			{
				if (APlayerController* Player = Iterator->Get())
				{
					ProcessActor(Player);
				}
			}

			if (NetDriver)
			{
				NetDriver->CleanupWorldForSeamlessTravel();
			}

			bool bCreateNewGameMode = !bIsClient;
			TArray<AController*> KeptControllers;
			{
				// scope because after GC the kept pointers will be bad
				AGameModeBase* KeptGameMode = nullptr;
				AGameStateBase* KeptGameState = nullptr;

				// Second pass to rename and move actors that need to transition into the new world
				// This is done after cleaning up actors that aren't transitioning in case those actors depend on these
				// actors being in the same world.
				for (AActor* const TheActor : ActuallyKeptActors)
				{
					KeepAnnotation.Clear(TheActor);
					
					// if it's a Controller, remove it from the appropriate list in the current world's WorldSettings
					if (AController* Controller = Cast<AController>(TheActor))
					{
						CurrentWorld->RemoveController(Controller);
						KeptControllers.Add(Controller);
					}
					else if (TheActor->IsA<AGameModeBase>())
					{
						KeptGameMode = static_cast<AGameModeBase*>(TheActor);
					}
					else if (TheActor->IsA<AGameStateBase>())
					{
						KeptGameState = static_cast<AGameStateBase*>(TheActor);
					}

					TheActor->Rename(nullptr, LoadedWorld->PersistentLevel);

					TheActor->bActorSeamlessTraveled = true;
				}

				if (KeptGameMode)
				{
					LoadedWorld->CopyGameState(KeptGameMode, KeptGameState);
					bCreateNewGameMode = false;
				}

				CopyWorldData(); // This copies the net driver too (LoadedWorld now has whatever NetDriver was previously held by CurrentWorld)
			}

			// only consider session ended if we're making the final switch so that HUD, etc. UI elements stay around until the end
			CurrentWorld->SetBegunPlay(false);
			CurrentWorld->CleanupWorld(bSwitchedToDefaultMap);
			CurrentWorld->RemoveFromRoot();
			CurrentWorld->ClearFlags(RF_Standalone);

			// Stop all audio to remove references to old world
			if (FAudioDevice* AudioDevice = CurrentWorld->GetAudioDeviceRaw())
			{
				AudioDevice->Flush(CurrentWorld);
			}

			// Copy the standby cheat status
			bool bHasStandbyCheatTriggered = (CurrentWorld->NetworkManager) ? CurrentWorld->NetworkManager->bHasStandbyCheatTriggered : false;

			// the new world should not be garbage collected
			LoadedWorld->AddToRoot();
			
			// Update the FWorldContext to point to the newly loaded world
			FWorldContext &CurrentContext = GEngine->GetWorldContextFromWorldChecked(CurrentWorld);
			CurrentContext.SetCurrentWorld(LoadedWorld);

			LoadedWorld->WorldType = CurrentContext.WorldType;
			if (CurrentContext.WorldType == EWorldType::PIE)
			{
				UPackage * WorldPackage = LoadedWorld->GetOutermost();
				check(WorldPackage);
				WorldPackage->SetPackageFlags(PKG_PlayInEditor);
			}

			// Clear any world specific state from NetDriver before switching World
			if (NetDriver)
			{
				NetDriver->PreSeamlessTravelGarbageCollect();

				// Warn if we loaded a game mode that wanted a different replication system from the previous mode.
				if (AGameModeBase* GameMode = LoadedWorld->GetAuthGameMode())
				{
					const EReplicationSystem LoadedGameModeRepSystem = GameMode->GetGameNetDriverReplicationSystem();
					const EReplicationSystem IrisCmdlineRepSystem = UE::Net::GetUseIrisReplicationCmdlineValue();

					const bool bIsNetDriverCompatible = LoadedGameModeRepSystem == EReplicationSystem::Default || 
														IrisCmdlineRepSystem != EReplicationSystem::Default ||
														(LoadedGameModeRepSystem == EReplicationSystem::Iris && NetDriver->IsUsingIrisReplication());
					ensureMsgf(bIsNetDriverCompatible, TEXT("Seamless travel loaded game mode %s that wants a different replication system than the current NetDriver uses."), *GetNameSafe(GameMode));
				}
			}

			GWorld = nullptr;

			// mark everything else contained in the world to be deleted
			for (auto LevelIt(CurrentWorld->GetLevelIterator()); LevelIt; ++LevelIt)
			{
				if (const ULevel* Level = *LevelIt)
				{
					UWorld* World = CastChecked<UWorld>(Level->GetOuter());
					if (!World->HasMarkedObjectsPendingKill())
					{
						World->MarkObjectsPendingKill();
					}
				}
			}

			for (ULevelStreaming* LevelStreaming : CurrentWorld->GetStreamingLevels())
			{
				// If an unloaded levelstreaming still has a loaded level we need to mark its objects to be deleted as well
				if (LevelStreaming->GetLoadedLevel())
				{
					UWorld* World = CastChecked<UWorld>(LevelStreaming->GetLoadedLevel()->GetOuter());
					if (!World->HasMarkedObjectsPendingKill())
					{
						World->MarkObjectsPendingKill();
					}
				}
			}

			CurrentWorld = nullptr;

			if (!UObjectBaseUtility::IsGarbageEliminationEnabled())
			{
				// If pending kill is disabled, run an explicit serializer to clear references to garbage objects on the transferred actors
				TMap<UObject*, UObject*> ReplacementMap;

				for (AActor* const TheActor : ActuallyKeptActors)
				{
					auto ClearReferences = [ReplacementMap](UObject* Object)
					{
						FArchiveReplaceOrClearGarbageReferences<UObject> ReplaceAr(Object, ReplacementMap, EArchiveReplaceObjectFlags::IgnoreOuterRef);
					};

					// Process all subobjects, even unreferenced ones
					ClearReferences(TheActor);
					ForEachObjectWithOuter(TheActor, ClearReferences, EGetObjectsFlags::IncludeNestedObjects, RF_NoFlags, EInternalObjectFlags::Garbage);
				}
			}

			// collect garbage to delete the old world
			// because we marked everything in it pending kill, references will be NULL'ed so we shouldn't end up with any dangling pointers
			CollectGarbage( GARBAGE_COLLECTION_KEEPFLAGS, true );

			if (GIsEditor)
			{
				CollectGarbage( GARBAGE_COLLECTION_KEEPFLAGS, true );
			}

			appDefragmentTexturePool();
			appDumpTextureMemoryStats(TEXT(""));

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			// verify that we successfully cleaned up the old world
			GEngine->CheckAndHandleStaleWorldObjectReferences(&CurrentContext);
#endif
			// Clean out NetDriver's Packagemaps, since they may have a lot of NULL object ptrs rotting in the lookup maps.
			if (NetDriver)
			{
				NetDriver->PostSeamlessTravelGarbageCollect();
			}

			// set GWorld to the new world and initialize it
			GWorld = LoadedWorld;
			if (!LoadedWorld->bIsWorldInitialized)
			{
				LoadedWorld->InitWorld();
			}

			// add controllers to initialized world 
			for (AController* Controller : KeptControllers)
			{
				LoadedWorld->AddController(Controller);
			}

			bWorldChanged = true;
			// Track session change on seamless travel.
			NETWORK_PROFILER(GNetworkProfiler.TrackSessionChange(true, LoadedWorld->URL));

#if WITH_EDITOR
			// PIE worlds should use the same feature level as the editor
			if (CurrentContext.PIEWorldFeatureLevel != ERHIFeatureLevel::Num && LoadedWorld->GetFeatureLevel() != CurrentContext.PIEWorldFeatureLevel)
			{
				LoadedWorld->ShaderPlatformChanged(GShaderPlatformForFeatureLevel[CurrentContext.PIEWorldFeatureLevel]);
			}
#endif

			checkSlow((LoadedWorld->GetNetMode() == NM_Client) == bIsClient);

			if (bCreateNewGameMode)
			{
				LoadedWorld->SetGameMode(PendingTravelURL);
			}

			// if we've already switched to entry before and this is the transition to the new map, re-create the gameinfo
			if (bSwitchedToDefaultMap && !bIsClient)
			{
				if (FAudioDevice* AudioDevice = LoadedWorld->GetAudioDeviceRaw())
				{
					AudioDevice->SetDefaultBaseSoundMix(LoadedWorld->GetWorldSettings()->DefaultBaseSoundMix);
				}

				// Copy cheat flags if the game info is present
				// @todo FIXMELH - see if this exists, it should not since it's created in GameMode or it's garbage info
				if (LoadedWorld->NetworkManager != nullptr)
				{
					LoadedWorld->NetworkManager->bHasStandbyCheatTriggered = bHasStandbyCheatTriggered;
				}
			}

			// Make sure "always loaded" sub-levels are fully loaded
			{
				SCOPE_LOG_TIME_IN_SECONDS(TEXT("    SeamlessTravel FlushLevelStreaming "), nullptr)
				LoadedWorld->FlushLevelStreaming(EFlushLevelStreamingType::Visibility);	
			}
			
			// Note that AI system will be created only if ai-system-creation conditions are met
			LoadedWorld->CreateAISystem();

			// call initialize functions on everything that wasn't carried over from the old world
			LoadedWorld->InitializeActorsForPlay(PendingTravelURL, false);

			// If using an empty temporary transition world, make sure the world settings aren't replicated
			FString TransitionMap = GetDefault<UGameMapsSettings>()->TransitionMap.GetLongPackageName();
			if (TransitionMap.IsEmpty() && !bSwitchedToDefaultMap)
			{
				AWorldSettings* WorldSettings = LoadedWorld->GetWorldSettings();
				if (WorldSettings)
				{
					WorldSettings->SetReplicates(false);
					LoadedWorld->RemoveNetworkActor(WorldSettings);
				}
			}

			// We don't want to add navigation system to the transition map as it never starts gameplay
			if (bSwitchedToDefaultMap)
			{
				// calling it after InitializeActorsForPlay has been called to have all potential bounding boxed initialized
				FNavigationSystem::AddNavigationSystemToWorld(*LoadedWorld, FNavigationSystemRunMode::GameMode);
			}

			FName LoadedWorldName = FName(*UWorld::RemovePIEPrefix(LoadedWorld->GetOutermost()->GetName()));

			// send loading complete notifications for all local players
			for (FLocalPlayerIterator It(GEngine, LoadedWorld); It; ++It)
			{
				UE_LOGF(LogWorld, Log, "Sending NotifyLoadedWorld for LP: %ls PC: %ls", *It->GetName(), It->PlayerController ? *It->PlayerController->GetName() : TEXT("NoPC"));
				if (It->PlayerController != nullptr)
				{
#if !UE_BUILD_SHIPPING
					LOG_SCOPE_VERBOSITY_OVERRIDE(LogNet, ELogVerbosity::VeryVerbose);
					LOG_SCOPE_VERBOSITY_OVERRIDE(LogNetTraffic, ELogVerbosity::VeryVerbose);
					UE_LOGF(LogNet, Verbose, "NotifyLoadedWorld Begin");
#endif
					It->PlayerController->NotifyLoadedWorld(LoadedWorldName, bSwitchedToDefaultMap);

					const FName SendLoadedWorldName = It->PlayerController->NetworkRemapPath(LoadedWorldName, false);
					It->PlayerController->ServerNotifyLoadedWorld(SendLoadedWorldName);
					It->CleanupViewState();
#if !UE_BUILD_SHIPPING
					UE_LOGF(LogNet, Verbose, "NotifyLoadedWorld End");
#endif
				}
				else
				{
					UE_LOGF(LogNet, Error, "No Player Controller during seamless travel for LP: %ls.", *It->GetName());
					// @todo add some kind of travel back to main menu
				}
			}

			// we've finished the transition
			LoadedWorld->bWorldWasLoadedThisTick = true;
			
			if (bSwitchedToDefaultMap)
			{
				// we've now switched to the final destination, so we're done
				
				// remember the last used URL
				CurrentContext.LastURL = PendingTravelURL;

				// Flag our transition as completed before we call PostSeamlessTravel.  This 
				// allows for chaining of maps.

				bTransitionInProgress = false;
				
				double TotalSeamlessTravelTime = FPlatformTime::Seconds() - SeamlessTravelStartTime;
				UE_LOGF(LogWorld, Log, "----SeamlessTravel finished in %.2f seconds ------", TotalSeamlessTravelTime );
				FLoadTimeTracker::Get().DumpRawLoadTimes();

				AGameModeBase* const GameMode = LoadedWorld->GetAuthGameMode();
				if (GameMode)
				{
					// inform the new GameMode so it can handle players that persisted
					GameMode->PostSeamlessTravel();					
				}

				// Called after post seamless travel to make sure players are setup correctly first
				LoadedWorld->BeginPlay();

				FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(LoadedWorld);
			}
			else
			{
				bSwitchedToDefaultMap = true;
				CurrentWorld = LoadedWorld;
				if (!bPauseAtMidpoint)
				{
					StartLoadingDestination();
				}
			}			
		}		
	}
	UWorld* OutWorld = nullptr;
	if( bWorldChanged )
	{
		OutWorld = LoadedWorld;
		// Cleanup the old pointers
		LoadedPackage = nullptr;
		LoadedWorld = nullptr;
	}
	
	return OutWorld;
}

/** seamlessly travels to the given URL by first loading the entry level in the background,
 * switching to it, and then loading the specified level. Does not disrupt network communication or disconnect clients.
 * You may need to implement GameMode::GetSeamlessTravelActorList(), PlayerController::GetSeamlessTravelActorList(),
 * GameMode::PostSeamlessTravel(), and/or GameMode::HandleSeamlessTravelPlayer() to handle preserving any information
 * that should be maintained (player teams, etc)
 * This codepath is designed for worlds that use little or no level streaming and GameModes where the game state
 * is reset/reloaded when transitioning. (like UT)
 * @param URL the URL to travel to; must be relative to the current URL (same server)
 * @param bAbsolute (opt) - if true, URL is absolute, otherwise relative
 */
void UWorld::SeamlessTravel(const FString& SeamlessTravelURL, bool bAbsolute)
{
	// construct the URL
	FURL NewURL(&GEngine->LastURLFromWorld(this), *SeamlessTravelURL, bAbsolute ? TRAVEL_Absolute : TRAVEL_Relative);
	if (!NewURL.Valid)
	{
		const FString Error = FText::Format( NSLOCTEXT("Engine", "InvalidUrl", "Invalid URL: {0}"), FText::FromString( SeamlessTravelURL ) ).ToString();
		GEngine->BroadcastTravelFailure(this, ETravelFailure::InvalidURL, Error);
	}
	else
	{
		if (NewURL.HasOption(TEXT("Restart")))
		{
			//@todo url cleanup - we should merge the two URLs, not completely replace it
			NewURL = GEngine->LastURLFromWorld(this);
		}
		// tell the handler to start the transition
		FSeamlessTravelHandler &SeamlessTravelHandler = GEngine->SeamlessTravelHandlerForWorld( this );
		if (!SeamlessTravelHandler.StartTravel(this, NewURL) && !SeamlessTravelHandler.IsInTransition())
		{
			const FString Error = FText::Format( NSLOCTEXT("Engine", "InvalidUrl", "Invalid URL: {0}"), FText::FromString( SeamlessTravelURL ) ).ToString();
			GEngine->BroadcastTravelFailure(this, ETravelFailure::InvalidURL, Error);
		}
	}
}

/** @return whether we're currently in a seamless transition */
bool UWorld::IsInSeamlessTravel() const
{
	FSeamlessTravelHandler& SeamlessTravelHandler = GEngine->SeamlessTravelHandlerForWorld(const_cast<UWorld*>(this));
	return SeamlessTravelHandler.IsInTransition();
}

/** this function allows pausing the seamless travel in the middle,
 * right before it starts loading the destination (i.e. while in the transition level)
 * this gives the opportunity to perform any other loading tasks before the final transition
 * this function has no effect if we have already started loading the destination (you will get a log warning if this is the case)
 * @param bNowPaused - whether the transition should now be paused
 */
void UWorld::SetSeamlessTravelMidpointPause(bool bNowPaused)
{
	FSeamlessTravelHandler &SeamlessTravelHandler = GEngine->SeamlessTravelHandlerForWorld( this );
	SeamlessTravelHandler.SetPauseAtMidpoint(bNowPaused);
}

int32 UWorld::GetDetailMode() const
{
	return GetCachedScalabilityCVars().DetailMode;
}

/**
 * Updates all physics constraint actor joint locations.
 */
void UWorld::UpdateConstraintActors()
{
	if( bAreConstraintsDirty )
	{
		for( TActorIterator<APhysicsConstraintActor> It(this); It; ++It )
		{
			APhysicsConstraintActor* ConstraintActor = *It;
			if( ConstraintActor->GetConstraintComp())
			{
				ConstraintActor->GetConstraintComp()->UpdateConstraintFrames();
			}
		}
		bAreConstraintsDirty = false;
	}
}

int32 UWorld::GetProgressDenominator() const
{
	return GetActorCount();
}

int32 UWorld::GetActorCount() const
{
	int32 TotalActorCount = 0;
	for( int32 LevelIndex=0; LevelIndex<GetNumLevels(); LevelIndex++ )
	{
		ULevel* Level = GetLevel(LevelIndex);
		TotalActorCount += Level->Actors.Num();
	}
	return TotalActorCount;
}

FConstLevelIterator	UWorld::GetLevelIterator() const
{
	return ToRawPtrTArrayUnsafe(Levels).CreateConstIterator();
}

ULevel* UWorld::GetLevel( int32 InLevelIndex ) const
{
	check(InLevelIndex < Levels.Num());
	check(Levels[InLevelIndex]);
	return Levels[ InLevelIndex ];
}

bool UWorld::ContainsLevel( ULevel* InLevel ) const
{
	return Levels.Find( InLevel ) != INDEX_NONE;
}

int32 UWorld::GetNumLevels() const
{
	return Levels.Num();
}

const TArray<class ULevel*>& UWorld::GetLevels() const
{
	return Levels;
}

bool UWorld::AddLevel( ULevel* InLevel )
{
	bool bAddedLevel = false;
	if(ensure(InLevel))
	{
		bAddedLevel = true;
		Levels.AddUnique( InLevel );
		BroadcastLevelsChanged();
	}
	return bAddedLevel;
}

bool UWorld::RemoveLevel( ULevel* InLevel )
{
	bool bRemovedLevel = false;
	if(ContainsLevel( InLevel ) == true )
	{
		bRemovedLevel = true;
		
#if WITH_EDITOR
		if( IsLevelSelected( InLevel ))
		{
			DeSelectLevel( InLevel );
		}
#endif //WITH_EDITOR
		Levels.Remove( InLevel );
		BroadcastLevelsChanged();
	}
	return bRemovedLevel;
}


FString UWorld::GetLocalURL() const
{
	return URL.ToString();
}

/** Returns whether script is executing within the editor. */
bool UWorld::IsPlayInEditor() const
{
	return WorldType == EWorldType::PIE;
}

bool UWorld::IsPlayInPreview() const
{
	return FParse::Param(FCommandLine::Get(), TEXT("PIEVIACONSOLE"));
}


bool UWorld::IsPlayInMobilePreview() const
{
#if WITH_EDITOR
	if (FParse::Param(FCommandLine::Get(), TEXT("featureleveles31")))
	{
		return true;
	}

	const FStaticShaderPlatform ShaderPlatform = Scene ? Scene->GetShaderPlatform(): GMaxRHIShaderPlatform;
	if (FDataDrivenShaderPlatformInfo::GetIsPreviewPlatform(ShaderPlatform) && IsMobilePlatform(ShaderPlatform))
	{
		return true;
	}
#endif // WITH_EDITOR
	return FParse::Param(FCommandLine::Get(), TEXT("simmobile"));
}

bool UWorld::IsGameWorld() const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview || WorldType == EWorldType::GameRPC;
}

bool UWorld::IsEditorWorld() const
{
	return WorldType == EWorldType::Editor || WorldType == EWorldType::EditorPreview || WorldType == EWorldType::PIE;
}

bool UWorld::IsPreviewWorld() const
{
	return WorldType == EWorldType::EditorPreview || WorldType == EWorldType::GamePreview;
}

bool UWorld::UsesGameHiddenFlags() const
{
	return IsGameWorld();
}

FString UWorld::GetAddressURL() const
{
	return FString::Printf( TEXT("%s"), *URL.GetHostPortString() );
}

FString UWorld::RemovePIEPrefix(const FString &Source, int32* OutPIEInstanceID)
{
	// PIE prefix is: UEDPIE_X_MapName (where X is some decimal number)
	const FString LookFor = PLAYWORLD_PACKAGE_PREFIX;
	if (OutPIEInstanceID)
	{
		*OutPIEInstanceID = INDEX_NONE;
	}
	int32 idx = Source.Find(LookFor);
	if (idx >= 0)
	{
		int32 end = idx + LookFor.Len();
		if ((end >= Source.Len()) || (Source[end] != '_'))
		{
			UE_LOGF(LogWorld, Warning, "Looks like World path invalid PIE prefix (expected '_' characeter after PIE prefix): %ls", *Source);
			return Source;
		}
		int32 PIEInstanceIDStartIndex = end + 1;
		for (++end; (end < Source.Len()) && (Source[end] != '_'); ++end)
		{
			if ((Source[end] < '0') || (Source[end] > '9'))
			{
				UE_LOGF(LogWorld, Warning, "Looks like World have invalid PIE prefix (PIE instance not number): %ls", *Source);
				return Source;
			}
		}
		if (end >= Source.Len())
		{
			UE_LOGF(LogWorld, Warning, "Looks like World path invalid PIE prefix (can't find end of PIE prefix): %ls", *Source);
			return Source;
		}
		if (OutPIEInstanceID && (end > PIEInstanceIDStartIndex))
		{
			const int32 PIEInstanceIDCount = end - PIEInstanceIDStartIndex;
			const FString PIEInstanceIDStr = Source.Mid(PIEInstanceIDStartIndex, PIEInstanceIDCount);
			TTypeFromString<int32>::FromString(*OutPIEInstanceID, *PIEInstanceIDStr);
		}
		const FString Prefix = Source.Left(idx);
		const FString Suffix = Source.Right(Source.Len() - end - 1);
		return Prefix + Suffix;
	}

	return Source;
}

UWorld* UWorld::FindWorldInPackage(UPackage* Package)
{
	UWorld* Result = nullptr;
	ForEachObjectWithPackage(Package, [&Result](UObject* Object)
	{
		Result = Cast<UWorld>(Object);
		return !Result;
	}, EGetObjectsFlags::None);
	return Result;
}

bool UWorld::IsWorldOrWorldExternalPackage(UPackage* Package)
{
	bool bResult = false;
	ForEachObjectWithPackage(Package, [&bResult](UObject* Object)
	{
		bResult = !!Cast<UWorld>(Object) || !!Object->GetTypedOuter<UWorld>();
		return !bResult;
	}, EGetObjectsFlags::None);
	return bResult;
}

UWorld* UWorld::FollowWorldRedirectorInPackage(UPackage* Package, UObjectRedirector** OptionalOutRedirector)
{
	UWorld* RetVal = nullptr;
	TArray<UObject*> PotentialRedirectors;
	GetObjectsWithPackage(Package, PotentialRedirectors, EGetObjectsFlags::None);
	for (auto ObjIt = PotentialRedirectors.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		UObjectRedirector* Redirector = Cast<UObjectRedirector>(*ObjIt);
		if (Redirector)
		{
			RetVal = Cast<UWorld>(Redirector->DestinationObject);
			if ( RetVal )
			{
				// Patch up the WorldType if found in the PreLoad map
				EWorldType::Type* PreLoadWorldType = UWorld::WorldTypePreLoadMap.Find(Redirector->GetOuter()->GetFName());
				if (PreLoadWorldType)
				{
					RetVal->WorldType = *PreLoadWorldType;
				}

				if ( OptionalOutRedirector )
				{
					(*OptionalOutRedirector) = Redirector;
				}
				break;
			}
		}
	}

	return RetVal;
}

#if WITH_EDITOR


void UWorld::BroadcastSelectedLevelsChanged() 
{ 
	if( bBroadcastSelectionChange )
	{
		SelectedLevelsChangedEvent.Broadcast(); 
	}
}


void UWorld::SelectLevel( ULevel* InLevel )
{
	check( InLevel );
	if( IsLevelSelected( InLevel ) == false )
	{
		SelectedLevels.AddUnique( InLevel );
		BroadcastSelectedLevelsChanged();
	}
}

void UWorld::DeSelectLevel( ULevel* InLevel )
{
	check( InLevel );
	if( IsLevelSelected( InLevel ) == true )
	{
		SelectedLevels.Remove( InLevel );
		BroadcastSelectedLevelsChanged();
	}
}

bool UWorld::IsLevelSelected( ULevel* InLevel ) const
{
	return SelectedLevels.Find( InLevel ) != INDEX_NONE;
}

int32 UWorld::GetNumSelectedLevels() const 
{
	return SelectedLevels.Num();
}

TArray<TObjectPtr<class ULevel>>& UWorld::GetSelectedLevels()
{
	return SelectedLevels;
}

ULevel* UWorld::GetSelectedLevel( int32 InLevelIndex ) const
{
	check(SelectedLevels[ InLevelIndex ]);
	return SelectedLevels[ InLevelIndex ];
}

void UWorld::SetSelectedLevels( const TArray<class ULevel*>& InLevels )
{
	// Disable the broadcasting of selection changes - we will send a single broadcast when we have finished
	bBroadcastSelectionChange = false;
	SelectedLevels.Empty();
	for (int32 iSelected = 0; iSelected <  InLevels.Num(); iSelected++)
	{
		SelectLevel( InLevels[ iSelected ]);
	}
	// Enable the broadcasting of selection changes
	bBroadcastSelectionChange = true;
	// Broadcast we have change the selections
	BroadcastSelectedLevelsChanged();
}

FDelegateHandle UWorld::AddOnFeatureLevelChangedHandler(const FOnFeatureLevelChanged::FDelegate& InHandler)
{
	return OnFeatureLevelChanged.Add(InHandler);
}

void UWorld::RemoveOnFeatureLevelChangedHandler(FDelegateHandle InHandle)
{
	OnFeatureLevelChanged.Remove(InHandle);
}

#endif // WITH_EDITOR

/**
 * Jumps the server to new level.  If bAbsolute is true and we are using seamless traveling, we
 * will do an absolute travel (URL will be flushed).
 *
 * @param URL the URL that we are traveling to
 * @param bAbsolute whether we are using relative or absolute travel
 * @param bShouldSkipGameNotify whether to notify the clients/game or not
 */
bool UWorld::ServerTravel(const FString& FURL, bool bAbsolute, bool bShouldSkipGameNotify)
{
	AGameModeBase* GameMode = GetAuthGameMode();
	
	if (GameMode != nullptr && !GameMode->CanServerTravel(FURL, bAbsolute))
	{
		return false;
	}

	// Set the next travel type to use
	NextTravelType = bAbsolute ? TRAVEL_Absolute : TRAVEL_Relative;

	// if we're not already in a level change, start one now
	// If the bShouldSkipGameNotify is there, then don't worry about seamless travel recursion
	// and accept that we really want to travel
	if (NextURL.IsEmpty() && (!IsInSeamlessTravel() || bShouldSkipGameNotify))
	{
		NextURL = FURL;
		if (GameMode != NULL)
		{
			// Skip notifying clients if requested
			if (!bShouldSkipGameNotify)
			{
				GameMode->ProcessServerTravel(FURL, bAbsolute);
			}
		}
		else
		{
			NextSwitchCountdown = 0;
		}
	}

	return true;
}

void UWorld::SetNavigationSystem(UNavigationSystemBase* InNavigationSystem)
{
	if (NavigationSystem != NULL && NavigationSystem != InNavigationSystem)
	{
		NavigationSystem->CleanUp(FNavigationSystem::ECleanupMode::CleanupWithWorld);
	}

	UE_LOGF(LogNavigation, Verbose, "   %s to %ls", __FUNCTION__, *GetNameSafe(NavigationSystem));
	NavigationSystem = InNavigationSystem;
}

#if WITH_EDITORONLY_DATA
/** Set the CurrentLevel for this world. **/
bool UWorld::SetCurrentLevel( class ULevel* InLevel )
{
	bool bChanged = false;
	if( CurrentLevel != InLevel )
	{
		ULevel* OldLevel = CurrentLevel;
		CurrentLevel = InLevel;
		bChanged = true;

		FWorldDelegates::OnCurrentLevelChanged.Broadcast(CurrentLevel, OldLevel, this);
	}
	return bChanged;
}
#endif

/** Get the CurrentLevel for this world. **/
ULevel* UWorld::GetCurrentLevel() const
{
#if WITH_EDITORONLY_DATA
	return CurrentLevel;
#else
	return PersistentLevel;
#endif
}

ENetMode UWorld::InternalGetNetMode() const
{
	if (NetDriver != nullptr)
	{
		const bool bIsClientOnly = IsRunningClientOnly();
		return bIsClientOnly ? NM_Client : NetDriver->GetNetMode();
	}

	// Use replay driver's net mode if we're in playback or ticking recording
	if (DemoNetDriver && (DemoNetDriver->IsPlaying() || (DemoNetDriver->IsRecording() && DemoNetDriver->IsInTick())))
	{
		return DemoNetDriver->GetNetMode();
	}

	ENetMode URLNetMode = AttemptDeriveFromURL();
#if WITH_EDITOR
	if (WorldType == EWorldType::PIE && (URLNetMode == NM_Standalone || PlayInEditorNetMode == NM_DedicatedServer))
	{
		// If we're early in startup before the net driver exists and there is no URL override
		// or this is a dedicated server, use the mode we were first created with
		// This is required for dedicated server/listen worlds so it is correct for InitWorld
		return PlayInEditorNetMode;
	}
#endif
	return URLNetMode;
}

bool UWorld::IsRecordingClientReplay() const
{
	if (GetNetDriver() != nullptr && !GetNetDriver()->IsServer())
	{
		if (DemoNetDriver != nullptr && DemoNetDriver->IsServer())
		{
			return true;
		}
	}

	return false;
}

bool UWorld::IsPlayingClientReplay() const
{
	return (DemoNetDriver != nullptr && DemoNetDriver->IsPlayingClientReplay());
}

ENetMode UWorld::AttemptDeriveFromURL() const
{
	if (GEngine != nullptr)
	{
		FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(this);

		if (WorldContext != nullptr)
		{
			// NetMode can be derived from the NextURL if it exists
			if (NextURL.Len() > 0)
			{
				FURL NextLevelURL(&WorldContext->LastURL, *NextURL, NextTravelType);

				if (NextLevelURL.Valid)
				{
					if (NextLevelURL.HasOption(TEXT("listen")))
					{
						return NM_ListenServer;
					}
					else if (NextLevelURL.Host.Len() > 0)
					{
						return NM_Client;
					}
				}
			}
			// NetMode can be derived from the PendingNetURL if it exists
			else if (WorldContext->PendingNetGame != nullptr && WorldContext->PendingNetGame->URL.Valid)
			{
				if (WorldContext->PendingNetGame->URL.HasOption(TEXT("listen")))
				{
					return NM_ListenServer;
				}
				else if (WorldContext->PendingNetGame->URL.Host.Len() > 0)
				{
					return NM_Client;
				}
			}
		}
	}

	return NM_Standalone;
}

void UWorld::SetGameState(AGameStateBase* NewGameState)
{
	if (NewGameState == GameState)
	{
		return;
	}

	GameState = NewGameState;

	// Set the GameState on the LevelCollection it's associated with.
	if (NewGameState != nullptr)
	{
	    const ULevel* const CachedLevel = NewGameState->GetLevel();
		if(CachedLevel != nullptr)
		{
	        FLevelCollection* const FoundCollection = CachedLevel->GetCachedLevelCollection();
	        if (FoundCollection)
	        {
		        FoundCollection->SetGameState(NewGameState);
        
		        // For now the static levels use the same GameState as the source dynamic levels.
		        if (FoundCollection->GetType() == ELevelCollectionType::DynamicSourceLevels)
		        {
					if (FLevelCollection* StaticLevels = FindCollectionByType(ELevelCollectionType::StaticLevels))
					{
						StaticLevels->SetGameState(NewGameState);
					}
		        }
	        }
		}
	}

	GameStateSetEvent.Broadcast(GameState);
}

void UWorld::CopyGameState(AGameModeBase* FromGameMode, AGameStateBase* FromGameState)
{
	AuthorityGameMode = FromGameMode;
	SetGameState(FromGameState);
}

void UWorld::GetLightMapsAndShadowMaps(ULevel* Level, TArray<UTexture2D*>& OutLightMapsAndShadowMaps, bool bForceLazyLoad /*= true*/)
{
	class FFindLightmapsArchive : public FArchiveUObject
	{
		/** The array of textures discovered */
		TArray<UTexture2D*>& TextureList;
		bool bForceLazyLoad;

	public:
		FFindLightmapsArchive(UObject* InSearch, TArray<UTexture2D*>& OutTextureList, bool bInForceLazyLoad)
			: TextureList(OutTextureList)
			, bForceLazyLoad(bInForceLazyLoad)
		{
			ArIsObjectReferenceCollector = true;
			ArIsModifyingWeakAndStrongReferences = true; // While we are not modifying them, we want to follow weak references as well

			// Don't bother searching through the object's references if there's no objects of the types we're looking for
			TArray<UObject*> Objects;
			GetObjectsOfClass(ULightMapTexture2D::StaticClass(), Objects);
			GetObjectsOfClass(UShadowMapTexture2D::StaticClass(), Objects);
			GetObjectsOfClass(ULightMapVirtualTexture2D::StaticClass(), Objects);

			if (Objects.Num())
			{
				for (FThreadSafeObjectIterator It; It; ++It)
				{
					It->Mark(OBJECTMARK_TagExp);
				}

				*this << InSearch;
			}
		}

		FArchive& operator<<(class UObject*& Obj)
		{
			// Don't check null references or objects already visited. Also, skip UWorlds as they will pull in more levels than desired
			// Also skip StaticMesh as it will cause stalls during async compilation and they do not contain any lightmaps anyway.
			if (Obj != NULL && Obj->HasAnyMarks(OBJECTMARK_TagExp) && !Obj->IsA<UWorld>() && !Obj->IsA<UStaticMesh>())
			{
				if (Obj->IsA<ULightMapTexture2D>() ||
					Obj->IsA<UShadowMapTexture2D>() ||
					Obj->IsA<ULightMapVirtualTexture2D>())
				{
					UTexture2D* Tex = Cast<UTexture2D>(Obj);
					if ( ensure(Tex) )
					{
						TextureList.Add(Tex);
					}
				}

				Obj->UnMark(OBJECTMARK_TagExp);
				Obj->Serialize(*this);
			}

			return *this;
		}

		FArchive& operator<<(FObjectPtr& Obj)
		{
			// @TODO: OBJPTR: Could some or all of this behavior be generalized for use in other reference collectors?
			//			Could add another Ar* flag to control whether lazy loads get resolved.  Could add a derivative
			//			of FArchiveUObject that filters references by type.

			// Don't check null references or objects already visited. Also, skip UWorlds as they will pull in more levels than desired
			// Also skip StaticMesh as it will cause stalls during async compilation and they do not contain any lightmaps anyway.
			if (Obj && !Obj.IsA<UWorld>() && !Obj.IsA<UStaticMesh>())
			{
				if (Obj.IsA<ULightMapTexture2D>() ||
					Obj.IsA<UShadowMapTexture2D>() ||
					Obj.IsA<ULightMapVirtualTexture2D>())
				{
					UTexture2D* Tex = Cast<UTexture2D>(Obj.Get());
					if (ensure(Tex))
					{
						TextureList.Add(Tex);
					}
				}
				else if (IsObjectHandleResolved(Obj.GetHandle()) || bForceLazyLoad)
				{
					return FArchiveUObject::operator<<(Obj);
				}
			}
			return *this;
		}
	};

	UObject* SearchObject = Level;
	if ( !SearchObject )
	{
		SearchObject = PersistentLevel;
	}

	FFindLightmapsArchive FindArchive(SearchObject, OutLightMapsAndShadowMaps, bForceLazyLoad);
}

void UWorld::CreateFXSystem()
{
	if ( !IsRunningDedicatedServer() && !IsRunningCommandlet() )
	{
		FXSystem = FFXSystemInterface::Create(GetFeatureLevel(), Scene);
	}
	else
	{
		FXSystem = NULL;
		Scene->SetFXSystem(NULL);
	}
}

FLevelCollection& UWorld::FindOrAddCollectionForLevelStreaming(const ULevelStreaming* Level)
{
	ELevelCollectionType Type = ELevelCollectionType::DynamicSourceLevels;
	if (bCreateStaticLevelCollection && Level->bIsStatic)
	{
		Type = ELevelCollectionType::StaticLevels;
	}
	
	return FindOrAddCollectionByType(Type);
}

FLevelCollection& UWorld::FindOrAddCollectionByType(const ELevelCollectionType InType)
{
	return LevelCollections[FindOrAddCollectionByType_Index(InType)];
}

int32 UWorld::FindOrAddCollectionByType_Index(const ELevelCollectionType InType)
{
	const int32 FoundIndex = FindCollectionIndexByType(InType);

	if (FoundIndex != INDEX_NONE)
	{
		return FoundIndex;
	}

	// Static collections should not be created if that is disabled
	ensure(InType != ELevelCollectionType::StaticLevels || bCreateStaticLevelCollection);

	// Not found, add a new one.
	FLevelCollection NewLC;
	NewLC.SetType(InType);
	return LevelCollections.Add(MoveTemp(NewLC));
}

FLevelCollection* UWorld::FindCollectionByType(const ELevelCollectionType InType)
{
	return const_cast<FLevelCollection*>(const_cast<const UWorld*>(this)->FindCollectionByType(InType));
}

const FLevelCollection* UWorld::FindCollectionByType(const ELevelCollectionType InType) const
{
	for (const FLevelCollection& LC : LevelCollections)
	{
		if (LC.GetType() == InType)
		{
			return &LC;
		}
	}

	return nullptr;
}

int32 UWorld::FindCollectionIndexByType(const ELevelCollectionType InType) const
{
	return LevelCollections.IndexOfByPredicate([InType](const FLevelCollection& Collection)
	{
		return Collection.GetType() == InType;
	});
}

const FLevelCollection* UWorld::GetActiveLevelCollection() const
{
	if (LevelCollections.IsValidIndex(ActiveLevelCollectionIndex))
	{
		return &LevelCollections[ActiveLevelCollectionIndex];
	}

	return nullptr;
}

void UWorld::SetActiveLevelCollection(int32 LevelCollectionIndex)
{
	// Only check if collection actually changes
	if (LevelCollectionIndex == ActiveLevelCollectionIndex)
	{
		return;
	}

	ActiveLevelCollectionIndex = LevelCollectionIndex;
	const FLevelCollection* const ActiveLevelCollection = GetActiveLevelCollection();

	if (ActiveLevelCollection == nullptr)
	{
		return;
	}

	PersistentLevel = ActiveLevelCollection->GetPersistentLevel();
#if WITH_EDITORONLY_DATA
	if (IsGameWorld())
	{
		SetCurrentLevel(ActiveLevelCollection->GetPersistentLevel());
	}
#endif
	GameState = ActiveLevelCollection->GetGameState();
	NetDriver = ActiveLevelCollection->GetNetDriver();
	DemoNetDriver = ActiveLevelCollection->GetDemoNetDriver();

	// Our net drivers may have been destroyed during the scope
	if (NetDriver && NetDriver->NetDriverName != NAME_None)
	{
		UNetDriver* TempNetDriver = GEngine->FindNamedNetDriver(this, NetDriver->NetDriverName);
		if (TempNetDriver != NetDriver)
		{
			UE_LOGF(LogWorld, Warning, "SetActiveLevelCollection attempted to use an out of date NetDriver: %ls", *(NetDriver->NetDriverName.ToString()));
			NetDriver = TempNetDriver;
		}
	}

	if (DemoNetDriver && DemoNetDriver->NetDriverName != NAME_None)
	{
		UDemoNetDriver* TempDemoNetDriver = Cast<UDemoNetDriver>(GEngine->FindNamedNetDriver(this, DemoNetDriver->NetDriverName));
		if (TempDemoNetDriver != DemoNetDriver)
		{
			UE_LOGF(LogWorld, Warning, "SetActiveLevelCollection attempted to use an out of date DemoNetDriver: %ls", *(DemoNetDriver->NetDriverName.ToString()));
			DemoNetDriver = TempDemoNetDriver;
		}
	}
}

static ULevel* DuplicateLevelWithPrefix(ULevel* InLevel, int32 InstanceID )
{
	if (!InLevel || !InLevel->GetOutermost())
	{
		return nullptr;
	}

	const double DuplicateStart = FPlatformTime::Seconds();

	UWorld* OriginalOwningWorld = CastChecked<UWorld>(InLevel->GetOuter());
	UPackage* OriginalPackage = InLevel->GetOutermost();

	const FString OriginalPackageName = OriginalPackage->GetName();

	// Use a PIE prefix for the new package
	const FString PrefixedPackageName = UWorld::ConvertToPIEPackageName( OriginalPackageName, InstanceID );

	// Create a package for duplicated level
	UPackage* NewPackage = CreatePackage( *PrefixedPackageName );
	NewPackage->SetPackageFlags( PKG_PlayInEditor );
	NewPackage->SetPIEInstanceID(InstanceID);
	NewPackage->SetLoadedPath(OriginalPackage->GetLoadedPath());
#if WITH_EDITORONLY_DATA
	NewPackage->SetSavedHash( OriginalPackage->GetSavedHash() );
#endif
	NewPackage->MarkAsFullyLoaded();

	FSoftObjectPath::AddPIEPackageName(NewPackage->GetFName());

	FTemporaryPlayInEditorIDOverride IDHelper(InstanceID);

	// Create "vestigial" world for the persistent level - it's OwningWorld will still be the main world,
	// but we're treating it like a streaming level (even though it's a duplicate of the persistent level).
	UWorld* NewWorld = NewObject<UWorld>(NewPackage, OriginalOwningWorld->GetFName());
	NewWorld->SetFlags(RF_Transactional);
	NewWorld->WorldType = EWorldType::Game;
	NewWorld->SetFeatureLevel(ERHIFeatureLevel::Num);

	ULevel::StreamedLevelsOwningWorld.Add(NewPackage->GetFName(), OriginalOwningWorld);

	FObjectDuplicationParameters Parameters( InLevel, NewWorld );
		
	Parameters.DestName			= InLevel->GetFName();
	Parameters.DestClass		= InLevel->GetClass();
	Parameters.PortFlags		= PPF_DuplicateForPIE;
	Parameters.DuplicateMode	= EDuplicateMode::PIE;

	ULevel* NewLevel = CastChecked<ULevel>( StaticDuplicateObjectEx( Parameters ) );

	ULevel::StreamedLevelsOwningWorld.Remove(NewPackage->GetFName());

	// Fixup model components. The index buffers have been created for the components in the source world and the order
	// in which components were post-loaded matters. So don't try to guarantee a particular order here, just copy the
	// elements over.
	if ( NewLevel->Model != NULL
			&& NewLevel->Model == InLevel->Model
			&& NewLevel->ModelComponents.Num() == InLevel->ModelComponents.Num() )
	{
		NewLevel->Model->ClearLocalMaterialIndexBuffersData();
		for ( int32 ComponentIndex = 0; ComponentIndex < NewLevel->ModelComponents.Num(); ++ComponentIndex )
		{
			UModelComponent* SrcComponent = InLevel->ModelComponents[ComponentIndex];
			UModelComponent* DestComponent = NewLevel->ModelComponents[ComponentIndex];
			DestComponent->CopyElementsFrom( SrcComponent );
		}
	}

	const double DuplicateEnd = FPlatformTime::Seconds();
	const double TotalSeconds = ( DuplicateEnd - DuplicateStart );

	UE_LOGF( LogNet, Log, "DuplicateLevelWithPrefix. TotalSeconds: %2.2f", TotalSeconds );

	return NewLevel;
}

void UWorld::DuplicateRequestedLevels(const FName MapName)
{
	if (GEngine->Experimental_ShouldPreDuplicateMap(MapName))
	{
		if (!IsPartitionedWorld())
		{
			// Duplicate the persistent level and only dynamic levels, but don't add them to the world.
			FLevelCollection DuplicateLevels;
			DuplicateLevels.SetType(ELevelCollectionType::DynamicDuplicatedLevels);
			DuplicateLevels.SetIsVisible(false);
			ULevel* const DuplicatePersistentLevel = DuplicateLevelWithPrefix(PersistentLevel, 1);
			if (!DuplicatePersistentLevel)
			{
				UE_LOGF(LogWorld, Warning, "UWorld::DuplicateRequestedLevels: failed to duplicate persistent level %ls. No duplicate level collection will be created.",
					*GetFullNameSafe(PersistentLevel));
				return;
			}
			// Don't tell the server about this level
			DuplicatePersistentLevel->bClientOnlyVisible = true;
			DuplicateLevels.SetPersistentLevel(DuplicatePersistentLevel);
			DuplicateLevels.AddLevel(DuplicatePersistentLevel);

			for (ULevelStreaming* StreamingLevel : StreamingLevels)
			{
				if (StreamingLevel && !StreamingLevel->bIsStatic)
				{
					ULevel* DuplicatedLevel = DuplicateLevelWithPrefix(StreamingLevel->GetLoadedLevel(), 1);
					if (!DuplicatedLevel)
					{
						UE_LOGF(LogWorld, Warning, "UWorld::DuplicateRequestedLevels: failed to duplicate streaming level %ls. No duplicate level collection will be created.",
							*GetFullNameSafe(StreamingLevel->GetLoadedLevel()));
						return;
					}
					// Don't tell the server about these levels
					DuplicatedLevel->bClientOnlyVisible = true;
					DuplicateLevels.AddLevel(DuplicatedLevel);
				}
			}

			LevelCollections.Add(MoveTemp(DuplicateLevels));
		}
		else
		{
			UE_LOGF(LogWorld, Error, "UWorld::DuplicateRequestedLevels: Attempted to duplicate streaming levels for partitioned world. This is not a supported operation.");
		}	
	}
}

#if WITH_EDITOR
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void UWorld::ChangeFeatureLevel(ERHIFeatureLevel::Type InFeatureLevel, bool bShowSlowProgressDialog, bool /*bForceUpdate*/)
{
	ShaderPlatformChanged(GShaderPlatformForFeatureLevel[InFeatureLevel], bShowSlowProgressDialog);
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void UWorld::ShaderPlatformChanged(EShaderPlatform InShaderPlatform, bool bShowSlowProgressDialog)
{
	if (InShaderPlatform == EShaderPlatform::SP_NumPlatforms)
	{
		UE_LOGF(LogWorld, Warning, "ShaderPlatformChanged called without a valid EShaderPlatform, defaulting to GMaxRHIShaderPlatform (%ls). Please update the call site to pass an explicit EShaderPlatform.", *LexToString(GMaxRHIShaderPlatform));
		InShaderPlatform = GMaxRHIShaderPlatform;
	}

	if (Scene && Scene->GetShaderPlatform() != InShaderPlatform)
	{
		ERHIFeatureLevel::Type InFeatureLevel = GetMaxSupportedFeatureLevel(InShaderPlatform);
		FText FriendlyNamePreviewFrom = FDataDrivenShaderPlatformInfo::GetFriendlyName(Scene->GetShaderPlatform());
		FText FriendlyNamePreviewTo = FDataDrivenShaderPlatformInfo::GetFriendlyName(InShaderPlatform);
		UE_LOGF(LogWorld, Log, "Changing Preview Shader Platform from %ls to %ls", *FriendlyNamePreviewFrom.ToString(), *FriendlyNamePreviewTo.ToString());
		FScopedSlowTask SlowTask(100.f, NSLOCTEXT("Engine", "ChangingPreviewRenderingLevelMessage", "Changing Preview Shader Platform"), bShowSlowProgressDialog);
		SlowTask.MakeDialog();
		{
			SlowTask.EnterProgressFrame(10.0f);
			// Give all scene components the opportunity to prepare for pending feature level change.
			for (TObjectIterator<USceneComponent> It; It; ++It)
			{
				USceneComponent* SceneComponent = *It;
				if (SceneComponent->GetWorld() == this)
				{
					SceneComponent->PreFeatureLevelChange(InFeatureLevel);
				}
			}

			SlowTask.EnterProgressFrame(10.0f);
			FGlobalComponentReregisterContext RecreateComponents;
			// Finish any deferred / async render cleanup work.
			GetRendererModule().PerFrameCleanupIfSkipRenderer();
			FlushRenderingCommands();

			SetFeatureLevel(InFeatureLevel);

			SlowTask.EnterProgressFrame(10.0f);
			RecreateScene(InFeatureLevel);

			InvalidateAllSkyCaptures();

			OnFeatureLevelChanged.Broadcast(GetFeatureLevel());

			SlowTask.EnterProgressFrame(10.0f);
			TriggerStreamingDataRebuild();
		}
	}
}

void UWorld::RecreateScene(ERHIFeatureLevel::Type InFeatureLevel, bool bBroadcastChange)
{
	if (Scene)
	{
		ensure(InFeatureLevel == GetFeatureLevel());

		FWorldDelegates::OnPreRecreateScene.Broadcast(this);

		for (ULevel* Level : Levels)
		{
			Level->ReleaseRenderingResources();
		}

		//Ensure we've destroyed our FXSystem before we change Scene on the world.
		bool bCreateFXSystem = false;
		if (FXSystem)
		{
			bCreateFXSystem = true;
			FFXSystemInterface::MarkPendingKill(FXSystem.Get());
			FXSystem = nullptr;
			Scene->SetFXSystem(nullptr);
		}

		ReleaseScene();

		if (bBroadcastChange)
		{
			FRenderResource::ChangeFeatureLevel(InFeatureLevel);
		}

		IRendererModule& RendererModule = GetRendererModule();
		RendererModule.AllocateScene(this, bRequiresHitProxies, bCreateFXSystem, InFeatureLevel);
		
#if WITH_STATE_STREAM
		StateStreamManager->Game_SetLaneUserData(LaneId, Scene);
#endif

		for (ULevel* Level : Levels)
		{
			Level->InitializeRenderingResources();
			Level->PrecomputedVisibilityHandler.UpdateScene(Scene);
			Level->PrecomputedVolumeDistanceField.UpdateScene(Scene);
		}

		FWorldDelegates::OnPostRecreateScene.Broadcast(this);
	}
}

// Recreate the editor world's FScene with a null scene interface to drop extra GPU memory during PIE
void UWorld::PurgeScene()
{
	if (CVarPurgeEditorSceneDuringPIE.GetValueOnGameThread() == 0)
	{
		return;
	}

	if (!bPurgedScene && this->IsEditorWorld())
	{
		// Clear out Slate's active scenes list since these ptrs no longer reference valid scenes.
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().FlushRenderState();
		}

		FGlobalComponentReregisterContext RecreateComponents;

		// Finish any deferred / async render cleanup work.
		GetRendererModule().PerFrameCleanupIfSkipRenderer();
		FlushRenderingCommands();

		const bool bOldVal = GUsingNullRHI;
		GUsingNullRHI = true;
		{
			RecreateScene(GetFeatureLevel(), false /* bBroadcastChange */);
		}
		GUsingNullRHI = bOldVal;

		InvalidateAllSkyCaptures();
		TriggerStreamingDataRebuild();

		bPurgedScene = true;
	}
}

// Restore the purged editor world FScene back to the proper GPU representation
void UWorld::RestoreScene()
{
	if (bPurgedScene)
	{
		FGlobalComponentReregisterContext RecreateComponents;

		// Finish any deferred / async render cleanup work.
		GetRendererModule().PerFrameCleanupIfSkipRenderer();
		FlushRenderingCommands();

		RecreateScene(GetFeatureLevel(), false /* bBroadcastChange */);

		InvalidateAllSkyCaptures();
		TriggerStreamingDataRebuild();

		bPurgedScene = false;
	}
}

void UWorld::OnAddExtraObjectsToDelete(const TArray<UObject*>& InObjectsToDelete, TSet<UObject*>& OutSecondaryObjects)
{	
	for (const UObject* Object : InObjectsToDelete)
	{
		if (const UWorld* World = Cast<UWorld>(Object))
		{
			if (World->PersistentLevel && World->PersistentLevel->MapBuildData)
			{
				// Delete MapBuildData together with maps
				OutSecondaryObjects.Add(World->PersistentLevel->MapBuildData);
			}
		}
	}
}

void UWorld::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

	if (PersistentLevel && PersistentLevel->OwningWorld)
	{
		if (ULevelScriptBlueprint* Blueprint = PersistentLevel->GetLevelScriptBlueprint(true))
		{
			Blueprint->GetAssetRegistryTags(Context);
		}
		// If there are no blueprints FiBData will be empty, the search manager will treat this as indexed
								
		if (UWorldPartition* WorldPartition = GetWorldPartition())
		{
			WorldPartition->AppendAssetRegistryTags(Context);
		}
		else
		{
			FBox LevelBounds;
			if (PersistentLevel->LevelBoundsActor.IsValid())
			{
				LevelBounds = PersistentLevel->LevelBoundsActor.Get()->GetComponentsBoundingBox();
			}
			else
			{
				LevelBounds = ALevelBounds::CalculateLevelBounds(PersistentLevel);
			}

			FVector LevelBoundsLocation;
			FVector LevelBoundsExtent;
			LevelBounds.GetCenterAndExtents(LevelBoundsLocation, LevelBoundsExtent);

			static const FName NAME_LevelBoundsLocation("LevelBoundsLocation");
			Context.AddTag(FAssetRegistryTag(NAME_LevelBoundsLocation, LevelBoundsLocation.ToCompactString(), FAssetRegistryTag::TT_Hidden));

			static const FName NAME_LevelBoundsExtent("LevelBoundsExtent");
			Context.AddTag(FAssetRegistryTag(NAME_LevelBoundsExtent, LevelBoundsExtent.ToCompactString(), FAssetRegistryTag::TT_Hidden));
		}
	
		if (PersistentLevel->IsUsingExternalActors())
		{
			static const FName NAME_LevelIsUsingExternalActors("LevelIsUsingExternalActors");
			Context.AddTag(FAssetRegistryTag(NAME_LevelIsUsingExternalActors, TEXT("1"), FAssetRegistryTag::TT_Hidden));
		}

		if (PersistentLevel->IsUsingActorFolders())
		{
			static const FName NAME_LevelIsUsingActorFolders("LevelIsUsingActorFolders");
			Context.AddTag(FAssetRegistryTag(NAME_LevelIsUsingActorFolders, TEXT("1"), FAssetRegistryTag::TT_Hidden));
		}

		if (AWorldSettings* WorldSettings = GetWorldSettings(/*bCheckStreamingPersistent*/false, /*bChecked*/false))
		{
			FVector LevelInstancePivotOffset = WorldSettings ? WorldSettings->LevelInstancePivotOffset : FVector::ZeroVector;
			if (!LevelInstancePivotOffset.IsNearlyZero())
			{
				static const FName NAME_LevelInstancePivotOffset("LevelInstancePivotOffset");
				Context.AddTag(FAssetRegistryTag(NAME_LevelInstancePivotOffset, LevelInstancePivotOffset.ToCompactString(), FAssetRegistryTag::TT_Hidden));
			}
		}
	}

	FName DateModifiedTagName("DateModified");
	if (Context.IsSaving() && !Context.IsProceduralSave())
	{
		FDateTime AssetDateModified = FDateTime::Now();
		Context.AddTag(FAssetRegistryTag(DateModifiedTagName, AssetDateModified.ToString(), FAssetRegistryTag::TT_Chronological, FAssetRegistryTag::TD_Date));
	}
	else
	{
		// To prevent cook indeterminism, loads and procedural saves such as cook saves do not alter the DateModified
		// tag if it already exists, and they set it to a deterministic value if it does not exist. Note that
		// cook-generated worlds such as WorldPartition streaming chunks will have this deterministic value.
		if (!Context.FindTag(DateModifiedTagName))
		{
			FDateTime ZeroTime = FDateTime::MinValue();
			Context.AddTag(FAssetRegistryTag(DateModifiedTagName, ZeroTime.ToString(), FAssetRegistryTag::TT_Chronological, FAssetRegistryTag::TD_Date));
		}
	}

	FWorldDelegates::GetAssetTagsWithContext.Broadcast(this, Context);
}

void UWorld::GetAssetRegistryTagMetadata(TMap<FName, FAssetRegistryTagMetadata>& OutMetadata) const
{
	Super::GetAssetRegistryTagMetadata(OutMetadata);

	OutMetadata.Add("DateModified").SetDisplayName(LOCTEXT("DateModifiedLabel", "Date Modified"));
}

void UWorld::GetExtendedAssetRegistryTagsForSave(const ITargetPlatform* TargetPlatform, TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetExtendedAssetRegistryTagsForSave(TargetPlatform, OutTags);

	if (!PersistentLevel->IsUsingExternalActors())
	{
		TArray<FString> ActorsMetaData;
		for (AActor* Actor : PersistentLevel->Actors)
		{
			if (IsValid(Actor) && Actor->SupportsExternalPackaging())
			{
				FWorldPartitionActorDescUtils::FActorDescInitParams ActorDescInitParams(Actor);
				ActorsMetaData.Add(ActorDescInitParams.ToString());
			}
		}

		if (ActorsMetaData.Num())
		{
			static FName NAME_ActorsMetaData("ActorsMetaData");
			const FString ActorsMetaDataStr = FString::Join(ActorsMetaData, TEXT(";"));
			OutTags.Add(UObject::FAssetRegistryTag(NAME_ActorsMetaData, ActorsMetaDataStr, UObject::FAssetRegistryTag::TT_Hidden));
		}
	}
}

void UWorld::ThreadedPostLoadAssetRegistryTagsOverride(FPostLoadAssetRegistryTagsContext& Context) const
{
	Super::ThreadedPostLoadAssetRegistryTagsOverride(Context);

	// GetAssetRegistryTags appends the LevelBlueprint tags to the World's tags, so we also have to run the Blueprint ThreadedPostLoadAssetRegistryTagsOverride
	UBlueprint::PostLoadBlueprintAssetRegistryTags(Context);
}

bool UWorld::IsNameStableForNetworking() const
{
	return bIsNameStableForNetworking || Super::IsNameStableForNetworking();
}
#endif

bool UWorld::ResolveSubobject(const TCHAR* SubObjectPath, UObject*& OutObject, bool bLoadIfExists)
{
	FString SubObjectName;
	FString SubObjectContext;	
	if (FString(SubObjectPath).Split(TEXT("."), &SubObjectContext, &SubObjectName))
	{
		if (UObject* SubObject = StaticFindObject(nullptr, this, *SubObjectContext))
		{
			return SubObject->ResolveSubobject(*SubObjectName, OutObject, bLoadIfExists);
		}
	}

	OutObject = nullptr;
	return false;
}

FPrimaryAssetId UWorld::GetPrimaryAssetId() const
{
	UPackage* Package = GetOutermost();
	const bool bIsWorldPartitionRuntime = PersistentLevel ? PersistentLevel->IsWorldPartitionRuntimeCell() : false;

	// PIE and world partition runtime levels are temporary and do not represent a primary asset
	if (!Package->HasAnyPackageFlags(PKG_PlayInEditor) && !bIsWorldPartitionRuntime)
	{
		// Return Map:/path/to/map
		return FPrimaryAssetId(UAssetManager::MapType, Package->GetFName());
	}

	return FPrimaryAssetId();
}

static IInterface_PostProcessVolume* ResolvePPVEntry(const UWorld::FPostProcessVolumeEntry& Entry)
{
	if (Entry.IsType<TWeakInterfacePtr<IInterface_PostProcessVolume>>())
	{
		return Entry.Get<TWeakInterfacePtr<IInterface_PostProcessVolume>>().Get();
	}
	return Entry.Get<IInterface_PostProcessVolume*>();
}

static bool PostProcessVolumeLess(const FPostProcessVolumeProperties& PropertiesA, const FPostProcessVolumeProperties& PropertiesB, double SizeB)
{
	if (PropertiesA.Priority != PropertiesB.Priority)
	{
		return PropertiesA.Priority < PropertiesB.Priority;
	}
	if (PropertiesA.bIsUnbound != PropertiesB.bIsUnbound)
	{
		// Unbounded volumes sort first.
		return PropertiesA.bIsUnbound;
	}
	if (PropertiesA.Size != SizeB)
	{
		// Larger sizes sort first.
		return PropertiesA.Size > SizeB;
	}

	// Finally, sort by guid.
	return PropertiesA.VolumeGuid < PropertiesB.VolumeGuid;
}

void UWorld::FPostProcessVolumeIterator::FIterator::AdvanceToValid()
{
	Current = nullptr;
	while (Index < Entries.Num())
	{
		const FPostProcessVolumeEntry& Entry = Entries[Index];
		Current = ResolvePPVEntry(Entry);

		if (Current)
		{
			break;
		}
		else
		{
			++Index;
		}
	}
}

void UWorld::InsertPostProcessVolumeEntry(FPostProcessVolumeEntry InVolume)
{
	IInterface_PostProcessVolume* InVolumeRaw = ResolvePPVEntry(InVolume);

	int32 NumVolumes = PostProcessVolumeEntries.Num();
	FPostProcessVolumeProperties TargetProperties = InVolumeRaw->GetProperties();
	int32 InsertIndex = 0;
	// TODO: replace with binary search.
	while (InsertIndex < NumVolumes)
	{
		IInterface_PostProcessVolume* CurrentVolume = ResolvePPVEntry(PostProcessVolumeEntries[InsertIndex]);
		if (CurrentVolume == InVolumeRaw)
		{
			return;
		}

		if (!CurrentVolume) // stale entry, volume was destroyed
		{
			RemovePostProcessVolume(InsertIndex);
			--NumVolumes;
		}
		else
		{
			FPostProcessVolumeProperties CurrentProperties = CurrentVolume->GetProperties();

			if (PostProcessVolumeLess(TargetProperties, CurrentProperties, PostProcessVolumeCachedSizes[InsertIndex]))
			{
				break;
			}
			++InsertIndex;
		}
	}

	// Check the remainder of the array to see if InVolume is already present.  The caller may have changed TargetProperties.Size
	// from what was originally cached, making the "Size > CurrentCachedSize" comparison inconsistent, meaning the existing item will not
	// have been reached in the loop.
	for (int32 Index = InsertIndex + 1; Index < NumVolumes; )
	{
		IInterface_PostProcessVolume* CurrentVolume = ResolvePPVEntry(PostProcessVolumeEntries[Index]);
		if (CurrentVolume == InVolumeRaw)
		{
			return;
		}

		if (!CurrentVolume) // stale entry, volume was destroyed
		{
			RemovePostProcessVolume(Index);
			--NumVolumes;
		}
		else
		{
			++Index;
		}
	}

	PostProcessVolumeEntries.Insert(MoveTemp(InVolume), InsertIndex);
	PostProcessVolumeCachedSizes.Insert(TargetProperties.Size, InsertIndex);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	PostProcessVolumes.Insert(InVolumeRaw, InsertIndex);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

// Deprecated
void UWorld::InsertPostProcessVolume(IInterface_PostProcessVolume* InVolume)
{
	check(InVolume);

	TWeakInterfacePtr<IInterface_PostProcessVolume> WeakInterfacePtr { InVolume };
	bool bIsUObject = WeakInterfacePtr.Get() != nullptr;
	if (bIsUObject)
	{
		AddPostProcessVolume(WeakInterfacePtr);
	}
	else
	{
#if DEBUG_POST_PROCESS_VOLUME_ENABLE
		UE_LOGF(LogWorld, Warning, "Post process volume %ls does not derive from UObject. This path is deprecated.", *InVolume->GetDebugName());
#endif

		FPostProcessVolumeEntry Entry;
		Entry.Set<IInterface_PostProcessVolume*>(InVolume);
		InsertPostProcessVolumeEntry(MoveTemp(Entry));
	}
}

void UWorld::AddPostProcessVolume(TWeakInterfacePtr<IInterface_PostProcessVolume> InVolume)
{
	check(InVolume.Get());

	FPostProcessVolumeEntry Entry;
	Entry.Set<TWeakInterfacePtr<IInterface_PostProcessVolume>>(InVolume);
	InsertPostProcessVolumeEntry(MoveTemp(Entry));
}

bool UWorld::RemovePostProcessVolume(IInterface_PostProcessVolume* InVolume)
{
	for (int32 Index = 0, NumVolumes = PostProcessVolumeEntries.Num(); Index < NumVolumes; )
	{
		IInterface_PostProcessVolume* CurrentVolume = ResolvePPVEntry(PostProcessVolumeEntries[Index]);
		if (!CurrentVolume) // stale entry, volume was destroyed
		{
			RemovePostProcessVolume(Index);
			--NumVolumes;
		}
		else
		{
			if (CurrentVolume == InVolume)
			{
				RemovePostProcessVolume(Index);
				--NumVolumes;
				return true;
			}
			++Index;
		}
	}
	return false;
}

void UWorld::RemovePostProcessVolume(int32 Index)
{
	PostProcessVolumeEntries.RemoveAt(Index);
	PostProcessVolumeCachedSizes.RemoveAt(Index);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	PostProcessVolumes.RemoveAt(Index);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UWorld::InitializeSubsystems()
{
	check(!SubsystemCollection.IsInitialized());
	SubsystemCollection.Initialize(this);
}

void UWorld::PostInitializeSubsystems()
{
	check(bIsWorldInitialized);

	SubsystemCollection.ForEachSubsystem([](UWorldSubsystem* WorldSubsystem)
	{
		WorldSubsystem->PostInitialize();
		WorldSubsystem->EnsureHasCalledPostInitialize();
	});
}

#if DEBUG_POST_PROCESS_VOLUME_ENABLE
static FProperty* GPostProcessDebugProperty = nullptr;
static FBoolProperty* GPostProcessDebugOverrideProperty = nullptr;
static TAutoConsoleVariable<FString> CVarPostProcessDebugProperty(
	TEXT("r.PostProcessing.Debug.Property"),
	TEXT(""),
	TEXT("When set to the name of a property in PostProcessSettings, the source of the value for that property will be shown in ShowFlag.VisualizePostProcessStack"),
	FConsoleVariableDelegate::CreateLambda(
		[](IConsoleVariable* CVar)
		{
			FString NewPropertyName = CVar->GetString();

			if (NewPropertyName.IsEmpty())
			{
				GPostProcessDebugProperty = nullptr;
			}
			else
			{
				GPostProcessDebugProperty = FindFProperty<FProperty>(FPostProcessSettings::StaticStruct(), FName(NewPropertyName));
				GPostProcessDebugOverrideProperty = FindFProperty<FBoolProperty>(FPostProcessSettings::StaticStruct(), FName(TEXT("bOverride_") + NewPropertyName));
				if (GPostProcessDebugProperty)
				{
					FString FullName = GPostProcessDebugProperty->GetFullName();
					UE_LOGF(LogWorld, Display, "Visualizing %ls", *FullName);
				}
				else
				{
					TStringBuilder<1024> StringBuilder {};
					for (TFieldIterator<FProperty>It(FPostProcessSettings::StaticStruct(), EFieldIterationFlags::Default); It; ++It)
					{
						if (!It->GetFName().ToString().StartsWith(TEXT("bOverride_")))
						{
							StringBuilder.Append(It->GetFName().ToString());
							StringBuilder.Append(TEXT("\n"));
						}
					}
					FString ValidPropertyNames = StringBuilder.ToString();

					UE_LOGF(LogWorld, Error, 
						   "Cannot find %ls\n" "Valid property names are:\n%ls",
						   *NewPropertyName,
						   *ValidPropertyNames);
				}
			}
		}
	)
);

static void GatherPostProcessVolumeDebugInfo(IInterface_PostProcessVolume* Volume, float LocalWeight, FPostProcessSettingsDebugInfo& Output)
{
	Output.Name = Volume->GetDebugName();

	FPostProcessVolumeProperties VProperties = Volume->GetProperties();
	Output.bIsEnabled = VProperties.bIsEnabled;
	Output.bIsUnbound = VProperties.bIsUnbound;
	Output.Priority = VProperties.Priority;
	Output.CurrentBlendWeight = LocalWeight;

	if (GPostProcessDebugProperty != nullptr)
	{
		bool bOverrideEnabled = true; // if we couldn't find the bOverride_ property, assume it's true
		if (GPostProcessDebugOverrideProperty)
		{
			const bool* bOverridePtr = GPostProcessDebugOverrideProperty->ContainerPtrToValuePtr<bool>(VProperties.Settings);
			if (bOverridePtr)
			{
				bOverrideEnabled = *bOverridePtr;
			}
		}

		if (bOverrideEnabled)
		{
			const void* PropData = GPostProcessDebugProperty->ContainerPtrToValuePtr<void>(VProperties.Settings);
			FString PropValueAsString;
			if (GPostProcessDebugProperty->ExportText_Direct(PropValueAsString, PropData, PropData, (UObject*)VProperties.Settings, PPF_DebugDump))
			{
				Output.DebugPropertyValue = PropValueAsString;
			}
			else
			{
				Output.DebugPropertyValue = TEXT("<failed to print value>");
			}
		}
		else
		{
			Output.DebugPropertyValue = TEXT("N/A");
		}
	}
}
#endif

static void DoPostProcessVolume(IInterface_PostProcessVolume* Volume, FVector ViewLocation, FSceneView* SceneView)
{
	const FPostProcessVolumeProperties VolumeProperties = Volume->GetProperties();
	if (!VolumeProperties.bIsEnabled)
	{
		return;
	}

	float DistanceToPoint = 0.0f;
	float LocalWeight = FMath::Clamp(VolumeProperties.BlendWeight, 0.0f, 1.0f);

	ensureMsgf((LocalWeight >= 0 && LocalWeight <= 1.0f), TEXT("Invalid post process blend weight retrieved from volume (%f)"), LocalWeight);

	if (!VolumeProperties.bIsUnbound)
	{
		float SquaredBlendRadius = VolumeProperties.BlendRadius * VolumeProperties.BlendRadius;
		Volume->EncompassesPoint(ViewLocation, 0.0f, &DistanceToPoint);

		if (DistanceToPoint >= 0)
		{
			if (DistanceToPoint > VolumeProperties.BlendRadius)
			{
				// outside
				LocalWeight = 0.0f;
			}
			else
			{
				// to avoid div by 0
				if (VolumeProperties.BlendRadius >= 1.0f)
				{
					LocalWeight *= 1.0f - DistanceToPoint / VolumeProperties.BlendRadius;

					if(!(LocalWeight >= 0 && LocalWeight <= 1.0f))
					{
						// Mitigate crash here by disabling this volume and generating info regarding the calculation that went wrong.
						ensureMsgf(false, TEXT("Invalid LocalWeight after post process volume weight calculation (Local: %f, DtP: %f, Radius: %f, SettingsWeight: %f)"), LocalWeight, DistanceToPoint, VolumeProperties.BlendRadius, VolumeProperties.BlendWeight);
						LocalWeight = 0.0f;
					}
				}
			}
		}
		else
		{
			LocalWeight = 0;
		}
	}

	if (LocalWeight > 0)
	{
		// Suppress blendables from post process volumes for runtime reflection captures.  Runtime reflection captures don't run post processing,
		// and only support a singleton post process material to allow artists to apply things like color or directional tint adjustments.  Runtime
		// reflection captures don't have an FSceneViewState to store MIDs, which would be required to handle generalized post process material
		// blending, but generally we also don't want other post process materials by design.
		const bool bAllowBlendables = !SceneView->IsRuntimeReflectionCapture();

		SceneView->OverridePostProcessSettings(*VolumeProperties.Settings, LocalWeight, bAllowBlendables);
	}

#if DEBUG_POST_PROCESS_VOLUME_ENABLE
	if (SceneView->Family && SceneView->Family->EngineShowFlags.VisualizePostProcessStack)
	{
		FPostProcessSettingsDebugInfo& PPDebug = SceneView->FinalPostProcessDebugInfo.AddDefaulted_GetRef();
		GatherPostProcessVolumeDebugInfo(Volume, LocalWeight, PPDebug);
	}
#endif
}

void UWorld::AddPostProcessingSettings(FVector ViewLocation, FSceneView* SceneView)
{
	SCOPED_NAMED_EVENT(UWorld_AddPostProcessingSettings, FColor::Red);

	OnBeginPostProcessSettings.Broadcast(ViewLocation, SceneView);

#if DEBUG_POST_PROCESS_VOLUME_ENABLE
	SceneView->PostProcessDebugPropertyName = GPostProcessDebugProperty ? GPostProcessDebugProperty->GetFName() : NAME_None;
	SceneView->FinalPostProcessDebugInfo.Reset();
#endif

	for (IInterface_PostProcessVolume& PPVolume : GetPostProcessVolumeIterator())
	{
		DoPostProcessVolume(&PPVolume, ViewLocation, SceneView);
	}
}

void UWorld::SetAudioDevice(const FAudioDeviceHandle& InHandle)
{
	check(IsInGameThread());

	if (InHandle.GetDeviceID() == AudioDeviceHandle.GetDeviceID())
	{
		return;
	}

	if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
	{
		// Register new world with incoming device first to avoid premature reporting due to no handles being valid...
		if (InHandle.IsValid())
		{
			check(InHandle.GetWorld() == this);
			DeviceManager->RegisterWorld(this, InHandle.GetDeviceID());
		}

		const Audio::FDeviceId OldDeviceId = AudioDeviceHandle.GetDeviceID();
		const bool bUnregister = AudioDeviceHandle.IsValid();

		AudioDeviceHandle = InHandle;

		if (bUnregister)
		{
			DeviceManager->UnregisterWorld(this, OldDeviceId);
		}
	}
	else
	{
		AudioDeviceHandle.Reset();
	}

	if (AudioDeviceHandle.IsValid() && !AudioDeviceDestroyedHandle.IsValid())
	{
		AudioDeviceDestroyedHandle = FAudioDeviceManagerDelegates::OnAudioDeviceDestroyed.AddLambda([this](const Audio::FDeviceId InDeviceId)
		{
			if (InDeviceId == AudioDeviceHandle.GetDeviceID())
			{
				FAudioDeviceHandle EmptyHandle;
				SetAudioDevice(EmptyHandle);
			}
		});
	}
	else if (!AudioDeviceHandle.IsValid() && AudioDeviceDestroyedHandle.IsValid())
	{
		FAudioDeviceManagerDelegates::OnAudioDeviceDestroyed.Remove(AudioDeviceDestroyedHandle);
		AudioDeviceDestroyedHandle.Reset();
	}
}

FAudioDeviceHandle UWorld::GetAudioDevice() const
{
	if (AudioDeviceHandle)
	{
		return AudioDeviceHandle;
	}
	else if (GEngine)
	{
		return GEngine->GetMainAudioDevice();
	}
	else
	{
		return FAudioDeviceHandle();
	}
}

FAudioDevice* UWorld::GetAudioDeviceRaw() const
{
	if (AudioDeviceHandle)
	{
		return AudioDeviceHandle.GetAudioDevice();
	}
	else if (GEngine)
	{
		return GEngine->GetMainAudioDeviceRaw();
	}
	else
	{
		return nullptr;
	}
}

TMulticastDelegateRegistration<void(float)>& UWorld::OnTickDispatch()
{
	return TickDispatchEvent;
}

TMulticastDelegateRegistration<void()>& UWorld::OnPostTickDispatch()
{
	return PostTickDispatchEvent;
}

TMulticastDelegateRegistration<void(float)>& UWorld::OnPreTickFlush()
{
	return PreTickFlushEvent;
}

TMulticastDelegateRegistration<void(float)>& UWorld::OnTickFlush()
{
	return TickFlushEvent;
}

TMulticastDelegateRegistration<void()>& UWorld::OnPostTickFlush()
{
	return PostTickFlushEvent;
}

void UWorld::BroadcastTickDispatch(float DeltaTime)	
{
	TickDispatchEvent.Broadcast(DeltaTime);
}

void UWorld::BroadcastPostTickDispatch()
{
	PostTickDispatchEvent.Broadcast();
}

void UWorld::BroadcastPreTickFlush(float DeltaTime)
{
	PreTickFlushEvent.Broadcast(DeltaTime);
}

void UWorld::BroadcastTickFlush(float DeltaTime)
{
	TickFlushEvent.Broadcast(DeltaTime);
}

void UWorld::BroadcastPostTickFlush(float DeltaTime)
{
	PostTickFlushEvent.Broadcast();
}

/**
* Dump visible actors in current world.
*/
static void DumpVisibleActors(UWorld* InWorld)
{
	UE_LOGF(LogWorld, Log, "------ START DUMP VISIBLE ACTORS ------");
	for (FActorIterator ActorIterator(InWorld); ActorIterator; ++ActorIterator)
	{
		AActor* Actor = *ActorIterator;
		if (Actor && Actor->WasRecentlyRendered(0.05f))
		{
			UE_LOGF(LogWorld, Log, "Visible Actor : %ls", *Actor->GetFullName());
		}
	}
	UE_LOGF(LogWorld, Log, "------ END DUMP VISIBLE ACTORS ------");
}

static FAutoConsoleCommandWithWorld DumpVisibleActorsCmd(
	TEXT("DumpVisibleActors"),
	TEXT("Dump visible actors in current world."),
	FConsoleCommandWithWorldDelegate::CreateStatic(DumpVisibleActors)
	);

static void DumpLevelCollections(UWorld* InWorld)
{
	if (!InWorld)
	{
		return;
	}

	UE_LOGF(LogWorld, Log, "--- Dumping LevelCollections ---");

	for(const FLevelCollection& LC : InWorld->GetLevelCollections())
	{
		UE_LOGF(LogWorld, Log, "%d: %d levels.", static_cast<int32>(LC.GetType()), LC.GetLevels().Num());
		UE_LOGF(LogWorld, Log, "  PersistentLevel: %ls", *GetFullNameSafe(LC.GetPersistentLevel()));
		UE_LOGF(LogWorld, Log, "  GameState: %ls", *GetFullNameSafe(LC.GetGameState()));
		UE_LOGF(LogWorld, Log, "  Levels:");
		for (const ULevel* Level : LC.GetLevels())
		{
			UE_LOGF(LogWorld, Log, "    %ls", *GetFullNameSafe(Level));
		}
	}
}

static FAutoConsoleCommandWithWorld DumpLevelCollectionsCmd(
	TEXT("DumpLevelCollections"),
	TEXT("Dump level collections in the current world."),
	FConsoleCommandWithWorldDelegate::CreateStatic(DumpLevelCollections)
	);

#if WITH_EDITOR
FAsyncPreRegisterDDCRequest::~FAsyncPreRegisterDDCRequest()
{
	// Discard any results
	if (Handle != 0)
	{
		WaitAsynchronousCompletion();
		TArray<uint8> Junk;
		GetAsynchronousResults(Junk);
	}
}

bool FAsyncPreRegisterDDCRequest::PollAsynchronousCompletion()
{
	if (Handle != 0)
	{
		return GetDerivedDataCacheRef().PollAsynchronousCompletion(Handle);
	}
	return true;
}

void FAsyncPreRegisterDDCRequest::WaitAsynchronousCompletion()
{
	if (Handle != 0)
	{
		GetDerivedDataCacheRef().WaitAsynchronousCompletion(Handle);
	}
}

bool FAsyncPreRegisterDDCRequest::GetAsynchronousResults(TArray<uint8>& OutData)
{
	check(Handle != 0);
	bool bResult = GetDerivedDataCacheRef().GetAsynchronousResults(Handle, OutData);
	// invalidate request after results received
	Handle = 0;
	DDCKey = TEXT("");
	return bResult;
}
#endif

FString ENGINE_API ToString(ENetMode NetMode)
{
	switch (NetMode)
	{
	case NM_Standalone: return TEXT("Standalone");
	case NM_DedicatedServer:  return TEXT("Dedicated Server");
	case NM_ListenServer: return TEXT("Listen Server");
	case NM_Client: return TEXT("Client");
	default: return TEXT("Invalid");
	}
}

#undef LOCTEXT_NAMESPACE 
