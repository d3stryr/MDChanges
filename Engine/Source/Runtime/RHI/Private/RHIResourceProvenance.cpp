// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHIResourceProvenance.h"

#if UE_BUILD_DEVELOPMENT

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTLS.h"
#include "HAL/PlatformTime.h"
#include "RHI.h"

#include <atomic>

namespace UE::RHI::ResourceProvenance
{
namespace
{
	static constexpr uint32 ThreadBufferCount = 128;
	static constexpr uint32 EventsPerThread = 2048;
	static constexpr uint32 IdentityCapacity = 32768;
	static constexpr uint32 NameCapacity = 64;
	static constexpr uint32 PathCapacity = 96;
	static constexpr uint32 LifecycleEventsPerIdentity = 8;
	static constexpr uint32 MaxDumpEvents = 256;

	static_assert((EventsPerThread & (EventsPerThread - 1)) == 0, "EventsPerThread must be a power of two.");

	struct FEvent
	{
		mutable std::atomic_flag Writer = ATOMIC_FLAG_INIT;
		uint64 PublishedSequence = 0;
		uint64 Cycles = 0;
		uint64 ResourceAddress = 0;
		uint64 FlagsAddress = 0;
		uint64 ResourceId = 0;
		uint64 CallerAddress = 0;
		uint64 CorrelationId = 0;
		uint32 PackedValue = 0;
		uint32 ThreadId = 0;
		uint32 OperationAndType = 0;
	};

	struct alignas(PLATFORM_CACHE_LINE_SIZE) FThreadBuffer
	{
		std::atomic<uint32> Claimed { 0 };
		std::atomic<uint64> TotalWrites { 0 };
		uint32 ThreadId = 0;
		uint32 WriteIndex = 0;
		FEvent Events[EventsPerThread];
	};

	template <uint32 Capacity>
	struct TAtomicString
	{
		std::atomic<TCHAR> Characters[Capacity] {};

		void Set(const TCHAR* Source)
		{
			uint32 Index = 0;
			if (Source)
			{
				for (; Index + 1 < Capacity && Source[Index] != 0; ++Index)
				{
					Characters[Index].store(Source[Index], std::memory_order_relaxed);
				}
			}

			Characters[Index].store(0, std::memory_order_release);
			for (++Index; Index < Capacity; ++Index)
			{
				Characters[Index].store(0, std::memory_order_relaxed);
			}
		}

		void Get(TCHAR (&Destination)[Capacity]) const
		{
			for (uint32 Index = 0; Index < Capacity; ++Index)
			{
				Destination[Index] = Characters[Index].load(std::memory_order_acquire);
				if (Destination[Index] == 0)
				{
					return;
				}
			}
			Destination[Capacity - 1] = 0;
		}
	};

	struct FLifecycleEvent
	{
		uint64 Sequence = 0;
		uint64 Cycles = 0;
		uint64 CallerAddress = 0;
		uint32 PackedValue = 0;
		uint32 ThreadId = 0;
		uint32 Operation = 0;
	};

	struct FIdentity
	{
		mutable std::atomic_flag Writer = ATOMIC_FLAG_INIT;
		std::atomic<uint64> ResourceId { 0 };
		std::atomic<uint64> ResourceAddress { 0 };
		std::atomic<uint64> FlagsAddress { 0 };
		std::atomic<uint64> CreateCaller { 0 };
		std::atomic<uint64> LastStateCaller { 0 };
		std::atomic<uint32> ResourceType { 0 };
		std::atomic<uint32> LastOperation { 0 };
		uint32 LifecycleWriteIndex = 0;
		FLifecycleEvent Lifecycle[LifecycleEventsPerIdentity];
		TAtomicString<NameCapacity> DebugName;
		TAtomicString<NameCapacity> OwnerName;
		TAtomicString<PathCapacity> OwnerPath;
	};

