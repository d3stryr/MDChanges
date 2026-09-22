// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHIResourceProvenance.h"

#if RHI_RESOURCE_PROVENANCE_ENABLED

#include "Containers/Array.h"
#include "Containers/StringConv.h"
#include "HAL/Event.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTLS.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "RHI.h"

#include <atomic>

namespace UE::RHI::ResourceProvenance
{
namespace
{
	static constexpr uint32 ThreadBufferCount = 128;
	static constexpr uint32 EventsPerThread = 2048;
	static constexpr uint32 IdentityCapacity = 32768;
	static constexpr uint32 DestroyedIdentityCapacity = 32768;
	static constexpr uint32 IdentityProbeLimit = 512;
	static constexpr uint64 ReservedIdentityId = MAX_uint64;
	static constexpr uint32 NameCapacity = 64;
	static constexpr uint32 PathCapacity = 96;
	static constexpr uint32 LifecycleEventsPerIdentity = 8;
	static constexpr uint32 MaxDumpEvents = 256;
	static constexpr uint32 JournalQueueCapacity = 16384;
	static constexpr uint32 JournalTextCapacity = 256;
	static constexpr uint32 JournalWriteBufferBytes = 256 * 1024;
	static constexpr uint32 JournalPollMilliseconds = 50;
	static constexpr uint32 JournalFlushMilliseconds = 2000;

	static_assert((EventsPerThread & (EventsPerThread - 1)) == 0, "EventsPerThread must be a power of two.");
	static_assert((IdentityCapacity & (IdentityCapacity - 1)) == 0, "IdentityCapacity must be a power of two.");
	static_assert((DestroyedIdentityCapacity & (DestroyedIdentityCapacity - 1)) == 0, "DestroyedIdentityCapacity must be a power of two.");
	static_assert((JournalQueueCapacity & (JournalQueueCapacity - 1)) == 0, "JournalQueueCapacity must be a power of two.");

