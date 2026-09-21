// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if UE_BUILD_DEVELOPMENT

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
		ExternalUse
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

	RHI_API void SetDebugName(uint64 ResourceId, const TCHAR* DebugName);
	RHI_API void SetOwnerName(uint64 ResourceId, const TCHAR* OwnerName);
	RHI_API void SetOwnerPath(uint64 ResourceId, const TCHAR* OwnerPath);

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

	RHI_API void ReportInvalidAtomic(
		const TCHAR* Reason,
		const void* ResourceAddress,
		const void* FlagsAddress,
		uint64 ObservedResourceId,
		uint8 ObservedResourceType,
		uint32 OldPacked,
		uint64 CallerAddress);
}

#endif // UE_BUILD_DEVELOPMENT