	struct FDumpEvent
	{
		uint64 Sequence = 0;
		uint64 Cycles = 0;
		uint64 ResourceAddress = 0;
		uint64 FlagsAddress = 0;
		uint64 ResourceId = 0;
		uint64 CallerAddress = 0;
		uint64 CorrelationId = 0;
		uint32 PackedValue = 0;
		uint32 ThreadId = 0;
		uint32 OperationAndType = 0;
	};

	FThreadBuffer GThreadBuffers[ThreadBufferCount];
	FIdentity GIdentities[IdentityCapacity];

	std::atomic<uint32> GNextThreadBuffer { 0 };
	std::atomic<uint64> GNextResourceId { 1 };
	std::atomic<uint64> GThreadBufferOverflows { 0 };
	std::atomic<uint64> GIdentityEvictions { 0 };

	thread_local int32 GTlsThreadBufferIndex = -2;
	thread_local uint32 GTlsCommandSequence = 0;

	TAutoConsoleVariable<int32> CVarRHIResourceProvenanceCommandUses(
		TEXT("r.RHI.ResourceProvenance.CommandUses"),
		0,
		TEXT("Records selected RHI command enqueue/execution and update operations. ")
		TEXT("This is bounded but can be expensive on binding-heavy frames."),
		ECVF_Default);

	const TCHAR* GetOperationName(EOperation Operation)
	{
		switch (Operation)
		{
		case EOperation::Create:                    return TEXT("Create");
		case EOperation::AddRef:                    return TEXT("AddRef");
		case EOperation::Release:                   return TEXT("Release");
		case EOperation::FinalRelease:              return TEXT("FinalRelease");
		case EOperation::MarkForDelete:             return TEXT("MarkForDelete");
		case EOperation::MarkForDeleteAlreadySet:   return TEXT("MarkForDeleteAlreadySet");
		case EOperation::DeleteCheck:               return TEXT("DeleteCheck");
		case EOperation::DeleteBegin:               return TEXT("DeleteBegin");
		case EOperation::DeleteCancelled:           return TEXT("DeleteCancelled");
		case EOperation::DestructorBegin:           return TEXT("DestructorBegin");
		case EOperation::PhysicalFree:              return TEXT("PhysicalFree");
		case EOperation::CommandEnqueue:            return TEXT("CommandEnqueue");
		case EOperation::CommandExecute:            return TEXT("CommandExecute");
		case EOperation::UpdateRequest:             return TEXT("UpdateRequest");
		case EOperation::UpdateExecute:             return TEXT("UpdateExecute");
		case EOperation::ExternalUse:               return TEXT("ExternalUse");
		default:                                    return TEXT("Unknown");
		}
	}

	FIdentity* FindIdentity(uint64 ResourceId)
	{
		if (ResourceId == 0)
		{
			return nullptr;
		}

		FIdentity& Identity = GIdentities[ResourceId % IdentityCapacity];
		return Identity.ResourceId.load(std::memory_order_acquire) == ResourceId ? &Identity : nullptr;
	}

	void LockIdentity(FIdentity& Identity)
	{
		while (Identity.Writer.test_and_set(std::memory_order_acquire))
		{
			FPlatformProcess::YieldThread();
		}
	}

	void UnlockIdentity(FIdentity& Identity)
	{
		Identity.Writer.clear(std::memory_order_release);
	}

	FThreadBuffer* GetThreadBuffer()
	{
		if (GTlsThreadBufferIndex == -2)
		{
			const uint32 Index = GNextThreadBuffer.fetch_add(1, std::memory_order_relaxed);
			if (Index >= ThreadBufferCount)
			{
				GTlsThreadBufferIndex = -1;
				GThreadBufferOverflows.fetch_add(1, std::memory_order_relaxed);
				return nullptr;
			}

			GTlsThreadBufferIndex = static_cast<int32>(Index);
			FThreadBuffer& Buffer = GThreadBuffers[Index];
			Buffer.ThreadId = FPlatformTLS::GetCurrentThreadId();
			Buffer.Claimed.store(1, std::memory_order_release);
		}

		return GTlsThreadBufferIndex >= 0 ? &GThreadBuffers[GTlsThreadBufferIndex] : nullptr;
	}