	TAutoConsoleVariable<int32> CVarRHIResourceProvenanceJournal(
		TEXT("r.RHI.ResourceProvenance.Journal"),
		1,
		TEXT("Writes bounded RHI resource identity and lifecycle records to a background binary journal."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarRHIResourceProvenanceJournalMaxMB(
		TEXT("r.RHI.ResourceProvenance.JournalMaxMB"),
		10240,
		TEXT("Maximum size in MiB of the RHI resource provenance journal. Sampled when the journal starts."),
		ECVF_Default);

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

	enum class EJournalRecordKind : uint8
	{
		Create = 1,
		DebugName = 2,
		OwnerName = 3,
		OwnerPath = 4,
		Lifecycle = 5,
		CommandUse = 6
	};

	enum class EJournalRecordFlags : uint16
	{
		None = 0,
		TextTruncated = 1 << 0
	};
	ENUM_CLASS_FLAGS(EJournalRecordFlags);

	struct FJournalQueueRecord
	{
		uint64 Cycles = 0;
		uint64 ResourceId = 0;
		uint64 ResourceAddress = 0;
		uint64 FlagsAddress = 0;
		uint64 CallerAddress = 0;
		uint64 CorrelationId = 0;
		uint32 PackedValue = 0;
		uint32 ThreadId = 0;
		uint16 TextLength = 0;
		EJournalRecordFlags Flags = EJournalRecordFlags::None;
		EJournalRecordKind Kind = EJournalRecordKind::Lifecycle;
		EOperation Operation = EOperation::ExternalUse;
		uint8 ResourceType = 0xff;
		uint8 Reserved = 0;
		TCHAR Text[JournalTextCapacity] {};
	};

	struct FJournalQueueSlot
	{
		std::atomic<uint64> Sequence { 0 };
		FJournalQueueRecord Record;
	};

	class FJournalQueue
	{
	public:
		FJournalQueue()
		{
			for (uint64 Index = 0; Index < JournalQueueCapacity; ++Index)
			{
				Slots[Index].Sequence.store(Index, std::memory_order_relaxed);
			}
		}

		bool TryEnqueue(const FJournalQueueRecord& Record)
		{
			uint64 Position = EnqueuePosition.load(std::memory_order_relaxed);
			FJournalQueueSlot* Slot = nullptr;

			for (;;)
			{
				Slot = &Slots[Position & (JournalQueueCapacity - 1)];
				const uint64 Sequence = Slot->Sequence.load(std::memory_order_acquire);
				const int64 Difference = static_cast<int64>(Sequence) - static_cast<int64>(Position);
				if (Difference == 0)
				{
					if (EnqueuePosition.compare_exchange_weak(
						Position,
						Position + 1,
						std::memory_order_relaxed,
						std::memory_order_relaxed))
					{
						break;
					}
				}
				else if (Difference < 0)
				{
					return false;
				}
				else
				{
					Position = EnqueuePosition.load(std::memory_order_relaxed);
				}
			}

			Slot->Record = Record;
			Slot->Sequence.store(Position + 1, std::memory_order_release);
			return true;
		}

		bool TryDequeue(FJournalQueueRecord& OutRecord)
		{
			const uint64 Position = DequeuePosition.load(std::memory_order_relaxed);
			FJournalQueueSlot& Slot = Slots[Position & (JournalQueueCapacity - 1)];
			const uint64 Sequence = Slot.Sequence.load(std::memory_order_acquire);
			const int64 Difference = static_cast<int64>(Sequence) - static_cast<int64>(Position + 1);
			if (Difference != 0)
			{
				return false;
			}

			OutRecord = Slot.Record;
			Slot.Sequence.store(Position + JournalQueueCapacity, std::memory_order_release);
			DequeuePosition.store(Position + 1, std::memory_order_release);
			return true;
		}

		uint64 GetEnqueuePosition() const
		{
			return EnqueuePosition.load(std::memory_order_acquire);
		}

		uint64 GetDequeuePosition() const
		{
			return DequeuePosition.load(std::memory_order_acquire);
		}

	private:
		alignas(PLATFORM_CACHE_LINE_SIZE) std::atomic<uint64> EnqueuePosition { 0 };
		alignas(PLATFORM_CACHE_LINE_SIZE) std::atomic<uint64> DequeuePosition { 0 };
		FJournalQueueSlot Slots[JournalQueueCapacity];
	};

#pragma pack(push, 1)
	struct FJournalFileHeader
	{
		ANSICHAR Magic[8] {};
		uint16 Version = 1;
		uint16 HeaderSize = 0;
		uint32 EndianMarker = 0x01020304;
		uint8 PointerSize = sizeof(void*);
		uint8 TCHARSize = sizeof(TCHAR);
		uint16 Reserved = 0;
		uint32 QueueCapacity = JournalQueueCapacity;
		uint32 IdentityCapacityValue = IdentityCapacity;
		uint64 StartCycles = 0;
		double SecondsPerCycle64 = 0.0;
		uint64 MaximumFileBytes = 0;
	};

	struct FJournalDiskRecordHeader
	{
		uint32 Magic = 0x52504852; // "RHPR" in a little-endian byte stream.
		uint16 Version = 1;
		uint16 HeaderSize = 0;
		uint32 RecordSize = 0;
		uint8 Kind = 0;
		uint8 Operation = 0;
		uint8 ResourceType = 0xff;
		uint8 Reserved = 0;
		uint16 Flags = 0;
		uint16 TextBytes = 0;
		uint32 ThreadId = 0;
		uint32 PackedValue = 0;
		uint64 Cycles = 0;
		uint64 ResourceId = 0;
		uint64 ResourceAddress = 0;
		uint64 FlagsAddress = 0;
		uint64 CallerAddress = 0;
		uint64 CorrelationId = 0;
	};
#pragma pack(pop)

	class FJournalWriter final : public FRunnable
	{
	public:
		FJournalWriter() = default;

		~FJournalWriter()
		{
			Shutdown();
		}

		void Enqueue(const FJournalQueueRecord& Record)
		{
			EnsureStarted();
			if (StartState.load(std::memory_order_acquire) == 3)
			{
				DisabledOrStartFailureDrops.fetch_add(1, std::memory_order_relaxed);
				return;
			}

			if (!Queue.TryEnqueue(Record))
			{
				QueueDrops.fetch_add(1, std::memory_order_relaxed);
			}
		}

		bool FlushForFailure(uint32 TimeoutMilliseconds)
		{
			if (StartState.load(std::memory_order_acquire) != 2 || WakeEvent == nullptr)
			{
				return false;
			}

			const uint64 Request = FlushRequested.fetch_add(1, std::memory_order_acq_rel) + 1;
			WakeEvent->Trigger();

			const double Deadline = FPlatformTime::Seconds() + static_cast<double>(TimeoutMilliseconds) / 1000.0;
			while (FlushCompleted.load(std::memory_order_acquire) < Request && FPlatformTime::Seconds() < Deadline)
			{
				FPlatformProcess::SleepNoStats(0.001f);
			}
			return FlushCompleted.load(std::memory_order_acquire) >= Request;
		}

		const FString& GetPath() const
		{
			return JournalPath;
		}

		uint64 GetQueueDrops() const
		{
			return QueueDrops.load(std::memory_order_relaxed);
		}

		uint64 GetDiskCapDrops() const
		{
			return DiskCapDrops.load(std::memory_order_relaxed);
		}

		uint64 GetWriteFailures() const
		{
			return WriteFailures.load(std::memory_order_relaxed);
		}

		uint64 GetDisabledOrStartFailureDrops() const
		{
			return DisabledOrStartFailureDrops.load(std::memory_order_relaxed);
		}

		uint64 GetBytesWritten() const
		{
			return BytesWritten.load(std::memory_order_relaxed);
		}

		uint64 GetQueuedRecords() const
		{
			return Queue.GetEnqueuePosition();
		}

		uint64 GetDrainedRecords() const
		{
			return Queue.GetDequeuePosition();
		}

		virtual uint32 Run() override
		{
			double LastPeriodicFlushSeconds = FPlatformTime::Seconds();

			while (!bStopRequested.load(std::memory_order_acquire))
			{
				DrainQueue();

				const uint64 RequestedFlush = FlushRequested.load(std::memory_order_acquire);
				if (RequestedFlush > FlushCompleted.load(std::memory_order_relaxed))
				{
					CommitBuffer(true);
					FlushCompleted.store(RequestedFlush, std::memory_order_release);
				}
				else
				{
					const double CurrentSeconds = FPlatformTime::Seconds();
					if ((CurrentSeconds - LastPeriodicFlushSeconds) * 1000.0 >= JournalFlushMilliseconds)
					{
						CommitBuffer(false);
						LastPeriodicFlushSeconds = CurrentSeconds;
					}
				}

				if (WakeEvent)
				{
					WakeEvent->Wait(JournalPollMilliseconds, true);
				}
			}

			DrainQueue();
			CommitBuffer(true);
			FlushCompleted.store(FlushRequested.load(std::memory_order_relaxed), std::memory_order_release);
			return 0;
		}

		virtual void Stop() override
		{
			bStopRequested.store(true, std::memory_order_release);
			if (WakeEvent)
			{
				WakeEvent->Trigger();
			}
		}

	private:
		void EnsureStarted()
		{
			int32 ExpectedState = 0;
			if (!StartState.compare_exchange_strong(
				ExpectedState,
				1,
				std::memory_order_acq_rel,
				std::memory_order_acquire))
			{
				return;
			}

			if (CVarRHIResourceProvenanceJournal.GetValueOnAnyThread() == 0)
			{
				StartState.store(3, std::memory_order_release);
				return;
			}

			const int32 MaximumMiB = FMath::Clamp(
				CVarRHIResourceProvenanceJournalMaxMB.GetValueOnAnyThread(),
				16,
				16384);
			MaximumFileBytes = static_cast<uint64>(MaximumMiB) * 1024ull * 1024ull;

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			const FString LogDirectory = FPaths::ProjectLogDir();
			if (!PlatformFile.CreateDirectoryTree(*LogDirectory))
			{
				StartState.store(3, std::memory_order_release);
				return;
			}

			const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
			JournalPath = FPaths::Combine(
				LogDirectory,
				FString::Printf(
					TEXT("RHIResourceProvenance-%s-%u.rhiprov"),
					*Timestamp,
					FPlatformProcess::GetCurrentProcessId()));

			FileHandle = PlatformFile.OpenWrite(*JournalPath, false, false);
			if (!FileHandle)
			{
				StartState.store(3, std::memory_order_release);
				return;
			}

			FJournalFileHeader Header;
			FMemory::Memcpy(Header.Magic, "RHIPROV", 7);
			Header.HeaderSize = static_cast<uint16>(sizeof(Header));
			Header.StartCycles = FPlatformTime::Cycles64();
			Header.SecondsPerCycle64 = FPlatformTime::GetSecondsPerCycle64();
			Header.MaximumFileBytes = MaximumFileBytes;
			if (!FileHandle->Write(reinterpret_cast<const uint8*>(&Header), sizeof(Header)))
			{
				++WriteFailures;
				delete FileHandle;
				FileHandle = nullptr;
				StartState.store(3, std::memory_order_release);
				return;
			}
			BytesWritten.store(sizeof(Header), std::memory_order_relaxed);

			WriteBuffer.Reserve(JournalWriteBufferBytes);
			WakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
			Thread = FRunnableThread::Create(this, TEXT("RHIResourceProvenanceJournal"), 0, TPri_BelowNormal);
			if (!Thread)
			{
				if (WakeEvent)
				{
					FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
					WakeEvent = nullptr;
				}
				delete FileHandle;
				FileHandle = nullptr;
				StartState.store(3, std::memory_order_release);
				return;
			}

			StartState.store(2, std::memory_order_release);
			UE_LOG(LogRHI, Display, TEXT("RHI resource provenance journal: '%s' maximum=%d MiB queue=%u records."),
				*JournalPath,
				MaximumMiB,
				JournalQueueCapacity);
		}

		void Shutdown()
		{
			if (StartState.load(std::memory_order_acquire) == 2 && Thread)
			{
				Stop();
				Thread->WaitForCompletion();
				delete Thread;
				Thread = nullptr;
			}

			if (WakeEvent)
			{
				FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
				WakeEvent = nullptr;
			}

			if (FileHandle)
			{
				delete FileHandle;
				FileHandle = nullptr;
			}
		}

		void DrainQueue()
		{
			FJournalQueueRecord Record;
			while (Queue.TryDequeue(Record))
			{
				AppendRecord(Record);
			}

			if (WriteBuffer.Num() >= static_cast<int32>(JournalWriteBufferBytes))
			{
				CommitBuffer(false);
			}
		}

		void AppendRecord(const FJournalQueueRecord& Record)
		{
			FTCHARToUTF8 ConvertedText(Record.Text);
			const uint16 TextBytes = static_cast<uint16>(FMath::Min<int32>(ConvertedText.Length(), MAX_uint16));

			FJournalDiskRecordHeader Header;
			Header.HeaderSize = static_cast<uint16>(sizeof(Header));
			Header.RecordSize = static_cast<uint32>(sizeof(Header)) + TextBytes;
			Header.Kind = static_cast<uint8>(Record.Kind);
			Header.Operation = static_cast<uint8>(Record.Operation);
			Header.ResourceType = Record.ResourceType;
			Header.Flags = static_cast<uint16>(Record.Flags);
			Header.TextBytes = TextBytes;
			Header.ThreadId = Record.ThreadId;
			Header.PackedValue = Record.PackedValue;
			Header.Cycles = Record.Cycles;
			Header.ResourceId = Record.ResourceId;
			Header.ResourceAddress = Record.ResourceAddress;
			Header.FlagsAddress = Record.FlagsAddress;
			Header.CallerAddress = Record.CallerAddress;
			Header.CorrelationId = Record.CorrelationId;

			const uint64 PendingBytes =
				static_cast<uint64>(WriteBuffer.Num()) +
				static_cast<uint64>(Header.RecordSize);
			if (BytesWritten.load(std::memory_order_relaxed) + PendingBytes > MaximumFileBytes)
			{
				DiskCapDrops.fetch_add(1, std::memory_order_relaxed);
				return;
			}

			if (WriteBuffer.Num() + static_cast<int32>(Header.RecordSize) > static_cast<int32>(JournalWriteBufferBytes))
			{
				CommitBuffer(false);
			}

			WriteBuffer.Append(reinterpret_cast<const uint8*>(&Header), static_cast<int32>(sizeof(Header)));
			if (TextBytes > 0)
			{
				WriteBuffer.Append(reinterpret_cast<const uint8*>(ConvertedText.Get()), TextBytes);
			}
		}

		void CommitBuffer(bool bFullFlush)
		{
			if (!FileHandle)
			{
				WriteBuffer.Reset();
				return;
			}

			if (WriteBuffer.Num() > 0)
			{
				if (FileHandle->Write(WriteBuffer.GetData(), WriteBuffer.Num()))
				{
					BytesWritten.fetch_add(static_cast<uint64>(WriteBuffer.Num()), std::memory_order_relaxed);
				}
				else
				{
					WriteFailures.fetch_add(1, std::memory_order_relaxed);
				}
				WriteBuffer.Reset();
			}

			if (!FileHandle->Flush(bFullFlush))
			{
				WriteFailures.fetch_add(1, std::memory_order_relaxed);
			}
		}

		FJournalQueue Queue;
		std::atomic<int32> StartState { 0 };
		std::atomic<bool> bStopRequested { false };
		std::atomic<uint64> FlushRequested { 0 };
		std::atomic<uint64> FlushCompleted { 0 };
		std::atomic<uint64> QueueDrops { 0 };
		std::atomic<uint64> DiskCapDrops { 0 };
		std::atomic<uint64> WriteFailures { 0 };
		std::atomic<uint64> DisabledOrStartFailureDrops { 0 };
		std::atomic<uint64> BytesWritten { 0 };

		FRunnableThread* Thread = nullptr;
		::FEvent* WakeEvent = nullptr;
		IFileHandle* FileHandle = nullptr;
		FString JournalPath;
		TArray<uint8> WriteBuffer;
		uint64 MaximumFileBytes = 0;
	};

	FJournalWriter GJournalWriter;

	void CopyJournalText(FJournalQueueRecord& Record, const TCHAR* Source)
	{
		if (!Source)
		{
			return;
		}

		uint32 Index = 0;
		for (; Index + 1 < JournalTextCapacity && Source[Index] != 0; ++Index)
		{
			Record.Text[Index] = Source[Index];
		}
		Record.Text[Index] = 0;
		Record.TextLength = static_cast<uint16>(Index);
		if (Source[Index] != 0)
		{
			Record.Flags |= EJournalRecordFlags::TextTruncated;
		}
	}

	FJournalQueueRecord MakeJournalRecord(
		EJournalRecordKind Kind,
		EOperation Operation,
		const void* ResourceAddress,
		const void* FlagsAddress,
		uint64 ResourceId,
		uint8 ResourceType,
		uint32 PackedValue,
		uint64 CallerAddress,
		uint64 CorrelationId = 0)
	{
		FJournalQueueRecord Record;
		Record.Cycles = FPlatformTime::Cycles64();
		Record.ResourceId = ResourceId;
		Record.ResourceAddress = reinterpret_cast<uint64>(ResourceAddress);
		Record.FlagsAddress = reinterpret_cast<uint64>(FlagsAddress);
		Record.CallerAddress = CallerAddress;
		Record.CorrelationId = CorrelationId;
		Record.PackedValue = PackedValue;
		Record.ThreadId = FPlatformTLS::GetCurrentThreadId();
		Record.Kind = Kind;
		Record.Operation = Operation;
		Record.ResourceType = ResourceType;
		return Record;
	}

	bool IsJournaledOperation(EOperation Operation)
	{
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
		case EOperation::CommandEnqueue:
		case EOperation::CommandExecute:
		case EOperation::UpdateRequest:
		case EOperation::UpdateExecute:
			return true;
		default:
			return false;
		}
	}

	EJournalRecordKind GetJournalRecordKind(EOperation Operation)
	{
		switch (Operation)
		{
		case EOperation::Create:
			return EJournalRecordKind::Create;
		case EOperation::CommandEnqueue:
		case EOperation::CommandExecute:
		case EOperation::UpdateRequest:
		case EOperation::UpdateExecute:
			return EJournalRecordKind::CommandUse;
		default:
			return EJournalRecordKind::Lifecycle;
		}
	}

	FThreadBuffer GThreadBuffers[ThreadBufferCount];
	FIdentity GIdentities[IdentityCapacity];
	FIdentity GDestroyedIdentities[DestroyedIdentityCapacity];

	std::atomic<uint32> GNextThreadBuffer { 0 };
	std::atomic<uint64> GNextResourceId { 1 };
	std::atomic<uint64> GNextDestroyedIdentity { 0 };
	std::atomic<uint64> GThreadBufferOverflows { 0 };
	std::atomic<uint64> GActiveIdentityOverflows { 0 };
	std::atomic<uint64> GDestroyedIdentityEvictions { 0 };

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
		if (ResourceId == 0 || ResourceId == ReservedIdentityId)
		{
			return nullptr;
		}

		const uint32 StartIndex = static_cast<uint32>(ResourceId) & (IdentityCapacity - 1);
		for (uint32 Probe = 0; Probe < IdentityProbeLimit; ++Probe)
		{
			FIdentity& Identity = GIdentities[(StartIndex + Probe) & (IdentityCapacity - 1)];
			if (Identity.ResourceId.load(std::memory_order_acquire) == ResourceId)
			{
				return &Identity;
			}
		}
		return nullptr;
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

	void CopyIdentityState(FIdentity& Destination, const FIdentity& Source, uint64 ResourceId)
	{
		TCHAR DebugName[NameCapacity] {};
		TCHAR OwnerName[NameCapacity] {};
		TCHAR OwnerPath[PathCapacity] {};
		Source.DebugName.Get(DebugName);
		Source.OwnerName.Get(OwnerName);
		Source.OwnerPath.Get(OwnerPath);

		Destination.ResourceId.store(0, std::memory_order_release);
		Destination.ResourceAddress.store(Source.ResourceAddress.load(std::memory_order_relaxed), std::memory_order_relaxed);
		Destination.FlagsAddress.store(Source.FlagsAddress.load(std::memory_order_relaxed), std::memory_order_relaxed);
		Destination.CreateCaller.store(Source.CreateCaller.load(std::memory_order_relaxed), std::memory_order_relaxed);
		Destination.LastStateCaller.store(Source.LastStateCaller.load(std::memory_order_relaxed), std::memory_order_relaxed);
		Destination.ResourceType.store(Source.ResourceType.load(std::memory_order_relaxed), std::memory_order_relaxed);
		Destination.LastOperation.store(Source.LastOperation.load(std::memory_order_relaxed), std::memory_order_relaxed);
		Destination.LifecycleWriteIndex = Source.LifecycleWriteIndex;
		for (uint32 Index = 0; Index < LifecycleEventsPerIdentity; ++Index)
		{
			Destination.Lifecycle[Index] = Source.Lifecycle[Index];
		}
		Destination.DebugName.Set(DebugName);
		Destination.OwnerName.Set(OwnerName);
		Destination.OwnerPath.Set(OwnerPath);
		Destination.ResourceId.store(ResourceId, std::memory_order_release);
	}

	void RetireIdentity(FIdentity& Identity, uint64 ResourceId)
	{
		const uint64 Sequence = GNextDestroyedIdentity.fetch_add(1, std::memory_order_relaxed);
		FIdentity& Destination = GDestroyedIdentities[Sequence & (DestroyedIdentityCapacity - 1)];

		LockIdentity(Destination);
		if (Destination.ResourceId.load(std::memory_order_relaxed) != 0)
		{
			GDestroyedIdentityEvictions.fetch_add(1, std::memory_order_relaxed);
		}
		CopyIdentityState(Destination, Identity, ResourceId);
		UnlockIdentity(Destination);

		Identity.ResourceId.store(0, std::memory_order_release);
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

		auto DumpTable = [&](FIdentity* Identities, uint32 Count, const TCHAR* StorageName)
		{
			for (uint32 IdentityIndex = 0; IdentityIndex < Count; ++IdentityIndex)
			{
				FIdentity& Identity = Identities[IdentityIndex];
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
					UE_LOG(LogRHI, Error, TEXT("RHI provenance %s identity slot busy during failure dump; one matching identity may be incomplete."), StorageName);
					continue;
				}

				const uint64 IdentityId = Identity.ResourceId.load(std::memory_order_relaxed);
				const uint64 IdentityResource = Identity.ResourceAddress.load(std::memory_order_relaxed);
				const uint64 IdentityFlags = Identity.FlagsAddress.load(std::memory_order_relaxed);
				const bool bIdMatch = ObservedResourceId != 0 && IdentityId == ObservedResourceId;
				const bool bAddressMatch = IdentityResource == reinterpret_cast<uint64>(ResourceAddress);
				const bool bFlagsMatch = IdentityFlags == reinterpret_cast<uint64>(FlagsAddress);
				if (IdentityId == 0 || IdentityId == ReservedIdentityId || (!bIdMatch && !bAddressMatch && !bFlagsMatch))
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
					TEXT("RHI provenance identity: storage=%s id=%llu resource=%p flags=%p type=%u create_pc=0x%llx last_state=%s last_state_pc=0x%llx lifecycle_total=%u lifecycle_retained=%u lifecycle_omitted=%u debug='%s' owner='%s' owner_path='%s'"),
					StorageName,
					static_cast<unsigned long long>(IdentityId),
					reinterpret_cast<const void*>(IdentityResource),
					reinterpret_cast<const void*>(IdentityFlags),
					ResourceType,
					static_cast<unsigned long long>(CreateCaller),
					GetOperationName(LastOperation),
					static_cast<unsigned long long>(LastStateCaller),
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
						TEXT("RHI provenance lifecycle: storage=%s identity_seq=%llu cycles=%llu thread=%u op=%s packed=0x%08x pc=0x%llx"),
						StorageName,
						static_cast<unsigned long long>(Event.Sequence),
						static_cast<unsigned long long>(Event.Cycles),
						Event.ThreadId,
						GetOperationName(static_cast<EOperation>(Event.Operation)),
						Event.PackedValue,
						static_cast<unsigned long long>(Event.CallerAddress));
				}
			}
		};

