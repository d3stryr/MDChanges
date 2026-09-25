# UE 5.8.2 PS5 RHI Resource Provenance Changes

This repository mirrors complete modified files from `d3stryr/UnrealEngine` for the PS5 Development-build RHI lifetime investigation.

## Source snapshot

- Source repository: [d3stryr/UnrealEngine](https://github.com/d3stryr/UnrealEngine)
- Source branch: [diagnostics/rhi-resource-provenance](https://github.com/d3stryr/UnrealEngine/tree/diagnostics/rhi-resource-provenance)
- Source commit: [47ed4de8bd2c220a18ae218627d33e6cdf5a59b0](https://github.com/d3stryr/UnrealEngine/commit/47ed4de8bd2c220a18ae218627d33e6cdf5a59b0)
- Engine version: 5.8.2
- Initial mirror date: 2026-09-21

The files below are complete snapshots, not patch fragments. Their paths match the Unreal Engine checkout so they can be copied over the engine root.

## Mirrored files

| File | Source blob | Status | Purpose |
|---|---|---|---|
| `Engine/Source/Runtime/RHI/RHI.Build.cs` | `c59a8c678f0c4d162b9568a695c7170356db62bc` | Modified | Defines `RHI_RESOURCE_PROVENANCE_ENABLED` for Debug, DebugGame, and Development; disables it for Test and Shipping. |
| `Engine/Source/Runtime/RHI/Public/RHIResourceProvenance.h` | `7cadd1e7f9a116986fa587d85fd206c05c5a6c7a` | Added | Non-production recorder interface, operation types, caller capture, metadata identity fields, owner tokens, command correlation, cached-binding lineage, and causal/ownership timelines. |
| `Engine/Source/Runtime/RHI/Private/RHIResourceProvenance.cpp` | `a0de5fda09dd2425d0246747a4519a961faba2d1` | Added | Bounded recorder, retained identities, journal, access/release owners, deduplicated generation-safe bindings, GC state, and scene-map timelines. |
| `Engine/Source/Runtime/RHI/Public/RHIResources.h` | `afdffde9b4a8f8caab09b22cd8709b3162f424fe` | Modified | Instruments AddRef, Release, deletion transitions, generation IDs, name/owner capture, and compact typed provenance events. |
| `Engine/Source/Runtime/RHI/Private/RHIResources.cpp` | `54eda7f84d693f3f6aa57e562ab87086571df865` | Modified | Registers identities, records destruction/delete completion, and copies available resource names. |
| `Engine/Source/Runtime/RHI/Public/RHICommandList.h` | `99c46d15e33b82159689efbb914614970163bf27` | Modified | Adds optional request/command correlation for shader resources, static uniform buffers, and uniform-buffer updates. |
| `Engine/Source/Runtime/RHI/Public/RHICommandListCommandExecutes.inl` | `fe347fa221f615ec0e8f9e0da6f0e9a3c90e8db4` | Modified | Records correlated execution of selected RHI commands. |
| `Engine/Build/BatchFiles/DecodeRHIResourceProvenance.py` | `d5aa6608df9e65bd2dc4ba60d3079eb967d57162` | Added | Filters journals and emits TSV with owners, command/binding IDs, causal IDs, GC state, and scene-refresh correlations. |
| `Engine/Source/Runtime/Engine/Public/ParameterCollection.h` | `1e2515778c4202ca4b4738ffe9c6be0936687265` | Modified | Adds path/release/fixed-event APIs and carries diagnostic causal IDs into render-thread MPC updates. |
| `Engine/Source/Runtime/Engine/Private/Materials/ParameterCollection.cpp` | `f2b355b96c6cebe6e95a8138a7001589cc507b07` | Modified | Captures MPC paths/release causes and queues game/render/RHI causal and GC-state events. |
| `Engine/Source/Runtime/Engine/Private/World.cpp` | `3b87986a1f1f0a1bdbac07ec484ad5edc550761f` | Modified | Records bounded pre/post-GC MPC state and the exact invalid-collection removal transition. |
| `Engine/Source/Runtime/Engine/Classes/Engine/World.h` | `866f41cd9c1bf4009d4503b60bee347f79cc913d` | Modified | Registers a diagnostic pre-GC callback so MPC retention state can be compared before and after collection. |
| `Engine/Source/Runtime/Renderer/Private/ShaderBaseClasses.cpp` | `10d0b06e5e633c5500a59e13f1e8ff31754704fc` | Modified | Captures material, render proxy, primitive owner/resource/level labels and writes owner plus exact MPC generation into cached bindings. |
| `Engine/Source/Runtime/Renderer/Public/MaterialShader.h` | `73a09ee6afb8a11792b8ccd8f0517707fa497c4b` | Modified | Carries optional mesh context into diagnostic MPC binding-owner capture without changing existing callers. |
| `Engine/Source/Runtime/Renderer/Private/RendererScene.cpp` | `0a2fd36426dea91fee617997a69a9e62e48f1934` | Modified | Audits exact scene MPC-map inserts, removals, and old/new replacement generations without labeling unchanged entries. |
| `Engine/Source/Runtime/Renderer/Public/MeshDrawShaderBindings.h` | `e42e2b6963f6727186579350538adebe13b0df87` | Modified | Carries copied owner and exact resource-generation sidecars beside each diagnostic uniform-buffer binding. |
| `Engine/Source/Runtime/Renderer/Public/MeshPassProcessor.h` | `695e5b6044e4c65ab2665332b8fe869d1a60a33f` | Modified | Adds a diagnostic binding-lineage ID and explicit cached-command invalidation entry point. |
| `Engine/Source/Runtime/Renderer/Private/MeshPassProcessor.cpp` | `a788020f2cbf96fb9ece451a95bc9eddd337c1f4` | Modified | Records targeted binding create/copy/move/submit/invalidate/release events using retained generation IDs. |
| `Engine/Source/Runtime/Renderer/Private/PrimitiveSceneInfo.cpp` | `2f0e9d43ad49d06950e4a514c4ea30550772137e` | Modified | Records actual cached draw-command removal before state-bucket or sparse draw-list storage is destroyed. |

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
- bounded, deduplicated MPC access-owner labels containing material path, render proxy, collection GUID, and immediate/cached binding source;
- a compact material-owner token stored beside cached mesh uniform-buffer pointers, copied through existing mesh-binding copies/moves, and linked to the exact deferred-command correlation ID;
- a unique cached-binding lineage ID plus `BindingCreate`, `BindingCopy`, `BindingMove`, `BindingSubmit`, `BindingInvalidate`, `BindingRelease`, and `BindingOwner` journal rows for tracked MPC generations;
- the exact resource generation ID copied beside each tracked raw uniform-buffer pointer, so binding events remain unambiguous after address reuse and do not need to dereference a dead resource;
- a process-unique MPC update causal ID linking the game-thread request, render-thread execution, in-place RHI update command, or newly recreated uniform-buffer generation;
- pre/post-GC MPC state rows that show whether the weak collection survived, its root/object flags while valid, and whether the owning world is partitioned, a game world, or itself a runtime-cell world;
- invalid MPC collections are prioritized within a bounded per-world GC capture budget, and `GCCoverageOmitted` reports exactly how many instances were not sampled;
- exact scene MPC-map mutation rows (`SceneMapInsert`, `SceneMapRemove`, `SceneMapReplaceOld`, and `SceneMapReplaceNew`) linked by a refresh ID; unchanged GUID/resource pairs do not generate false release markers;
- primitive owner, primitive resource, and primitive level copied into cached MPC access-owner labels; `primitive_level` is the first World Partition cell clue, while explicit runtime Data Layer membership is intentionally deferred to the bounded contributor feature;
- assertion-time `command_owner_key` and copied `command_owner` output, with explicit label-eviction, link-overwrite, link-miss, staging-overflow, and owner-set saturation counters;
- explicit MPC release-owner labels distinguishing world post-GC removal, world instance replacement, UObject destruction, scene-map refresh, and uniform-buffer replacement;
- Material Parameter Collection uniform-buffer generations receive labeled `Collection=`, `Instance=`, and `World=` path records copied once on the game thread and cached on the render resource; the collection path is retained in the bounded failure-time identity;
- a bounded append-only binary journal written by a below-normal-priority background thread.

Selected command-use tracing is disabled by default. Enable it with:

```ini
r.RHI.ResourceProvenance.CommandUses=1
```

It currently covers shader resource/bindless parameters, static uniform-buffer binding, and uniform-buffer update dispatch. When enabled, `CommandEnqueue`, `CommandExecute`, `UpdateRequest`, and `UpdateExecute` records are persisted to the bounded journal with their correlation IDs. It does not cover every raw-pointer read, every RHI operation, or GPU execution.

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
python Engine/Build/BatchFiles/DecodeRHIResourceProvenance.py <journal.rhiprov> --causal 42
python Engine/Build/BatchFiles/DecodeRHIResourceProvenance.py <journal.rhiprov> --scene-refresh 57
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
- up to 64 deduplicated access-owner identities per retained priority generation; the four most recent labels are retained for crash-time output and accepted labels are journaled
- 8,192 copied access-owner labels and 32,768 command-owner correlation links; both are fixed-capacity and report eviction/overwrite/miss coverage
- 65,536 fixed binding-submit dedup slots; normal submissions persist once per binding/resource pair, collisions are counted, and submissions observed after a stale lifecycle state are never suppressed
- 16 staged owner tokens per producer thread; overflow is counted, and cached mesh binding data carries two diagnostic `uint64` values per uniform-buffer slot (owner key and exact resource generation ID)
- four recent release-owner labels per retained priority generation
- 256 KiB background write buffer
- 10,240 MiB (10 GiB) default journal cap, configurable before journal startup with `r.RHI.ResourceProvenance.JournalMaxMB`; accepted range is 16–16,384 MiB
- 256 matching events emitted during failure reporting
- 256 MPC instance resources per world per GC phase by default, configurable from 1–4,096 with `r.RHI.ResourceProvenance.MaxMPCGCInstancesPerWorld`; omitted coverage is journaled

These limits intentionally bound memory and disk use. Event overwrites, active-table overflow, destroyed-history eviction, journal queue drops, disk-cap drops, thread-buffer exhaustion, and omitted matching events are reported. Normal AddRef/Release events remain in memory; creation, identity metadata, decisive lifecycle operations, and command-use operations when enabled are persisted.

## Validation status

- Source files were checked against committed diagnostic branch `47ed4de8bd2c220a18ae218627d33e6cdf5a59b0`.
- Preprocessor and delimiter balance checks passed.
- The first recorder revision compiled and executed on PS5, captured the `0xDD` failure, and recovered a prior valid type-18 generation at the same resource/flags addresses.
- The active/destroyed identity and persistent-journal revision has passed source/mirror equality, delimiter/preprocessor balance, and Python decoder syntax checks, but has not yet been compiled with the PS5 SDK/toolchain.
- The MPC owner-association revision passed source delimiter/preprocessor checks. Its two complete files are mirrored, but the revision has not yet been compiled with the PS5 SDK/toolchain.
- The command-journal revision has exact source/mirror blob equality for the recorder and decoder; it has not yet been compiled or exercised on PS5.
- Features 3 and 4 passed preprocessor-balance, Python syntax, operation-wiring, source-blob, and synthetic GC/scene journal decode checks. They have not been compiled with the PS5 SDK/toolchain.
- A full diagnostic PS5 rebuild is required because the instrumented `FRHIResource` layout and RHI module implementation changed.

## Change log

### 2026-09-25 — Feature 4: exact scene MPC-map mutation audit

- Replaced the blanket `SceneParameterCollectionMapRefresh` release marker, which labeled every prior entry even when the same RHI generation was immediately reinserted.
- Compare collection GUID and uniform-buffer pointer before clearing the render-scene map. Emit only real `SceneMapInsert`, `SceneMapRemove`, `SceneMapReplaceOld`, and `SceneMapReplaceNew` transitions.
- Give every `FScene::UpdateParameterCollections` request an always-on correlation ID. Old/new sides of one replacement carry the same ID, so the decoder can reconstruct the handoff with `--scene-refresh <id>`.
- Pack the entry index and collection count into the event payload. The decoder adds `scene_refresh_id` and human-readable `detail` columns, and decodes Feature 3 GC state flags in the same column.
- Record scene removal and replacement-old as retained lifecycle transitions. This makes assertion-time identity output name the scene ownership transition even if per-thread rings have wrapped.
- Use fixed event records and existing input/map storage: no path formatting, stack walk, extra owning RHI reference, or diagnostic container allocation is added to the render command. Comparison is intentionally linear over the normally small MPC list.
- Corrected `CausalLink` TSV decoding so the parent MPC causal ID comes from the stored caller field and the child RHI command correlation comes from the correlation field.
- Python syntax, source/mirror equality, delimiter/preprocessor balance, and PS5 compilation still need final validation below.

### 2026-09-25 — Feature 3: bounded pre/post-GC MPC retention audit

- Register `UWorld::OnPreGC` beside the existing post-GC callback and record `GCPreSnapshot`, `GCPostSurvived`, or `GCPostCollected` against the exact current MPC uniform-buffer generation.
- Pack collection validity, root status, `RF_Standalone`, `RF_Public`, `RF_Transient`, begin/finish-destroyed flags, instance root/destroy state, partitioned-world state, runtime-cell-world state, and game-world state into a fixed `uint32` event payload.
- Preserve the already copied `Collection=`, `Instance=`, and `World=` paths as the identity source; neither GC callback formats a path nor dereferences a UObject from the render/RHI thread.
- Limit each world to 256 recorded MPC resources per GC phase by default. Post-GC capture prioritizes invalid collections, and a fixed `GCCoverageOmitted` record reports the remaining count instead of silently losing coverage.
- Promote `GCPostCollected` into retained lifecycle history so assertion-time output can show the ownership-loss transition even before the persistent journal is decoded.
- This identifies GC as the collection transition and reports observable root/object state; it does not claim to enumerate arbitrary UObject strong referencers. The next contributor feature will add synchronized higher-level owners, including World Partition/Data Layer context.
- Source/preprocessor validation is pending completion of Feature 4 in the same diagnostic build; no PS5 compile has been run here.

### 2026-09-25 — Feature 2: cross-thread MPC causal timeline

- Allocate a causal ID only when `r.RHI.ResourceProvenance.CommandUses=1`; the disabled path returns zero without journaling.
- Record `CausalRequest` on the game thread before `UpdateCollectionCommand` is queued and `CausalExecute` when that render command begins. `packed=1` means the request asked to recreate the uniform buffer; `packed=0` means an in-place update was requested.
- Stage the causal ID only around the matching in-place `UpdateUniformBuffer` call. `BeginCommandUse` emits `CausalLink` with the parent MPC causal ID and the child RHI command correlation, then the previous thread-local parent is restored.
- For a recreated buffer, emit `CausalResource` after owner paths promote the new RHI generation. This directly links the new generation ID to the original MPC request.
- Add TSV columns `causal_id` and `parent_correlation`, plus `--causal <id>` filtering to recover request/execute/resource rows that intentionally have no RHI resource ID on the game-thread side.
- Causal records are fixed-size journal events; there are no per-update strings, allocations, stack walks, or file writes on gameplay/render threads.
- Python syntax, whitespace, preprocessor-balance, source checks, and a synthetic `CausalRequest`/`CausalLink` journal decode passed. This revision has not been compiled with the PS5 SDK/toolchain.

### 2026-09-25 — Feature 1: cached draw-command binding lifetime audit

- Added a process-unique binding lineage ID only to binding sets containing an MPC generation already promoted into retained provenance storage.
- Copied the exact RHI generation ID beside the raw uniform-buffer pointer and owner key. No extra owning RHI reference is added, and lifecycle reporting never dereferences the resource pointer.
- Added journal operations for binding creation, copy, move, submission, explicit cache invalidation, release, and binding-to-owner association. The decoder emits a dedicated `binding_id` column.
- Bound normal submission volume with a fixed 65,536-slot lock-free dedup table. The first submit for a binding/resource pair is journaled; repeats are counted but suppressed. Once the retained resource state is stale, submissions are always journaled. First/stale/deduplicated/collision totals are printed in failure coverage.
- Instrumented `FPrimitiveSceneInfo::RemoveCachedMeshDrawCommands` at the actual state-bucket and sparse draw-list removal points. A shared state-bucket binding is invalidated only when its reference count reaches zero and the cached command is actually erased.
- Extended cached MPC access-owner text with render-side primitive owner, primitive resource, and level names. In a World Partition build, the level name helps identify the runtime-cell context without touching a UObject on the render thread.
- Explicit Data Layer membership is not guessed from level names. It will be copied at a synchronized higher-level association point in the bounded actor/component contributor feature.
- Dynamic-instancing equality and hashing still ignore diagnostic owner/generation sidecars and the lineage ID.
- Python syntax, whitespace, source/mirror blob equality, and targeted operation-mapping checks passed. This revision has not been compiled with the PS5 SDK/toolchain.

### 2026-09-24 — Capture MPC access owners and release owners

- The GameInstance hard reference to `MPC_GlobalEnvironment` stopped the crash, strongly indicating that unloading the collection asset allowed its per-world instance/resource to be removed while a stale render-side reference survived.
- Added `AccessOwner` and `ReleaseOwner` operations without changing the version-1 journal record layout.
- Added `CommandOwner` without changing the version-1 journal layout. `AccessOwner` and `CommandOwner` rows expose the same `owner_key`, while `CommandOwner` also carries the deferred command correlation.
- MPC material access capture runs only when `r.RHI.ResourceProvenance.CommandUses=1` and only for already promoted owner-associated resources.
- Access owners are deduplicated by material render proxy and access source. Each retained generation accepts at most 64 unique material owners, journals each accepted label, keeps the four most recent labels for assertion-time output, and reports saturation explicitly.
- Cached mesh bindings store a diagnostic `uint64` owner token beside each uniform-buffer pointer. Existing binding copy/move operations carry both together; command submission stages the token and the RHI recorder links it to `CommandEnqueue`/`CommandExecute` correlation.
- Dynamic-instancing equality and hashes intentionally ignore the diagnostic token to avoid changing batching and draw counts. If UE merges equivalent draw commands, the exact command token is the retained representative material owner; all accepted material-owner candidates remain in the journal.
- Added release-owner markers for `UWorld::OnPostGC` invalid-collection removal, `UWorld::CreateParameterCollectionInstance` replacement, MPC asset/instance destruction, scene parameter-map refresh, and MPC uniform-buffer replacement.
- Higher-level strings are copied while valid. Render/RHI failure reporting reads only independent retained storage and does not dereference a freed resource or UObject.
- Added decoder `--contains` filtering so `MPC_GlobalEnvironment` can be found by text first; use the resulting generation ID for a complete `--id` decode.
- Added complete mirrored copies of `World.cpp`, `ShaderBaseClasses.cpp`, `MaterialShader.h`, `MeshPassProcessor.h`, and `MeshPassProcessor.cpp`.
- Local diff, Python syntax, decoder-format, and delimiter validation passed; this revision has not yet been compiled with the PS5 SDK/toolchain.

### 2026-09-22 — Persist correlated command-use records

- The successful PS5 failure dump proved that `r.RHI.ResourceProvenance.CommandUses=1` was active: the bounded per-thread rings contained two `CommandEnqueue` records and one `CommandExecute` record with correlation `438168269141643`.
- The decoded TSV did not contain those records because the journal predicate persisted only identity and decisive lifecycle operations. This was a recorder coverage gap, not a binary-layout or TSV-decoder corruption.
- Added a distinct `CommandUse` journal record kind.
- Persist `CommandEnqueue`, `CommandExecute`, `UpdateRequest`, and `UpdateExecute` records with their command correlation IDs.
- Command records are generated only when command-use tracing is enabled; normal `AddRef` and `Release` events remain ring-only.
- Added decoder support for the new record kind without changing the version-1 disk record layout.
- Disk and memory use remain bounded by the existing queue, journal cap, loss counters, and per-thread buffers.
- Updated the complete mirrored recorder and decoder files.

### 2026-09-22 — Remove per-update MPC path-copy overhead

- Replaced the first owner-association implementation before PS5 validation because it would have copied three `FString` paths through every MPC update command.
- Added a diagnostic-only `GameThread_SetProvenancePaths` command that publishes the copied paths once and caches them on the render resource.
- `SetCollection` publishes per-world instance paths once; default resources republish only when their uniform buffer is recreated.
- Restored the original `GameThread_UpdateContents` and `UpdateContents` signatures and command payload for ordinary MPC parameter updates.
- New uniform-buffer generations reuse the cached paths, so no UObject is dereferenced and no path is formatted during render/RHI destruction.
- Updated both complete mirrored MPC files.

### 2026-09-22 — Associate MPC uniform buffers with UObject paths

- Identified the failed generation as `MaterialParameterCollectionInstanceResource` and the stale access as a uniform-buffer bind during deferred `FRHICommandSetShaderParameters` execution.
- At `UMaterialParameterCollectionInstance::SetCollection`, copy `Collection->GetPathName()`, the instance `GetPathName()`, and `World->GetPathName()` while those UObjects are valid on the game thread. Default resources publish their paths when their uniform buffer is recreated.
- Carry the copied strings once through a dedicated ordered render command and cache them on `FMaterialParameterCollectionInstanceResource`; render/RHI code never dereferences a UObject to obtain provenance.
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

## 2026-09-22: retained identity, selective stacks, stale-command origin, and MPC release reasons

### Why this change was needed

The original crash-time identity table could report `identity miss` after its active table overflowed or its destroyed-history ring evicted the matching generation. The persistent journal still recovered the resource offline, but the assertion log could not print the name, owner, or lifecycle immediately. A single caller PC also could not distinguish the resource creator, final releaser, cached-binding producer, command producer, and invalid AddRef consumer.

### Recorder changes

- Added a separate fixed-capacity table of 4096 retained priority identities. A resource is promoted when a higher-level owner path or explicit diagnostic release marker is supplied. The table does not own or AddRef the RHI resource.
- Each priority identity retains the four most recent owner-path associations independently, so the crash log can print the MPC instance, world, and collection paths instead of only the final path. Overflow is explicit in the per-path retained/total counts.
- Added a 32768-entry bounded address-to-generation index. A new construction at the same address invalidates the old current-address mapping while all retained generations remain available for failure-time ambiguity reporting.
- Added explicit counters for priority identity overflow, address-index overflow, selective stack capture count, and stack capture drops.
- Added 16-PC raw stack captures for owner association, final reference release, completion of the C++ delete expression (`PhysicalFree`), first cached mesh-binding store, and the most recent bounded stale command enqueue/execute observations.
- Stale command stack walks are capped at 256 for the run. Once exhausted, the recorder reports drops rather than continuing unbounded work.
- `ReportInvalidAtomic` now captures and journals the complete invalid-use CPU stack, recovers the retained generation without dereferencing the dead object, and attaches the last command-execute correlation seen for that resource on the failing thread.
- Command-use rows now receive the retained resource ID, flags address, and type when the independent address index can resolve them. Raw pointers that were never promoted still correctly remain ID 0/type 255.
- Added journal record kinds `StackFrame` and `Marker`, and operations `OwnerAssociation`, `ReleaseReason`, `BindingStore`, and `InvalidUse`. The decoder maps all of them. `packed` is the zero-based frame index for `StackFrame` rows.
- Added `r.RHI.ResourceProvenance.PriorityStacks` (default 1). This diagnostic option enables the selective raw-PC captures; exact-build symbols are still required after capture.
- The failure journal summary now prints the sampled `command_uses` and `priority_stacks` values, removing ambiguity about whether Rider launch arguments actually enabled the two diagnostic paths.

### Higher-level MPC and cached-binding instrumentation

- `FMeshDrawSingleShaderBindings::Add(..., const FRHIUniformBuffer*)` reports a compact `BindingStore` only when the resource is in the retained priority table. The first such store also gets a stack. This identifies the code path that copied an owner-associated uniform-buffer raw pointer into cached mesh draw bindings.
- `FMaterialParameterCollectionInstanceResource::UpdateContents` records a release reason before replacing an existing uniform buffer.
- `FMaterialParameterCollectionInstanceResource::GameThread_Destroy` records a release reason before `SafeRelease`.
- `FScene::UpdateParameterCollections` records a release reason for owning scene-map references immediately before the map is emptied.
- These markers are emitted while a valid owning reference still exists. They never call `GetPathName` or dereference the RHI resource after final release.

### Files modified in this revision

- `Engine/Source/Runtime/RHI/Public/RHIResourceProvenance.h`
- `Engine/Source/Runtime/RHI/Private/RHIResourceProvenance.cpp`
- `Engine/Source/Runtime/RHI/Public/RHIResources.h`
- `Engine/Source/Runtime/Renderer/Public/MeshDrawShaderBindings.h`
- `Engine/Source/Runtime/Engine/Private/Materials/ParameterCollection.cpp`
- `Engine/Source/Runtime/Renderer/Private/RendererScene.cpp`
- `Engine/Build/BatchFiles/DecodeRHIResourceProvenance.py`

### Interpretation and current engine hypothesis

The recovered object is the per-world `MaterialParameterCollectionInstanceResource` uniform buffer for `MPC_GlobalEnvironment`. UE 5.8.2's scene parameter-collection map owns `FUniformBufferRHIRef` references, but `FMeshDrawShaderBindings` stores plain `FRHIUniformBuffer*` values. The MPC update/destroy paths wait for asynchronous RDG execution before replacing or releasing the buffer, but that wait does not itself prove that persistent cached mesh draw bindings were rebuilt or invalidated. The current leading engine-level hypothesis is therefore a stale raw uniform-buffer pointer retained in cached mesh draw bindings across MPC buffer replacement, instance destruction, or scene-map refresh. It is not yet proven; the new `BindingStore`, release-reason, command-correlation, and stack records are intended to distinguish that path from project code, custom renderer code, world teardown, or an unrelated overwrite.

Do not treat `PhysicalFree` as an allocator-level free stack. It is recorded after the C++ `delete Resource` expression completes. Use a same-run Memory Insights trace from process startup to obtain the underlying CPU allocation and free callstacks and match the pointer against containing allocation ranges and historical generations.

### Run settings for the next repro

Keep the existing trace host arguments and add:

`-ExecCmds="r.RHI.ResourceProvenance.CommandUses 1,r.RHI.ResourceProvenance.PriorityStacks 1,r.RHI.ResourceProvenance.Journal 1,r.RHI.ResourceProvenance.JournalMaxMB 10240" -trace=default,memory,module,metadata,assetmetadata,log`

The memory trace must be active at process startup. After the crash, decode by the recovered resource address and inspect `Marker`, `BindingStore`, `CommandUse`, and `StackFrame` rows. Correlate `CommandEnqueue`, `CommandExecute`, and `InvalidUse` by `correlation`; resolve each `caller` PC using symbols from that exact executable build.
