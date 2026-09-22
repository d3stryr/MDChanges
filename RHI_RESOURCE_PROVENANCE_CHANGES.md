# UE 5.8.2 PS5 RHI Resource Provenance Changes

This repository mirrors complete modified files from `d3stryr/UnrealEngine` for the PS5 Development-build RHI lifetime investigation.

## Source snapshot

- Source repository: [d3stryr/UnrealEngine](https://github.com/d3stryr/UnrealEngine)
- Source branch: [diagnostics/rhi-resource-provenance](https://github.com/d3stryr/UnrealEngine/tree/diagnostics/rhi-resource-provenance)
- Source commit: [8736eedc0f700a917059968c1b69c63045034340](https://github.com/d3stryr/UnrealEngine/commit/8736eedc0f700a917059968c1b69c63045034340)
- Engine version: 5.8.2
- Initial mirror date: 2026-09-21

The files below are complete snapshots, not patch fragments. Their paths match the Unreal Engine checkout so they can be copied over the engine root.

## Mirrored files

| File | Source blob | Status | Purpose |
|---|---|---|---|
| `Engine/Source/Runtime/RHI/RHI.Build.cs` | `c59a8c678f0c4d162b9568a695c7170356db62bc` | Modified | Defines `RHI_RESOURCE_PROVENANCE_ENABLED` for Debug, DebugGame, and Development; disables it for Test and Shipping. |
| `Engine/Source/Runtime/RHI/Public/RHIResourceProvenance.h` | `4573061c1a0c08d2d4a3b6c4acd09ddf97109e2f` | Added | Non-production recorder interface, operation types, caller capture, metadata identity fields, and command correlation API. |
| `Engine/Source/Runtime/RHI/Private/RHIResourceProvenance.cpp` | `b1245f973ab0421220c383ad3fe2cdc5a9fbc4e2` | Added | Bounded per-thread events, active/destroyed identity retention, background binary journal, lifecycle history, failure reporting, and optional command-use capture. |
| `Engine/Source/Runtime/RHI/Public/RHIResources.h` | `d8887be077f6050b54a3db0280127f0778e8e3ed` | Modified | Instruments AddRef, Release, deletion transitions, generation IDs, and name/owner capture with resource/flags identity fields. |
| `Engine/Source/Runtime/RHI/Private/RHIResources.cpp` | `54eda7f84d693f3f6aa57e562ab87086571df865` | Modified | Registers identities, records destruction/delete completion, and copies available resource names. |
| `Engine/Source/Runtime/RHI/Public/RHICommandList.h` | `99c46d15e33b82159689efbb914614970163bf27` | Modified | Adds optional request/command correlation for shader resources, static uniform buffers, and uniform-buffer updates. |
| `Engine/Source/Runtime/RHI/Public/RHICommandListCommandExecutes.inl` | `fe347fa221f615ec0e8f9e0da6f0e9a3c90e8db4` | Modified | Records correlated execution of selected RHI commands. |
| `Engine/Build/BatchFiles/DecodeRHIResourceProvenance.py` | `f5aa614302a7a67b93f3921a90006c3e58db1330` | Added | Streams and filters `.rhiprov` journals by generation ID, resource address, or flags address and emits readable TSV. |
| `Engine/Source/Runtime/Engine/Public/ParameterCollection.h` | `1ed7eec251a8e57a4b0ce4afd9ed2a1da7cfd649` | Modified | Carries copied collection, transient instance, and world paths through diagnostic MPC update commands. |
| `Engine/Source/Runtime/Engine/Private/Materials/ParameterCollection.cpp` | `59ff54bdbe15d4a686275d24c546ac8e3c3da41a` | Modified | Captures UObject paths on the game thread and associates them with each newly created MPC uniform-buffer generation. |

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
- Material Parameter Collection uniform-buffer generations receive labeled `Collection=`, `Instance=`, and `World=` path records copied on the game thread; the collection path is retained in the bounded failure-time identity;
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

For an instrumented Material Parameter Collection generation, the decoded TSV contains three labeled `OwnerPath` records (`Instance=`, `World=`, and `Collection=`). The collection record is written last so `owner_path` in the bounded crash-time identity resolves to the asset path when it fits the retained path capacity.

A `PhysicalFree` event means the C++ delete expression completed. Memory Insights remains the authoritative source for allocation/free stacks and containing allocation ranges.

## Storage limits

- 128 thread buffers
- 2,048 events per thread
- 32,768 preallocated active identity slots; live identities are not displaced by newer IDs
- 32,768 recently destroyed identity generations
- 8 lifecycle events per active/destroyed identity
- 16,384 preallocated journal queue records
- 256 KiB background write buffer
- 10,240 MiB (10 GiB) default journal cap, configurable before journal startup with `r.RHI.ResourceProvenance.JournalMaxMB`; accepted range is 16–16,384 MiB
- 256 matching events emitted during failure reporting

These limits intentionally bound memory and disk use. Event overwrites, active-table overflow, destroyed-history eviction, journal queue drops, disk-cap drops, thread-buffer exhaustion, and omitted matching events are reported. Normal AddRef/Release events remain in memory; creation, identity metadata, and decisive lifecycle operations are persisted.

## Validation status

- Source files were checked against the committed diagnostic branch.
- Preprocessor and delimiter balance checks passed.
- The first recorder revision compiled and executed on PS5, captured the `0xDD` failure, and recovered a prior valid type-18 generation at the same resource/flags addresses.
- The active/destroyed identity and persistent-journal revision has passed source/mirror equality, delimiter/preprocessor balance, and Python decoder syntax checks, but has not yet been compiled with the PS5 SDK/toolchain.
- The MPC owner-association revision passed source delimiter/preprocessor checks. Its two complete files are mirrored, but the revision has not yet been compiled with the PS5 SDK/toolchain.
- A full diagnostic PS5 rebuild is required because the instrumented `FRHIResource` layout and RHI module implementation changed.

## Change log

### 2026-09-22 — Associate MPC uniform buffers with UObject paths

- Identified the failed generation as `MaterialParameterCollectionInstanceResource` and the stale access as a uniform-buffer bind during deferred `FRHICommandSetShaderParameters` execution.
- At `UMaterialParameterCollectionInstance::DeferredUpdateRenderState`, copy `Collection->GetPathName()`, the instance `GetPathName()`, and `World->GetPathName()` while those UObjects are valid on the game thread.
- Carry the copied strings by value through `UpdateCollectionCommand`; render/RHI code never dereferences a UObject to obtain provenance.
- Associate the three labeled paths only when a new or recreated `FUniformBufferRHIRef` generation is created, avoiding metadata work on ordinary in-place MPC updates.
- Journal paths in `Instance=`, `World=`, `Collection=` order. The collection is stored last so the bounded destroyed identity reports the actionable asset path, while the persistent journal retains all three labeled records.
- Cover default collection resources with `Instance=<default-resource>` and `World=<none>`.
- Guard the additional parameters and work with `RHI_RESOURCE_PROVENANCE_ENABLED`, leaving Test and Shipping signatures/behavior unchanged.
- Added complete mirrored copies of `ParameterCollection.h` and `ParameterCollection.cpp`.

### 2026-09-22 — Raise persistent journal cap to 10 GiB

- The captured PS5 run reached the previous 1,024 MiB cap at approximately 282.57 seconds and reported 19,803,907 disk-cap drops, so the journal did not retain command-use records through the later crash.
- Raised the default `r.RHI.ResourceProvenance.JournalMaxMB` value from 1,024 MiB to 10,240 MiB.
- Raised the accepted upper bound from 4,096 MiB to 16,384 MiB so the 10 GiB default and explicit overrides are not clamped back to 4 GiB.
- Retained the existing `uint64` byte calculation (`static_cast<uint64>(MaximumMiB) * 1024ull * 1024ull`) to avoid 32-bit overflow.
- This changes bounded disk usage only; the queue, per-thread buffers, and in-memory identity capacities are unchanged.
- Updated the complete mirrored `RHIResourceProvenance.cpp`.

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