		DumpTable(GIdentities, IdentityCapacity, TEXT("active"));
		DumpTable(GDestroyedIdentities, DestroyedIdentityCapacity, TEXT("destroyed"));

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
				TEXT("RHI provenance identity miss: no active or recently destroyed generation for resource address %p. Consult the persistent journal; the active table may have overflowed, destroyed history may have been evicted, or the pointer may never have referenced an instrumented FRHIResource."),
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
				static_cast<unsigned long long>(Event.Sequence),
				static_cast<unsigned long long>(Event.Cycles),
				Event.ThreadId,
				GetOperationName(Operation),
				static_cast<unsigned long long>(Event.ResourceId),
				reinterpret_cast<const void*>(Event.ResourceAddress),
				reinterpret_cast<const void*>(Event.FlagsAddress),
				ResourceType,
				Event.PackedValue,
				static_cast<unsigned long long>(Event.CallerAddress),
				static_cast<unsigned long long>(Event.CorrelationId));
		}

		UE_LOG(LogRHI, Error,
			TEXT("RHI provenance coverage: retained_matching_events=%u total_matching_events=%llu matching_events_omitted=%llu thread_buffers=%u/%u thread_buffer_overflows=%llu overwritten_events_all_threads=%llu active_identity_overflows=%llu destroyed_identity_evictions=%llu."),
			MatchCount,
			static_cast<unsigned long long>(TotalMatches),
			static_cast<unsigned long long>(TotalMatches > MatchCount ? TotalMatches - MatchCount : 0),
			ClaimedBuffers,
			ThreadBufferCount,
			static_cast<unsigned long long>(GThreadBufferOverflows.load(std::memory_order_relaxed)),
			static_cast<unsigned long long>(TotalOverwritten),
			static_cast<unsigned long long>(GActiveIdentityOverflows.load(std::memory_order_relaxed)),
			static_cast<unsigned long long>(GDestroyedIdentityEvictions.load(std::memory_order_relaxed)));
	}
}

