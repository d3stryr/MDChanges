# UE 5.8.2 PS5 RHI Resource Provenance Changes

This repository mirrors complete modified files from `d3stryr/UnrealEngine` for the PS5 Development-build RHI lifetime investigation.

## Source snapshot

- Source repository: [d3stryr/UnrealEngine](https://github.com/d3stryr/UnrealEngine)
- Source branch: [diagnostics/rhi-resource-provenance](https://github.com/d3stryr/UnrealEngine/tree/diagnostics/rhi-resource-provenance)
- Source commit: [7c51c9112d99fe04a8605f657adc06f26f84cf32](https://github.com/d3stryr/UnrealEngine/commit/7c51c9112d99fe04a8605f657adc06f26f84cf32)
- Engine version: 5.8.2
- Initial mirror date: 2026-09-21

The files below are complete snapshots, not patch fragments. Their paths match the Unreal Engine checkout so they can be copied over the engine root.

## Mirrored files

| File | Source blob | Status | Purpose |
|---|---|---|---|
| `Engine/Source/Runtime/RHI/Public/RHIResourceProvenance.h` | `889c2d71156c4ac7d2b615f83363fd1dbb9157c6` | Added | Development-only recorder interface, operation types, caller capture, command correlation API. |
| `Engine/Source/Runtime/RHI/Private/RHIResourceProvenance.cpp` | `376423f2374a974e391bd228b44db795fbd771b6` | Added | Bounded per-thread events, retained identity generations, lifecycle history, failure reporting, and optional command-use capture. |
| `Engine/Source/Runtime/RHI/Public/RHIResources.h` | `08c77b2b3f3dabd40a49016719e75530c974c869` | Modified | Instruments AddRef, Release, deletion transitions, generation IDs, and name/owner capture. |
| `Engine/Source/Runtime/RHI/Private/RHIResources.cpp` | `4d90a0a9fe16966908379fcc5d6057107e88c73e` | Modified | Registers identities, records destruction/delete completion, and copies available resource names. |
| `Engine/Source/Runtime/RHI/Public/RHICommandList.h` | `f7f865fd5698f00ed2d4b7ed8fe4830baab73644` | Modified | Adds optional request/command correlation for shader resources, static uniform buffers, and uniform-buffer updates. |
| `Engine/Source/Runtime/RHI/Public/RHICommandListCommandExecutes.inl` | `61b33eaa74ea2b1945cf05fe4194dd28302d2acd` | Modified | Records correlated execution of selected RHI commands. |

## Current behavior

The core recorder is compiled and active only when `UE_BUILD_DEVELOPMENT` is true.

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
- overflow, overwrite, and identity-eviction counters;
- available debug names, owner names, and externally supplied owner paths.

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

A `PhysicalFree` event means the C++ delete expression completed. Memory Insights remains the authoritative source for allocation/free stacks and containing allocation ranges.

## Storage limits

- 128 thread buffers
- 2,048 events per thread
- 32,768 retained identity generations
- 8 lifecycle events per identity
- 256 matching events emitted during failure reporting

These limits intentionally bound memory use. Event overwrites, identity evictions, thread-buffer exhaustion, and omitted matching events are reported.

## Validation status

- Source files were checked against the committed diagnostic branch.
- Preprocessor and delimiter balance checks passed.
- The changes have not yet been compiled with the PS5 SDK/toolchain.
- A full Development PS5 rebuild is required because the Development layout of `FRHIResource` changed.

## Change log

### 2026-09-21 — Initial mirror

- Mirrored all six files changed by the RHI resource-provenance implementation.
- Added bounded identity, lifecycle, AddRef/Release, deletion, and optional command-use instrumentation.
- Established this file as the synchronization record for future changes.

## Synchronization rule

Whenever the diagnostic implementation changes in `d3stryr/UnrealEngine`, update the corresponding complete file in this repository and append a dated entry here. Update the source commit and blob hashes so the mirror can be audited against the engine branch.