	bool ReadEvent(const FEvent& Source, FDumpEvent& Destination)
	{
		// Failure-time readers never wait on an event currently being published.
		if (Source.Writer.test_and_set(std::memory_order_acquire))
		{
			return false;
		}

		if (Source.PublishedSequence == 0)
		{
			Source.Writer.clear(std::memory_order_release);
			return false;
		}

		Destination.Sequence = Source.PublishedSequence;
		Destination.Cycles = Source.Cycles;
		Destination.ResourceAddress = Source.ResourceAddress;
		Destination.FlagsAddress = Source.FlagsAddress;
		Destination.ResourceId = Source.ResourceId;
		Destination.CallerAddress = Source.CallerAddress;
		Destination.CorrelationId = Source.CorrelationId;
		Destination.PackedValue = Source.PackedValue;
		Destination.ThreadId = Source.ThreadId;
		Destination.OperationAndType = Source.OperationAndType;

		Source.Writer.clear(std::memory_order_release);
		return true;
	}

	void InsertNewest(FDumpEvent (&Events)[MaxDumpEvents], uint32& Count, uint64& TotalMatches, const FDumpEvent& Candidate)
	{
		++TotalMatches;

		if (Count < MaxDumpEvents)
		{
			Events[Count++] = Candidate;
			return;
		}

		uint32 OldestIndex = 0;
		for (uint32 Index = 1; Index < Count; ++Index)
		{
			if (Events[Index].Cycles < Events[OldestIndex].Cycles ||
				(Events[Index].Cycles == Events[OldestIndex].Cycles && Events[Index].Sequence < Events[OldestIndex].Sequence))
			{
				OldestIndex = Index;
			}
		}

		if (Candidate.Cycles > Events[OldestIndex].Cycles ||
			(Candidate.Cycles == Events[OldestIndex].Cycles && Candidate.Sequence > Events[OldestIndex].Sequence))
		{
			Events[OldestIndex] = Candidate;
		}
	}

	void SortBySequence(FDumpEvent (&Events)[MaxDumpEvents], uint32 Count)
	{
		for (uint32 Index = 1; Index < Count; ++Index)
		{
			FDumpEvent Value = Events[Index];
			uint32 InsertAt = Index;
			while (InsertAt > 0 &&
				(Events[InsertAt - 1].Cycles > Value.Cycles ||
				(Events[InsertAt - 1].Cycles == Value.Cycles && Events[InsertAt - 1].Sequence > Value.Sequence)))
			{
				Events[InsertAt] = Events[InsertAt - 1];
				--InsertAt;
			}
			Events[InsertAt] = Value;
		}
	}