uint64 RegisterResource(const void* ResourceAddress, const void* FlagsAddress, uint8 ResourceType, uint64 CallerAddress)
{
	const uint64 ResourceId = GNextResourceId.fetch_add(1, std::memory_order_relaxed);
	const uint32 StartIndex = static_cast<uint32>(ResourceId) & (IdentityCapacity - 1);
	FIdentity* ClaimedIdentity = nullptr;

	for (uint32 Probe = 0; Probe < IdentityProbeLimit; ++Probe)
	{
		FIdentity& Candidate = GIdentities[(StartIndex + Probe) & (IdentityCapacity - 1)];
		uint64 ExpectedId = 0;
		if (Candidate.ResourceId.compare_exchange_strong(
			ExpectedId,
			ReservedIdentityId,
			std::memory_order_acq_rel,
			std::memory_order_relaxed))
		{
			ClaimedIdentity = &Candidate;
			break;
		}
	}

	if (ClaimedIdentity)
	{
		LockIdentity(*ClaimedIdentity);
		ClaimedIdentity->ResourceAddress.store(reinterpret_cast<uint64>(ResourceAddress), std::memory_order_relaxed);
		ClaimedIdentity->FlagsAddress.store(reinterpret_cast<uint64>(FlagsAddress), std::memory_order_relaxed);
		ClaimedIdentity->CreateCaller.store(CallerAddress, std::memory_order_relaxed);
		ClaimedIdentity->LastStateCaller.store(CallerAddress, std::memory_order_relaxed);
		ClaimedIdentity->ResourceType.store(ResourceType, std::memory_order_relaxed);
		ClaimedIdentity->LastOperation.store(static_cast<uint32>(EOperation::Create), std::memory_order_relaxed);
		ClaimedIdentity->LifecycleWriteIndex = 0;
		for (FLifecycleEvent& LifecycleEvent : ClaimedIdentity->Lifecycle)
		{
			LifecycleEvent = {};
		}
		ClaimedIdentity->DebugName.Set(nullptr);
		ClaimedIdentity->OwnerName.Set(nullptr);
		ClaimedIdentity->OwnerPath.Set(nullptr);
		ClaimedIdentity->ResourceId.store(ResourceId, std::memory_order_release);
		UnlockIdentity(*ClaimedIdentity);
	}
	else
	{
		GActiveIdentityOverflows.fetch_add(1, std::memory_order_relaxed);
	}

	Record(EOperation::Create, ResourceAddress, FlagsAddress, ResourceId, ResourceType, 0, CallerAddress);
	return ResourceId;
}

