// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#ifndef RHI_RESOURCE_PROVENANCE_ENABLED
#define RHI_RESOURCE_PROVENANCE_ENABLED 0
#endif

#if RHI_RESOURCE_PROVENANCE_ENABLED

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace UE::RHI::ResourceProvenance
{
	enum class EOperation : uint8
	{
		Create,
		AddRef,
		Release,
		FinalRelease,
		MarkForDelete,
		MarkForDeleteAlreadySet,
		DeleteCheck,
		DeleteBegin,
		DeleteCancelled,
		DestructorBegin,
		PhysicalFree,
		CommandEnqueue,
		CommandExecute,
		UpdateRequest,
		UpdateExecute,
		ExternalUse,
		OwnerAssociation,
		ReleaseReason,
		BindingStore,
		InvalidUse,
		AccessOwner,
		ReleaseOwner,
		CommandOwner,
		BindingCreate,
		BindingCopy,
		BindingMove,
		BindingSubmit,
		BindingRelease,
		BindingOwner,
		BindingInvalidate,
		CausalRequest,
		CausalExecute,
		CausalLink,
		CausalResource,
		GCPreSnapshot,
		GCPostSurvived,
		GCPostCollected,
		GCCoverageOmitted,
		SceneMapInsert,
		SceneMapRemove,
		SceneMapReplaceOld,
		SceneMapReplaceNew,
		ReleaseCause,
		ReleaseBindingSnapshot,
		ReleaseBindingCoverage,
		ContributorRegistered,
		ContributorActor,
		ContributorComponent,
		ContributorWorldPartition,
		ContributorDataLayer,
		ContributorRetired,
		BindingContributor,
		ContributorCoverageOmitted,
		DataLayerStateRequest,
		DataLayerStateRejected,
		DataLayerTargetStateChanged,
		DataLayerEffectiveStateChanged,
		DataLayerStateNoOp,
		DataLayerTransitionCoverage,
		CellStateRequest,
		CellStateAccepted,
		CellStateBlocked,
		CellStateProgress,
		CellStateCompleted,
		CellTransitionSuperseded,
		CellTransitionCoverage
	};

	enum class EReleaseCause : uint8
	{
		Unknown,
		MPCAssetBeginDestroy,
		MPCAssetFinishDestroy,
		MPCInstanceFinishDestroy,
		MPCGameThreadDestroy,
		MPCUniformBufferRecreate,
		MPCUniformBufferInvalidReplacement,
		WorldReplacedMPCInstance,
		WorldPostGCInvalidCollection,
		SceneMapRemove,
		SceneMapReplace
	};

	FORCEINLINE uint64 CaptureCallerAddress()
	{
#if defined(__clang__) || defined(__GNUC__)
		return reinterpret_cast<uint64>(__builtin_return_address(0));
#elif defined(_MSC_VER)
		return reinterpret_cast<uint64>(_ReturnAddress());
#else
		return 0;
#endif
	}

	RHI_API uint64 RegisterResource(
		const void* ResourceAddress,
		const void* FlagsAddress,
		uint8 ResourceType,
		uint64 CallerAddress);

	RHI_API void SetDebugName(
		uint64 ResourceId,
		const void* ResourceAddress,
		const void* FlagsAddress,
		uint8 ResourceType,
		const TCHAR* DebugName);
	RHI_API void SetOwnerName(
		uint64 ResourceId,
		const void* ResourceAddress,
		const void* FlagsAddress,
		uint8 ResourceType,
		const TCHAR* OwnerName);
	RHI_API void SetOwnerPath(
		uint64 ResourceId,
		const void* ResourceAddress,
		const void* FlagsAddress,
		uint8 ResourceType,
		const TCHAR* OwnerPath);

	RHI_API void RecordMarker(
		EOperation Operation,
		uint64 ResourceId,
		const void* ResourceAddress,
		const void* FlagsAddress,
		uint8 ResourceType,
		const TCHAR* Text);

	RHI_API void Record(
		EOperation Operation,
		const void* ResourceAddress,
		const void* FlagsAddress,
		uint64 ResourceId,
		uint8 ResourceType,
		uint32 PackedValue,
		uint64 CallerAddress,
		uint64 CorrelationId = 0);

	RHI_API uint64 BeginCommandUse(EOperation Operation, const void* ResourceAddress, uint64 CallerAddress);
	RHI_API void RecordCommandUse(EOperation Operation, const void* ResourceAddress, uint64 CorrelationId, uint64 CallerAddress);
	RHI_API void RecordBindingStore(const void* ResourceAddress, uint64 CallerAddress);
	RHI_API void RecordReleaseCause(
		EReleaseCause Cause,
		uint64 ResourceId,
		const void* ResourceAddress,
		const void* FlagsAddress,
		uint8 ResourceType,
		uint64 CallerAddress);

	/**
	 * Records the lifetime of a cached binding using its saved resource generation.
	 * This never dereferences ResourceAddress and remains valid after physical free.
	 */
	RHI_API void RecordBindingLifecycle(
		EOperation Operation,
		uint64 ResourceId,
		const void* ResourceAddress,
		uint64 BindingId,
		uint64 OwnerKey,
		uint64 ContributorId,
		uint64 CallerAddress);

	/** Returns whether bounded actor/component/World Partition contributor capture is active. */
	RHI_API bool IsContributorCaptureEnabled();

	/** Allocates one bounded contributor id, or zero when capture is disabled/exhausted. */
	RHI_API uint64 AllocateContributorId();

	/** Maximum number of Data Layer descriptor rows retained per contributor. */
	RHI_API uint32 GetMaxContributorDataLayers();

	/**
	 * Records immutable contributor metadata. Text is copied synchronously into the
	 * journal queue; SubjectAddress is treated as an opaque identity token only.
	 */
	RHI_API void RecordContributorMetadata(
		EOperation Operation,
		uint64 ContributorId,
		const void* SubjectAddress,
		uint32 PackedValue,
		const TCHAR* Text,
		uint64 CallerAddress);

	/** Allocates one bounded World Partition Data Layer transition id. */
	RHI_API uint64 AllocateDataLayerTransitionId();

	/**
	 * Records a request/outcome/effective-state row for one Data Layer transition.
	 * SubjectAddress is an opaque UDataLayerInstance identity and is never dereferenced.
	 */
	RHI_API void RecordDataLayerTransition(
		EOperation Operation,
		uint64 TransitionId,
		const void* SubjectAddress,
		uint32 PackedValue,
		const TCHAR* Text,
		uint64 CallerAddress);

	/** Allocates one bounded World Partition runtime-cell transition id. */
	RHI_API uint64 AllocateCellTransitionId();

	/**
	 * Records one request, acceptance, block, progress, completion, or supersession row
	 * for a runtime-cell transition. SubjectAddress is an opaque runtime-cell identity.
	 */
	RHI_API void RecordCellTransition(
		EOperation Operation,
		uint64 TransitionId,
		const void* SubjectAddress,
		uint32 PackedValue,
		const TCHAR* Text,
		uint64 CallerAddress);

	/** Allocates an always-on correlation id for diagnostic timelines. */
	RHI_API uint64 AllocateTimelineId();

	/** Allocates and records bounded cross-thread causal tokens when command-use tracing is enabled. */
	RHI_API uint64 AllocateCausalId();
	RHI_API void RecordCausalPhase(
		EOperation Operation,
		uint64 CausalId,
		const void* SubjectAddress,
		uint32 Detail,
		uint64 CallerAddress);
	RHI_API uint64 SetCurrentCausalParent(uint64 CausalId);
	RHI_API void RecordResourceCausalLink(
		const void* ResourceAddress,
		uint64 CausalId,
		uint64 CallerAddress);

	/**
	 * Registers one bounded access owner. Formatting is only needed when
	 * bOutNeedsOwnerText is true. Returns the resource generation id, or zero when the
	 * resource is not retained or bounded owner coverage is exhausted.
	 */
	RHI_API uint64 ClaimAccessOwner(
		const void* ResourceAddress,
		uint64 OwnerKey,
		bool& bOutNeedsOwnerText);
	RHI_API void RecordAccessOwner(
		uint64 ResourceId,
		const void* ResourceAddress,
		uint64 OwnerKey,
		const TCHAR* OwnerText,
		uint64 CallerAddress);

	/**
	 * Stages the owner for the next command enqueue of this resource on the current thread.
	 * Cached mesh bindings carry OwnerKey beside the resource pointer through copies/moves.
	 */
	RHI_API void StageAccessOwner(
		const void* ResourceAddress,
		uint64 OwnerKey);

	RHI_API void ReportInvalidAtomic(
		const TCHAR* Reason,
		const void* ResourceAddress,
		const void* FlagsAddress,
		uint64 ObservedResourceId,
		uint8 ObservedResourceType,
		uint32 OldPacked,
		uint64 CallerAddress);
}

#endif // RHI_RESOURCE_PROVENANCE_ENABLED