	void DumpIdentityMatches(const void* ResourceAddress, const void* FlagsAddress, uint64 ObservedResourceId)
	{
		uint32 AddressGenerationCount = 0;

		for (FIdentity& Identity : GIdentities)
		{
			const uint64 PreliminaryId = Identity.ResourceId.load(std::memory_order_acquire);
			const uint64 PreliminaryResource = Identity.ResourceAddress.load(std::memory_order_relaxed);
			const uint64 PreliminaryFlags = Identity.FlagsAddress.load(std::memory_order_relaxed);
			if ((ObservedResourceId == 0 || PreliminaryId != ObservedResourceId) &&
				PreliminaryResource != reinterpret_cast<uint64>(ResourceAddress) &&
				PreliminaryFlags != reinterpret_cast<uint64>(FlagsAddress))
			{
				continue;
			}

			// Failure reporting must not wait on a writer that may have stopped.
			if (Identity.Writer.test_and_set(std::memory_order_acquire))
			{
				UE_LOG(LogRHI, Error, TEXT("RHI provenance identity slot busy during failure dump; one matching identity may be incomplete."));
				continue;
			}

			const uint64 IdentityId = Identity.ResourceId.load(std::memory_order_relaxed);
			const uint64 IdentityResource = Identity.ResourceAddress.load(std::memory_order_relaxed);
			const uint64 IdentityFlags = Identity.FlagsAddress.load(std::memory_order_relaxed);
			const bool bIdMatch = ObservedResourceId != 0 && IdentityId == ObservedResourceId;
			const bool bAddressMatch = IdentityResource == reinterpret_cast<uint64>(ResourceAddress);
			const bool bFlagsMatch = IdentityFlags == reinterpret_cast<uint64>(FlagsAddress);
			if (IdentityId == 0 || (!bIdMatch && !bAddressMatch && !bFlagsMatch))
			{
				Identity.Writer.clear(std::memory_order_release);
				continue;
			}

			if (bAddressMatch)
			{
				++AddressGenerationCount;
			}

			TCHAR DebugName[NameCapacity] {};
			TCHAR OwnerName[NameCapacity] {};
			TCHAR OwnerPath[PathCapacity] {};
			Identity.DebugName.Get(DebugName);
			Identity.OwnerName.Get(OwnerName);
			Identity.OwnerPath.Get(OwnerPath);

			const uint32 ResourceType = Identity.ResourceType.load(std::memory_order_relaxed);
			const uint64 CreateCaller = Identity.CreateCaller.load(std::memory_order_relaxed);
			const EOperation LastOperation = static_cast<EOperation>(Identity.LastOperation.load(std::memory_order_relaxed));
			const uint64 LastStateCaller = Identity.LastStateCaller.load(std::memory_order_relaxed);

			FLifecycleEvent LifecycleEvents[LifecycleEventsPerIdentity] {};
			uint32 LifecycleEventCount = 0;
			const uint32 LifecycleWriteIndex = Identity.LifecycleWriteIndex;
			const uint32 FirstLifecycleIndex = LifecycleWriteIndex > LifecycleEventsPerIdentity
				? LifecycleWriteIndex - LifecycleEventsPerIdentity
				: 0;
			for (uint32 LifecycleIndex = FirstLifecycleIndex; LifecycleIndex < LifecycleWriteIndex; ++LifecycleIndex)
			{
				const FLifecycleEvent& Event = Identity.Lifecycle[LifecycleIndex % LifecycleEventsPerIdentity];
				if (Event.Sequence == static_cast<uint64>(LifecycleIndex) + 1)
				{
					LifecycleEvents[LifecycleEventCount++] = Event;
				}
			}

			Identity.Writer.clear(std::memory_order_release);

			UE_LOG(LogRHI, Error,
				TEXT("RHI provenance identity: id=%llu resource=%p flags=%p type=%u create_pc=0x%llx last_state=%s last_state_pc=0x%llx lifecycle_total=%u lifecycle_retained=%u lifecycle_omitted=%u debug='%s' owner='%s' owner_path='%s'"),
				IdentityId,
				reinterpret_cast<const void*>(IdentityResource),
				reinterpret_cast<const void*>(IdentityFlags),
				ResourceType,
				CreateCaller,
				GetOperationName(LastOperation),
				LastStateCaller,
				LifecycleWriteIndex,
				LifecycleEventCount,
				LifecycleWriteIndex > LifecycleEventCount ? LifecycleWriteIndex - LifecycleEventCount : 0,
				DebugName[0] ? DebugName : TEXT("<missing>"),
				OwnerName[0] ? OwnerName : TEXT("<missing>"),
				OwnerPath[0] ? OwnerPath : TEXT("<missing>"));

			for (uint32 LifecycleIndex = 0; LifecycleIndex < LifecycleEventCount; ++LifecycleIndex)
			{
				const FLifecycleEvent& Event = LifecycleEvents[LifecycleIndex];
				UE_LOG(LogRHI, Error,
					TEXT("RHI provenance lifecycle: identity_seq=%llu cycles=%llu thread=%u op=%s packed=0x%08x pc=0x%llx"),
					Event.Sequence,
					Event.Cycles,
					Event.ThreadId,
					GetOperationName(static_cast<EOperation>(Event.Operation)),
					Event.PackedValue,
					Event.CallerAddress);
			}
		}

		if (AddressGenerationCount > 1)
		{
			UE_LOG(LogRHI, Error,
				TEXT("RHI provenance ambiguity: resource address %p has %u retained generations; do not assume the newest generation is the failing object."),
				ResourceAddress,
				AddressGenerationCount);
		}
		else if (AddressGenerationCount == 0)
		{
			UE_LOG(LogRHI, Error,
				TEXT("RHI provenance identity miss: no retained generation for resource address %p. The identity may have been evicted or the pointer may never have referenced an instrumented FRHIResource."),
				ResourceAddress);
		}
	}