void SetDebugName(
	uint64 ResourceId,
	const void* ResourceAddress,
	const void* FlagsAddress,
	uint8 ResourceType,
	const TCHAR* DebugName)
{
	FJournalQueueRecord JournalRecord = MakeJournalRecord(
		EJournalRecordKind::DebugName,
		EOperation::ExternalUse,
		ResourceAddress,
		FlagsAddress,
		ResourceId,
		ResourceType,
		0,
		0);
	CopyJournalText(JournalRecord, DebugName);
	GJournalWriter.Enqueue(JournalRecord);

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

void SetOwnerName(
	uint64 ResourceId,
	const void* ResourceAddress,
	const void* FlagsAddress,
	uint8 ResourceType,
	const TCHAR* OwnerName)
{
	FJournalQueueRecord JournalRecord = MakeJournalRecord(
		EJournalRecordKind::OwnerName,
		EOperation::ExternalUse,
		ResourceAddress,
		FlagsAddress,
		ResourceId,
		ResourceType,
		0,
		0);
	CopyJournalText(JournalRecord, OwnerName);
	GJournalWriter.Enqueue(JournalRecord);

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

void SetOwnerPath(
	uint64 ResourceId,
	const void* ResourceAddress,
	const void* FlagsAddress,
	uint8 ResourceType,
	const TCHAR* OwnerPath)
{
	FJournalQueueRecord JournalRecord = MakeJournalRecord(
		EJournalRecordKind::OwnerPath,
		EOperation::ExternalUse,
		ResourceAddress,
		FlagsAddress,
		ResourceId,
		ResourceType,
		0,
		0);
	CopyJournalText(JournalRecord, OwnerPath);
	GJournalWriter.Enqueue(JournalRecord);

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
	if (FThreadBuffer* Buffer = GetThreadBuffer())
	{
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
	}

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

				if (Operation == EOperation::PhysicalFree)
				{
					RetireIdentity(*Identity, ResourceId);
				}
			}
			UnlockIdentity(*Identity);
		}
		break;
	default:
		break;
	}

	if (IsJournaledOperation(Operation))
	{
		GJournalWriter.Enqueue(MakeJournalRecord(
			GetJournalRecordKind(Operation),
			Operation,
			ResourceAddress,
			FlagsAddress,
			ResourceId,
			ResourceType,
			PackedValue,
			CallerAddress,
			CorrelationId));
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
	const bool bJournalFlushed = GJournalWriter.FlushForFailure(1000);
	UE_LOG(LogRHI, Error,
		TEXT("RHI provenance journal: path='%s' failure_flush=%s queued=%llu drained=%llu bytes=%llu queue_drops=%llu disk_cap_drops=%llu write_failures=%llu disabled_or_start_failure_drops=%llu"),
		GJournalWriter.GetPath().IsEmpty() ? TEXT("<unavailable>") : *GJournalWriter.GetPath(),
		bJournalFlushed ? TEXT("yes") : TEXT("no"),
		static_cast<unsigned long long>(GJournalWriter.GetQueuedRecords()),
		static_cast<unsigned long long>(GJournalWriter.GetDrainedRecords()),
		static_cast<unsigned long long>(GJournalWriter.GetBytesWritten()),
		static_cast<unsigned long long>(GJournalWriter.GetQueueDrops()),
		static_cast<unsigned long long>(GJournalWriter.GetDiskCapDrops()),
		static_cast<unsigned long long>(GJournalWriter.GetWriteFailures()),
		static_cast<unsigned long long>(GJournalWriter.GetDisabledOrStartFailureDrops()));

	UE_LOG(LogRHI, Error,
		TEXT("=== RHI RESOURCE PROVENANCE FAILURE === reason='%s' resource=%p flags=%p observed_id=%llu observed_type=%u old_packed=0x%08x caller_pc=0x%llx"),
		Reason,
		ResourceAddress,
		FlagsAddress,
		static_cast<unsigned long long>(ObservedResourceId),
		ObservedResourceType,
		OldPacked,
		static_cast<unsigned long long>(CallerAddress));

	DumpIdentityMatches(ResourceAddress, FlagsAddress, ObservedResourceId);
	DumpEventMatches(ResourceAddress, FlagsAddress, ObservedResourceId);

	UE_LOG(LogRHI, Error,
		TEXT("RHI provenance notes: PCs require exact-build symbols. owner_path='<missing>' means no higher-level UObject association was supplied. Events cover instrumented CPU operations only."));
}

} // namespace UE::RHI::ResourceProvenance

#endif // RHI_RESOURCE_PROVENANCE_ENABLED
