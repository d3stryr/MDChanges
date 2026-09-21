# UE 5.8.2 PS5 RHI Resource Provenance Changes

This repository mirrors complete modified files from `d3stryr/UnrealEngine` for the PS5 Development-build RHI lifetime investigation.

## Source snapshot

- Source repository: [d3stryr/UnrealEngine](https://github.com/d3stryr/UnrealEngine)
- Source branch: [diagnostics/rhi-resource-provenance](https://github.com/d3stryr/UnrealEngine/tree/diagnostics/rhi-resource-provenance)
- Source commit: [47421f8c9285113680e7c98ecd8f1a479fb5849d](https://github.com/d3stryr/UnrealEngine/commit/47421f8c9285113680e7c98ecd8f1a479fb5849d)
- Engine version: 5.8.2
- Initial mirror date: 2026-09-21

The files below are complete snapshots, not patch fragments. Their paths match the Unreal Engine checkout so they can be copied over the engine root.

## Mirrored files

| File | Source blob | Status | Purpose |
|---|---|---|---|
| `Engine/Source/Runtime/RHI/RHI.Build.cs` | `c59a8c678f0c4d162b9568a695c7170356db62bc` | Modified | Defines `RHI_RESOURCE_PROVENANCE_ENABLED` for Debug, DebugGame, and Development; disables it for Test and Shipping. |
| `Engine/Source/Runtime/RHI/Public/RHIResourceProvenance.h` | `1c7492e046d36de5e57117a190073fb3e9ff871b` | Added | Non-production recorder interface, operation types, caller capture, and command correlation API. |
| `Engine/Source/Runtime/RHI/Private/RHIResourceProvenance.cpp` | `b483dbfd579d6ad94f2fdbdb462478360aa557fb` | Added | Bounded per-thread events, retained identity generations, lifecycle history, failure reporting, and optional command-use capture. |
| `Engine/Source/Runtime/RHI/Public/RHIResources.h` | `2c0eba6472270ea85d67787e0f0ca14dd73d4145` | Modified | Instruments AddRef, Release, deletion transitions, generation IDs, and name/owner capture. |
| `Engine/Source/Runtime/RHI/Private/RHIResources.cpp` | `54eda7f84d693f3f6aa57e562ab87086571df865` | Modified | Registers identities, records destruction/delete completion, and copies available resource names. |
| `Engine/Source/Runtime/RHI/Public/RHICommandList.h` | `99c46d15e33b82159689efbb914614970163bf27` | Modified | Adds optional request/command correlation for shader resources, static uniform buffers, and uniform-buffer updates. |
| `Engine/Source/Runtime/RHI/Public/RHICommandListCommandExecutes.inl` | `fe347fa221f615ec0e8f9e0da6f0e9a3c90e8db4` | Modified | Records correlated execution of selected RHI commands. |

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