	void DumpEventMatches(const void* ResourceAddress, const void* FlagsAddress, uint64 ObservedResourceId)
	{
		FDumpEvent Matches[MaxDumpEvents] {};
		uint32 MatchCount = 0;
		uint64 TotalMatches = 0;
		uint64 TotalOverwritten = 0;

		const uint32 RequestedBuffers = GNextThreadBuffer.load(std::memory_order_acquire);
		const uint32 ClaimedBuffers = RequestedBuffers < ThreadBufferCount ? RequestedBuffers : ThreadBufferCount;
		for (uint32 BufferIndex = 0; BufferIndex < ClaimedBuffers; ++BufferIndex)
		{
			const FThreadBuffer& Buffer = GThreadBuffers[BufferIndex];
			if (Buffer.Claimed.load(std::memory_order_acquire) == 0)
			{
				continue;
			}

			const uint64 Writes = Buffer.TotalWrites.load(std::memory_order_relaxed);
			if (Writes > EventsPerThread)
			{
				TotalOverwritten += Writes - EventsPerThread;
			}

			for (const FEvent& Event : Buffer.Events)
			{
				FDumpEvent Candidate;
				if (!ReadEvent(Event, Candidate))
				{
					continue;
				}

				const bool bIdMatch = ObservedResourceId != 0 && Candidate.ResourceId == ObservedResourceId;
				const bool bResourceMatch = Candidate.ResourceAddress == reinterpret_cast<uint64>(ResourceAddress);
				const bool bFlagsMatch = Candidate.FlagsAddress == reinterpret_cast<uint64>(FlagsAddress);
				if (bIdMatch || bResourceMatch || bFlagsMatch)
				{
					InsertNewest(Matches, MatchCount, TotalMatches, Candidate);
				}
			}
		}

		SortBySequence(Matches, MatchCount);
		for (uint32 Index = 0; Index < MatchCount; ++Index)
		{
			const FDumpEvent& Event = Matches[Index];
			const EOperation Operation = static_cast<EOperation>(Event.OperationAndType & 0xff);
			const uint32 ResourceType = Event.OperationAndType >> 8;
			UE_LOG(LogRHI, Error,
				TEXT("RHI provenance event: thread_seq=%llu cycles=%llu thread=%u op=%s id=%llu resource=%p flags=%p type=%u packed=0x%08x pc=0x%llx correlation=%llu"),
				Event.Sequence,
				Event.Cycles,
				Event.ThreadId,
				GetOperationName(Operation),
				Event.ResourceId,
				reinterpret_cast<const void*>(Event.ResourceAddress),
				reinterpret_cast<const void*>(Event.FlagsAddress),
				ResourceType,
				Event.PackedValue,
				Event.CallerAddress,
				Event.CorrelationId);
		}

		UE_LOG(LogRHI, Error,
			TEXT("RHI provenance coverage: retained_matching_events=%u total_matching_events=%llu matching_events_omitted=%llu thread_buffers=%u/%u thread_buffer_overflows=%llu overwritten_events_all_threads=%llu identity_evictions=%llu."),
			MatchCount,
			TotalMatches,
			TotalMatches > MatchCount ? TotalMatches - MatchCount : 0,
			ClaimedBuffers,
			ThreadBufferCount,
			GThreadBufferOverflows.load(std::memory_order_relaxed),
			TotalOverwritten,
			GIdentityEvictions.load(std::memory_order_relaxed));
	}
}

uint64 RegisterResource(const void* ResourceAddress, const void* FlagsAddress, uint8 ResourceType, uint64 CallerAddress)
{
	const uint64 ResourceId = GNextResourceId.fetch_add(1, std::memory_order_relaxed);
	FIdentity& Identity = GIdentities[ResourceId % IdentityCapacity];

	LockIdentity(Identity);

	if (Identity.ResourceId.load(std::memory_order_relaxed) != 0)
	{
		GIdentityEvictions.fetch_add(1, std::memory_order_relaxed);
	}

	Identity.ResourceId.store(0, std::memory_order_release);
	Identity.ResourceAddress.store(reinterpret_cast<uint64>(ResourceAddress), std::memory_order_relaxed);
	Identity.FlagsAddress.store(reinterpret_cast<uint64>(FlagsAddress), std::memory_order_relaxed);
	Identity.CreateCaller.store(CallerAddress, std::memory_order_relaxed);
	Identity.LastStateCaller.store(CallerAddress, std::memory_order_relaxed);
	Identity.ResourceType.store(ResourceType, std::memory_order_relaxed);
	Identity.LastOperation.store(static_cast<uint32>(EOperation::Create), std::memory_order_relaxed);
	Identity.DebugName.Set(nullptr);
	Identity.OwnerName.Set(nullptr);
	Identity.OwnerPath.Set(nullptr);
	Identity.ResourceId.store(ResourceId, std::memory_order_release);

	UnlockIdentity(Identity);

	Record(EOperation::Create, ResourceAddress, FlagsAddress, ResourceId, ResourceType, 0, CallerAddress);
	return ResourceId;
}

void SetDebugName(uint64 ResourceId, const TCHAR* DebugName)
{
	if (FIdentity* Identity = FindIdentity(ResourceId))
	{
		LockIdentity(*Identity);
		if (Identity->ResourceId.load(std::memory_order_relaxed) == ResourceId)
		{
			Identity->DebugName.Set(DebugName);
		}
		UnlockIdentity(*Identity);
	}
}

void SetOwnerName(uint64 ResourceId, const TCHAR* OwnerName)
{
	if (FIdentity* Identity = FindIdentity(ResourceId))
	{
		LockIdentity(*Identity);
		if (Identity->ResourceId.load(std::memory_order_relaxed) == ResourceId)
		{
			Identity->OwnerName.Set(OwnerName);
		}
		UnlockIdentity(*Identity);
	}
}

void SetOwnerPath(uint64 ResourceId, const TCHAR* OwnerPath)
{
	if (FIdentity* Identity = FindIdentity(ResourceId))
	{
		LockIdentity(*Identity);
		if (Identity->ResourceId.load(std::memory_order_relaxed) == ResourceId)
		{
			Identity->OwnerPath.Set(OwnerPath);
		}
		UnlockIdentity(*Identity);
	}
}

void Record(
	EOperation Operation,
	const void* ResourceAddress,
	const void* FlagsAddress,
	uint64 ResourceId,
	uint8 ResourceType,
	uint32 PackedValue,
	uint64 CallerAddress,
	uint64 CorrelationId)
{
	FThreadBuffer* Buffer = GetThreadBuffer();
	if (!Buffer)
	{
		return;
	}

	const uint64 Sequence = Buffer->TotalWrites.fetch_add(1, std::memory_order_relaxed) + 1;
	FEvent& Event = Buffer->Events[Buffer->WriteIndex++ & (EventsPerThread - 1)];

	while (Event.Writer.test_and_set(std::memory_order_acquire))
	{
		FPlatformProcess::YieldThread();
	}

	Event.PublishedSequence = Sequence;
	Event.Cycles = FPlatformTime::Cycles64();
	Event.ResourceAddress = reinterpret_cast<uint64>(ResourceAddress);
	Event.FlagsAddress = reinterpret_cast<uint64>(FlagsAddress);
	Event.ResourceId = ResourceId;
	Event.CallerAddress = CallerAddress;
	Event.CorrelationId = CorrelationId;
	Event.PackedValue = PackedValue;
	Event.ThreadId = Buffer->ThreadId;
	Event.OperationAndType = static_cast<uint32>(Operation) | (static_cast<uint32>(ResourceType) << 8);

	Event.Writer.clear(std::memory_order_release);

	switch (Operation)
	{
	case EOperation::Create:
	case EOperation::FinalRelease:
	case EOperation::MarkForDelete:
	case EOperation::MarkForDeleteAlreadySet:
	case EOperation::DeleteCheck:
	case EOperation::DeleteBegin:
	case EOperation::DeleteCancelled:
	case EOperation::DestructorBegin:
	case EOperation::PhysicalFree:
		if (FIdentity* Identity = FindIdentity(ResourceId))
		{
			LockIdentity(*Identity);
			if (Identity->ResourceId.load(std::memory_order_relaxed) == ResourceId)
			{
				Identity->LastOperation.store(static_cast<uint32>(Operation), std::memory_order_relaxed);
				Identity->LastStateCaller.store(CallerAddress, std::memory_order_relaxed);

				const uint32 LifecycleIndex = Identity->LifecycleWriteIndex++;
				FLifecycleEvent& LifecycleEvent = Identity->Lifecycle[LifecycleIndex % LifecycleEventsPerIdentity];
				LifecycleEvent.Sequence = static_cast<uint64>(LifecycleIndex) + 1;
				LifecycleEvent.Cycles = FPlatformTime::Cycles64();
				LifecycleEvent.CallerAddress = CallerAddress;
				LifecycleEvent.PackedValue = PackedValue;
				LifecycleEvent.ThreadId = FPlatformTLS::GetCurrentThreadId();
				LifecycleEvent.Operation = static_cast<uint32>(Operation);
			}
			UnlockIdentity(*Identity);
		}
		break;
	default:
		break;
	}
}

uint64 BeginCommandUse(EOperation Operation, const void* ResourceAddress, uint64 CallerAddress)
{
	if (CVarRHIResourceProvenanceCommandUses.GetValueOnAnyThread() == 0)
	{
		return 0;
	}

	const uint64 CorrelationId =
		(static_cast<uint64>(FPlatformTLS::GetCurrentThreadId()) << 32) |
		static_cast<uint64>(++GTlsCommandSequence);

	Record(Operation, ResourceAddress, nullptr, 0, 0xff, 0, CallerAddress, CorrelationId);
	return CorrelationId;
}

void RecordCommandUse(EOperation Operation, const void* ResourceAddress, uint64 CorrelationId, uint64 CallerAddress)
{
	if (CorrelationId != 0)
	{
		Record(Operation, ResourceAddress, nullptr, 0, 0xff, 0, CallerAddress, CorrelationId);
	}
}

void ReportInvalidAtomic(
	const TCHAR* Reason,
	const void* ResourceAddress,
	const void* FlagsAddress,
	uint64 ObservedResourceId,
	uint8 ObservedResourceType,
	uint32 OldPacked,
	uint64 CallerAddress)
{
	UE_LOG(LogRHI, Error,
		TEXT("=== RHI RESOURCE PROVENANCE FAILURE === reason='%s' resource=%p flags=%p observed_id=%llu observed_type=%u old_packed=0x%08x caller_pc=0x%llx"),
		Reason,
		ResourceAddress,
		FlagsAddress,
		ObservedResourceId,
		ObservedResourceType,
		OldPacked,
		CallerAddress);

	DumpIdentityMatches(ResourceAddress, FlagsAddress, ObservedResourceId);
	DumpEventMatches(ResourceAddress, FlagsAddress, ObservedResourceId);

	UE_LOG(LogRHI, Error,
		TEXT("RHI provenance notes: PCs require exact-build symbols. owner_path='<missing>' means no higher-level UObject association was supplied. Events cover instrumented CPU operations only."));
}

} // namespace UE::RHI::ResourceProvenance

#endif // UE_BUILD_DEVELOPMENT
