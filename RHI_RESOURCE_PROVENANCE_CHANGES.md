# UE 5.8.2 PS5 RHI Resource Provenance Changes

This repository mirrors complete modified files from `d3stryr/UnrealEngine` for the PS5 Development-build RHI lifetime investigation.

## Source snapshot

- Source repository: [d3stryr/UnrealEngine](https://github.com/d3stryr/UnrealEngine)
- Source branch: [diagnostics/rhi-resource-provenance](https://github.com/d3stryr/UnrealEngine/tree/diagnostics/rhi-resource-provenance)
- Source commit: [4a593025ba1f2f5cc2ccfc9c71a15ad9e08ff068](https://github.com/d3stryr/UnrealEngine/commit/4a593025ba1f2f5cc2ccfc9c71a15ad9e08ff068)
- Engine version: 5.8.2
- Initial mirror date: 2026-09-21

The files below are complete snapshots, not patch fragments. Their paths match the Unreal Engine checkout so they can be copied over the engine root.

## Mirrored files

| File | Source blob | Status | Purpose |
|---|---|---|---|
| `Engine/Source/Runtime/RHI/RHI.Build.cs` | `c59a8c678f0c4d162b9568a695c7170356db62bc` | Modified | Defines `RHI_RESOURCE_PROVENANCE_ENABLED` for Debug, DebugGame, and Development; disables it for Test and Shipping. |
| `Engine/Source/Runtime/RHI/Public/RHIResourceProvenance.h` | `4573061c1a0c08d2d4a3b6c4acd09ddf97109e2f` | Added | Non-production recorder interface, operation types, caller capture, metadata identity fields, and command correlation API. |
| `Engine/Source/Runtime/RHI/Private/RHIResourceProvenance.cpp` | `e0080f94a75e31f01055fc6dfd57afc5a08ae271` | Added | Bounded per-thread events, active/destroyed identity retention, background binary journal, lifecycle history, failure reporting, and optional command-use capture. |
| `Engine/Source/Runtime/RHI/Public/RHIResources.h` | `d8887be077f6050b54a3db0280127f0778e8e3ed` | Modified | Instruments AddRef, Release, deletion transitions, generation IDs, and name/owner capture with resource/flags identity fields. |
| `Engine/Source/Runtime/RHI/Private/RHIResources.cpp` | `54eda7f84d693f3f6aa57e562ab87086571df865` | Modified | Registers identities, records destruction/delete completion, and copies available resource names. |
| `Engine/Source/Runtime/RHI/Public/RHICommandList.h` | `99c46d15e33b82159689efbb914614970163bf27` | Modified | Adds optional request/command correlation for shader resources, static uniform buffers, and uniform-buffer updates. |
| `Engine/Source/Runtime/RHI/Public/RHICommandListCommandExecutes.inl` | `fe347fa221f615ec0e8f9e0da6f0e9a3c90e8db4` | Modified | Records correlated execution of selected RHI commands. |
| `Engine/Build/BatchFiles/DecodeRHIResourceProvenance.py` | `f5aa614302a7a67b93f3921a90006c3e58db1330` | Added | Streams and filters `.rhiprov` journals by generation ID, resource address, or flags address and emits readable TSV. |

## Current behavior

The core recorder is compiled when `RHI_RESOURCE_PROVENANCE_ENABLED` is `1`. `RHI.Build.cs` defines it for `Debug`, `DebugGame`, and `Development`, independently of `UE_BUILD_DEVELOPMENT`. It is disabled for `Test` and `Shipping`.

It records:

- resource creation and generation identity;
- resource and `FAtomicFlags` addresses;
- AddRef and Release operations with the atomic `OldPacked` value;
- final reference decrement;
- MarkedForDelete and DeletingBit transitions;
- deletion cancellation when a resource is revived;
- destructor entry and completion of the delete expression;
- bounded per-thread history;
- eight retained lifecycle transitions per identity;
- address-reuse ambiguity;
- active-table overflow, destroyed-history eviction, event overwrite, journal queue-drop, disk-cap, flush, and write-failure counters;
- available debug names, owner names, and externally supplied owner paths;
- a bounded append-only binary journal written by a below-normal-priority background thread.

Selected command-use tracing is disabled by default. Enable it with:

```ini
r.RHI.ResourceProvenance.CommandUses=1
```

It currently covers shader resource/bindless parameters, static uniform-buffer binding, and uniform-buffer update dispatch. It does not cover every raw-pointer read, every RHI operation, or GPU execution.

## Failure output

The relevant log section begins with:

```text
=== RHI RESOURCE PROVENANCE FAILURE ===
```

Captured PCs must be resolved with symbols from the exact diagnostic build. The recorded caller address is one operation site, not a full stack.

The journal is created through `FPaths::ProjectLogDir()` with a name such as:

```text
RHIResourceProvenance-20260922-123456-1234.rhiprov
```

Decode a failing generation or address with:

```bash
python Engine/Build/BatchFiles/DecodeRHIResourceProvenance.py <journal.rhiprov> --id 1047167
python Engine/Build/BatchFiles/DecodeRHIResourceProvenance.py <journal.rhiprov> --address 0x106b8df2e0
```

The assertion path requests a full journal flush for up to one second and reports the resolved path, queued/drained counts, bytes written, queue drops, disk-cap drops, write failures, and startup failures.

A `PhysicalFree` event means the C++ delete expression completed. Memory Insights remains the authoritative source for allocation/free stacks and containing allocation ranges.

## Storage limits

- 128 thread buffers
- 2,048 events per thread
- 32,768 preallocated active identity slots; live identities are not displaced by newer IDs
- 32,768 recently destroyed identity generations
- 8 lifecycle events per active/destroyed identity
- 16,384 preallocated journal queue records
- 256 KiB background write buffer
- 1,024 MiB default journal cap, configurable before journal startup with `r.RHI.ResourceProvenance.JournalMaxMB`
- 256 matching events emitted during failure reporting

These limits intentionally bound memory and disk use. Event overwrites, active-table overflow, destroyed-history eviction, journal queue drops, disk-cap drops, thread-buffer exhaustion, and omitted matching events are reported. Normal AddRef/Release events remain in memory; creation, identity metadata, and decisive lifecycle operations are persisted.

## Validation status

- Source files were checked against the committed diagnostic branch.
- Preprocessor and delimiter balance checks passed.
- The first recorder revision compiled and executed on PS5, captured the `0xDD` failure, and recovered a prior valid type-18 generation at the same resource/flags addresses.
- The active/destroyed identity and persistent-journal revision has passed source/mirror equality, delimiter/preprocessor balance, and Python decoder syntax checks, but has not yet been compiled with the PS5 SDK/toolchain.
- A full diagnostic PS5 rebuild is required because the instrumented `FRHIResource` layout and RHI module implementation changed.

## Change log

### 2026-09-22 — Fix journal FEvent name collision

- PS5 compilation showed that the recorder's internal `FEvent` history struct shadowed Unreal Core's global synchronization-event type.
- Qualified the journal wake-event pointer as `::FEvent*`.
- This resolves the repeated `Trigger`, `Wait`, `GetSynchEventFromPool`, and `ReturnSynchEventToPool` conversion/member errors.
- Updated the complete mirrored `RHIResourceProvenance.cpp`.

### 2026-09-22 — Preserve active identities and add persistent journal

- The PS5 run recovered generation `1047167`, resource type `18` (`RRT_UniformBuffer`), at the same resource and flags addresses before the failing fields became `0xDD`.
- The failure report also showed roughly 2.1 million identity evictions, proving the modulo-by-ID table could discard a still-live, long-lived resource.
- Replaced modulo overwrite with a bounded open-addressed active table. An active identity is removed only after the recorded `PhysicalFree` transition.
- Added a separate 32,768-entry recently destroyed-generation ring so stale-pointer failures can still recover copied metadata after destruction.
- Added a 16,384-record preallocated MPMC journal queue. Resource threads perform fixed-size publication only; a below-normal-priority thread performs string conversion and file I/O.
- Added bounded binary persistence for creation, debug-name, owner-name, owner-path, final release, deletion, destructor, and physical-free records.
- Added a 1,024 MiB configurable disk cap, 256 KiB write buffer, periodic flush, one-second assertion-time flush request, and explicit loss/failure counters.
- Journal metadata updates now carry resource address, flags address, generation ID, and RHI type even if the in-memory identity has already been displaced.
- Added `DecodeRHIResourceProvenance.py` to filter journals by ID/resource/flags address and emit readable TSV.
- Kept UObject access outside low-level RHI. Owner paths are copied only when a synchronized higher-level caller supplies one.
- Mirrored every complete changed/added file into this repository.

### 2026-09-22 — Fix PS5 uint64 format errors

- The dedicated guard compiled the diagnostic code and exposed PS5 `-Werror,-Wformat` failures.
- On this PS5 toolchain, Unreal `uint64` is `unsigned long`, while `%llu` and `%llx` require `unsigned long long`.
- Added explicit `static_cast<unsigned long long>` conversions for every `%llu` and `%llx` diagnostic argument.
- Fixed both the AddRef/Release assertion text and all provenance failure/event/coverage logging.
- Updated the complete mirrored copies of `RHIResources.h` and `RHIResourceProvenance.cpp`.

### 2026-09-21 — Replace incorrect UE_BUILD_DEVELOPMENT gate

- Confirmed the target had `UE_BUILD_DEVELOPMENT=0`, so the diagnostic blocks were compiled out.
- Added `RHI_RESOURCE_PROVENANCE_ENABLED` through `RHI.Build.cs`.
- Enabled the recorder for `Debug`, `DebugGame`, and `Development`.
- Kept the recorder disabled for `Test` and `Shipping`.
- Replaced every provenance `UE_BUILD_DEVELOPMENT` guard across the six implementation files.
- Added the complete modified `RHI.Build.cs` to this mirror.
- A target rebuild must show `RHI_RESOURCE_PROVENANCE_ENABLED=1` for the diagnostic configuration.

### 2026-09-21 — PS5 shadow-warning compile fix

- Renamed Development diagnostic `ResourceType` parameters in `FRHIResource::FAtomicFlags` to `InResourceType`.
- Renamed the `FRHIResource::MarkForDelete` diagnostic parameter consistently.
- Renamed the `DeleteResources` local token to `ResourceTypeValue`.
- Updated the complete mirrored copies of `RHIResources.h` and `RHIResources.cpp`.
- This resolves PS5 warnings-as-errors such as “declaration shadows a field of FRHIResource” in `Release`, `UnmarkForDelete`, and related methods.

### 2026-09-21 — Initial mirror

- Mirrored all six files changed by the RHI resource-provenance implementation.
- Added bounded identity, lifecycle, AddRef/Release, deletion, and optional command-use instrumentation.
- Established this file as the synchronization record for future changes.

## Synchronization rule

Whenever the diagnostic implementation changes in `d3stryr/UnrealEngine`, update the corresponding complete file in this repository and append a dated entry here. Update the source commit and blob hashes so the mirror can be audited against the engine branch.
