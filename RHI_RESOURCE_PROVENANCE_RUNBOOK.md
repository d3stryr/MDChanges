# PS5 UE 5.8.2 RHI Resource Provenance Runbook

This runbook describes how to launch a Development PS5 build with the MPC/RHI provenance instrumentation, recover the journal after a crash, filter the recorded evidence, and produce the automated responsibility report.

## Preconditions

- Build configuration: `Development | PS5`.
- Engine branch: `diagnostics/rhi-resource-provenance`.
- Use the executable and symbols produced by the same build as the captured journal.
- Ensure the devkit has room for the journal and Unreal Insights trace. The journal is capped at 10 GiB with the recommended settings.
- The instrumentation is compiled out of Test and Shipping.

## PS5 Development symbols

Build the exact executable used for the repro with full debug information and a linker map. In the game target constructor, keep this diagnostic-only block:

```csharp
if (Target.Platform == UnrealTargetPlatform.PS5 &&
    Target.Configuration == UnrealTargetConfiguration.Development)
{
    bForceDebugInfo = true;
    DebugInfo = DebugInfoMode.Full;
    bDisableDebugInfoForGeneratedCode = false;
    bCreateMapFile = true;
    bAllowRuntimeSymbolFiles = true;
    bPublicSymbolsByDefault = true;
}
```

Equivalent UBT switches for a one-off build are:

```text
-ForceDebugInfo -DebugInfo=Full -MapFile -PublicSymbolsByDefault
```

`bUsePDBFiles` is a Visual C++ setting and is not the PS5 symbol switch. Preserve the exact staged `eboot.bin`, linker map, Prospero symbol/debug artifacts, packaged build manifest, and build identifier together. Load that exact executable and its matching symbols in the Prospero debugger/Rider before resolving `caller` and `StackFrame` PCs. Never symbolize a capture with artifacts from a later incremental build.

## Recommended launch parameters

Paste the following into Project Launcher's **Additional Command Line Parameters** field:

```text
-ExecCmds="r.RHI.ResourceProvenance.JournalMaxMB 10240,r.RHI.ResourceProvenance.CommandUses 1,r.RHI.ResourceProvenance.PriorityStacks 1,r.RHI.ResourceProvenance.Contributors 1,r.RHI.ResourceProvenance.MPCReferenceChains 1,r.RHI.ResourceProvenance.MPCTarget MPC_GlobalEnvironment,r.RHI.ResourceProvenance.MaxMPCReferenceChainCaptures 4,r.RHI.ResourceProvenance.Journal 1" -trace=default,memory,module,metadata,assetmetadata,log
```

If a live Unreal Insights trace host is already used, retain it:

```text
-tracehost=<YOUR_PC_IP>
```

Complete example:

```text
-ExecCmds="r.RHI.ResourceProvenance.JournalMaxMB 10240,r.RHI.ResourceProvenance.CommandUses 1,r.RHI.ResourceProvenance.PriorityStacks 1,r.RHI.ResourceProvenance.Contributors 1,r.RHI.ResourceProvenance.MPCReferenceChains 1,r.RHI.ResourceProvenance.MPCTarget MPC_GlobalEnvironment,r.RHI.ResourceProvenance.MaxMPCReferenceChainCaptures 4,r.RHI.ResourceProvenance.Journal 1" -trace=default,memory,module,metadata,assetmetadata,log -tracehost=192.168.x.x
```

### What these settings enable

| Setting | Purpose |
|---|---|
| `Journal 1` | Writes the persistent binary `.rhiprov` journal. |
| `JournalMaxMB 10240` | Allows up to 10 GiB so a five-to-six-minute reproduction is not cut off by the disk cap. |
| `CommandUses 1` | Captures selected enqueue/execute/update operations needed to prove stale command use. |
| `PriorityStacks 1` | Captures bounded raw call stacks at important ownership and stale-use points. |
| `Contributors 1` | Records actor, component, runtime-cell, and Data Layer contributors. Requires `CommandUses=1`. |
| `MPCReferenceChains 1` | Captures bounded healthy/suspicious UObject reference-chain snapshots for the target MPC. |
| `MPCTarget MPC_GlobalEnvironment` | Restricts expensive reference-chain capture to the investigated collection. |
| `MaxMPCReferenceChainCaptures 4` | Bounds reference-chain snapshots for the process run. |
| `-trace=...` | Captures Memory Insights, modules, metadata, asset metadata, and logs from process startup. |

## Verify capture startup

Search the PS5 log for:

```text
RHI resource provenance journal:
```

Expected form:

```text
RHI resource provenance journal: '<project-log-directory>/RHIResourceProvenance-YYYYMMDD-HHMMSS-PID.rhiprov' maximum=10240 MiB queue=16384 records.
```

The journal is created under `FPaths::ProjectLogDir()`.

At failure, the summary should show:

```text
command_uses=1 priority_stacks=1
```

If either value is zero, the required launch settings did not reach the game.

Also record the summary counters:

- `queued` and `drained`;
- `bytes`;
- `queue_drops`;
- `disk_cap_drops`;
- `write_failures`;
- `disabled_or_start_failure_drops`;
- `failure_flush=yes/no`.

Any nonzero drop/failure count is a coverage gap and must be considered when assigning responsibility.

## Reproduction and capture checklist

1. Launch the Development PS5 build with the recommended parameters.
2. Confirm the journal startup line and its resolved path.
3. Reproduce the World Partition/Data Layer transition that normally precedes the crash.
4. Allow the failure path to print the journal summary and request its bounded flush.
5. Copy the newest matching `.rhiprov` journal from the devkit's project log directory.
6. Copy the PS5 log, Unreal Insights trace, executable, map file, PDB/Prospero symbols, and build identifier.
7. Do not mix symbols from another build when resolving `caller` or stack-frame PCs.

## Decoder setup

Run the decoder from the engine checkout:

```powershell
$Decoder = ".\Engine\Build\BatchFiles\DecodeRHIResourceProvenance.py"
$Journal = "D:\Captures\RHIResourceProvenance-YYYYMMDD-HHMMSS-PID.rhiprov"

py -3 $Decoder --help
```

The decoder accepts decimal or `0x` hexadecimal identifiers and addresses.

### Output formats

Normal decoded records support three output formats:

- `--format tsv` is the default and preserves the existing tab-separated output.
- `--format markdown` writes journal metadata plus a real escaped Markdown table that can be uploaded directly as `.md`.
- `--format json` writes `metadata`, `columns`, `records`, and `summary` objects. Numeric identifiers, cycle values, addresses, correlations, and caller PCs remain strings so JSON consumers cannot lose 64-bit precision.

Examples:

```powershell
py -3 $Decoder $Journal --id 1047167 `
  --format markdown --output ".\MPC-generation-1047167.md"

py -3 $Decoder $Journal --id 1047167 `
  --format json --output ".\MPC-generation-1047167.json"
```

All existing filters work with all three formats. `--responsibility-report` already produces Markdown; use its default behavior or `--format markdown`. It intentionally rejects `--format json`.

### Owner-key and collection-GUID discovery

Use `--owner-key` with either decimal or `0x` hexadecimal input. It emits the explicit `AccessOwner`, `CommandOwner`, and `BindingOwner` rows carrying that key:

```powershell
$OwnerKey = 0xe178c1313c798be2
py -3 $Decoder $Journal --owner-key $OwnerKey `
  --format markdown --output ".\owner-key.md"
```

Use `--collection-guid` with a 32-digit GUID; braces and hyphens are optional and matching is case-insensitive:

```powershell
$CollectionGuid = "01234567-89ab-cdef-0123-456789abcdef"
py -3 $Decoder $Journal --collection-guid $CollectionGuid `
  --format json --output ".\collection-guid.json"
```

The collection GUID is recorded on `AccessOwner` rows. The decoder emits it in the canonical lowercase, 32-hex-digit `collection_guid` column. Use the returned `owner_key`, `binding_id`, and resource `id` with their dedicated filters to expand the full binding and generation timelines.

## Recommended filtering workflow

### Step 1: discover the target generation

Search text-bearing records for the MPC path/name:

```powershell
py -3 $Decoder $Journal `
  --contains "MPC_GlobalEnvironment" `
  --output ".\MPC-discovery.tsv"
```

`--contains` is case-insensitive. Inspect these TSV columns:

- `seconds`;
- `operation`;
- `id`;
- `resource`;
- `flags_address`;
- `thread`;
- `caller`;
- `correlation`;
- `binding_id`;
- `causal_id`;
- `scene_refresh_id`;
- `contributor_id`;
- `data_layer_transition_id`;
- `cell_transition_id`;
- `primitive_teardown_id`;
- `owner_key`;
- `collection_guid`;
- `detail` and `text`.

The `id` column is the uniform-buffer generation ID. Prefer it over the raw address because an allocator may reuse the same address for later generations.

### Step 2: decode the complete generation

```powershell
$ResourceId = 1047167

py -3 $Decoder $Journal `
  --id $ResourceId `
  --output ".\MPC-$ResourceId.tsv"
```

Look for:

- `MPCAssetPostLoad`, `MPCAssetBeginDestroy`, and `MPCAssetFinishDestroy`;
- `MPCGCReferenceChain` and GC snapshots;
- `SceneMapInsert`, `SceneMapReplace`, and `SceneMapRemove`;
- `BindingCreate`, `BindingStore`, `BindingSubmit`, `BindingInvalidate`, and `BindingRelease`;
- `ReleaseReason`, `FinalRelease`, `ReferenceCensus`, and `PhysicalFree`;
- `CommandEnqueue`, `CommandExecute`, and `InvalidUse`;
- coverage/omission records.

Strong stale-use evidence is a `BindingSubmit`, `CommandEnqueue`, `CommandExecute`, or `InvalidUse` occurring after `FinalRelease` or `PhysicalFree` for the same generation.

### Step 3: generate the responsibility report

```powershell
py -3 $Decoder $Journal `
  --id $ResourceId `
  --responsibility-report `
  --output ".\MPC-$ResourceId-responsibility.md"
```

The report separates evidence into:

1. Asset reachability/root loss.
2. World instance and scene-map teardown.
3. Final RHI release and reference census.
4. Stale binding and primitive contributors.
5. Command submission and execution.

It reports a confidence level, verdict, evidence, and explicit coverage gaps. Do not promote a low-confidence verdict when the report lists missing command, binding, or lifecycle evidence.

### Step 4: expand relationship identifiers

Use the relationship IDs found in the generation TSV or responsibility report.

#### Resource address

```powershell
py -3 $Decoder $Journal `
  --address 0x106B8DF2E0 `
  --output ".\address.tsv"
```

Use this when the assertion only provides the `FRHIResource*` address. Check all returned generation IDs because addresses can be reused.

#### Atomic flags address

```powershell
py -3 $Decoder $Journal `
  --flags 0x106B8DF300 `
  --output ".\flags.tsv"
```

#### Cross-thread MPC causal chain

```powershell
py -3 $Decoder $Journal `
  --causal 42 `
  --output ".\causal-42.tsv"
```

This follows the game-thread request through render/RHI execution and resource association.

#### Scene MPC-map refresh

```powershell
py -3 $Decoder $Journal `
  --scene-refresh 57 `
  --output ".\scene-refresh-57.tsv"
```

#### Cached-binding lifetime

```powershell
py -3 $Decoder $Journal `
  --binding 910 `
  --output ".\binding-910.tsv"
```

This is the primary expansion after identifying a binding submitted after release.

#### Primitive contributor

```powershell
py -3 $Decoder $Journal `
  --contributor 314 `
  --output ".\contributor-314.tsv"
```

This joins the cached binding to actor, component, runtime cell, and Data Layer metadata when capture coverage is available.

#### Data Layer transition

```powershell
py -3 $Decoder $Journal `
  --data-layer-transition 27 `
  --output ".\data-layer-27.tsv"
```

#### Runtime-cell transition

```powershell
py -3 $Decoder $Journal `
  --cell-transition 81 `
  --output ".\cell-81.tsv"
```

#### Primitive teardown/invalidation bridge

```powershell
py -3 $Decoder $Journal `
  --primitive-teardown 93 `
  --output ".\teardown-93.tsv"
```

## Current `MPC_GlobalEnvironment` capture: exact JSON extraction

Use this block for retained generation `1516848`. Keep these as separate decoder commands because normal decoder filters are combined with logical **AND**.

The current capture attempted approximately 109,960 contributor descriptors (`65536` recorded plus `44424` omitted). For the next reproduction, add `r.RHI.ResourceProvenance.MaxContributorDescriptors 131072` to the existing `-ExecCmds` list. If the same route still reports descriptor omissions, raise it to `262144` and verify that journal queue/disk-drop counters remain zero.

### Pass 1: generation, responsibility, collection, and owners

```powershell
$Decoder = ".\Engine\Build\BatchFiles\DecodeRHIResourceProvenance.py"
$Journal = ".\capture.rhiprov"

py -3 $Decoder $Journal `
  --contains "MPC_GlobalEnvironment" `
  --format json `
  --output ".\01-mpc-discovery.json"

py -3 $Decoder $Journal `
  --id 1516848 `
  --format json `
  --output ".\02-generation-1516848.json"

py -3 $Decoder $Journal `
  --id 1516848 `
  --responsibility-report `
  --output ".\03-responsibility-1516848.md"

py -3 $Decoder $Journal `
  --collection-guid 29b56cb74cca1a3d8bba8787e4f00276 `
  --format json `
  --output ".\04-collection-owners.json"

py -3 $Decoder $Journal `
  --owner-key 0xe178c134fdd391b2 `
  --format json `
  --output ".\05-owner-water-minimap.json"

py -3 $Decoder $Journal `
  --owner-key 0xe178c10ebf7d45ae `
  --format json `
  --output ".\06-owner-chest-logo.json"

py -3 $Decoder $Journal `
  --owner-key 0xe178c10f9923cd9a `
  --format json `
  --output ".\07-owner-body.json"
```

Attach files `01` through `07` with `RHI_RESOURCE_PROVENANCE_ANALYSIS_PROMPT.md`. The prompt can inspect the generation JSON, distinguish the process-wide stale-submit count from target-generation rows, and return exact second-pass commands using the IDs it finds.

### Find target-generation stale submissions locally

```powershell
$Records = (
    Get-Content ".\02-generation-1516848.json" -Raw |
    ConvertFrom-Json
).records

$FinalRelease = $Records |
    Where-Object operation -eq "FinalRelease" |
    Sort-Object { [UInt64]$_.cycles } |
    Select-Object -First 1

if ($null -eq $FinalRelease) {
    throw "No FinalRelease row was found for generation 1516848."
}

$FinalCycles = [UInt64]$FinalRelease.cycles

$TargetStaleSubmits = $Records |
    Where-Object {
        $_.operation -eq "BindingSubmit" -and
        [UInt64]$_.cycles -gt $FinalCycles
    } |
    Sort-Object { [UInt64]$_.cycles }

$TargetStaleSubmits |
    Select-Object cycles, binding_id, owner_key, correlation, caller, detail, text |
    Format-Table -AutoSize

$TargetBindingIds = $TargetStaleSubmits |
    ForEach-Object binding_id |
    Where-Object { $_ -and $_ -ne "0" } |
    Sort-Object -Unique

$TargetBindingIds
```

`binding_submit_stale` in the crash coverage line is process-wide. The script above selects only `BindingSubmit` rows for generation `1516848` that occur after that generation's `FinalRelease`.

### Pass 2: binding and contributor relationship files

For every binding ID returned by the prompt or `$TargetBindingIds`, run:

```powershell
py -3 $Decoder $Journal `
  --binding <binding-id> `
  --format json `
  --output ".\binding-<binding-id>.json"
```

Attach the binding JSON files to the same analysis prompt. For every nonzero `contributor_id` identified in the generation or binding exports, run:

```powershell
py -3 $Decoder $Journal `
  --contributor <contributor-id> `
  --format json `
  --output ".\contributor-<contributor-id>.json"
```

The analysis prompt must replace every placeholder with an exact discovered ID and emit one command per required file. If the input already contains a contributor ID, it should request the contributor export immediately rather than waiting for another pass. If a contributor descriptor was omitted by capture coverage, no decoder filter can reconstruct it; the report must identify that as a coverage gap.

After attaching the requested relationship files, rerun the prompt to generate the final responsibility report. It may also request exact `--causal`, `--scene-refresh`, `--data-layer-transition`, `--cell-transition`, or `--primitive-teardown` exports when those relationship IDs are present.

## Filter semantics

Normal filters are combined with logical **AND**. For example:

```powershell
py -3 $Decoder $Journal --id 10 --binding 20
```

returns only rows matching both conditions. Run relationship filters separately first; otherwise an accidental intersection can produce an empty result.

`--responsibility-report` is different: it requires `--id`, scans the full journal in bounded passes, and follows related binding, contributor, teardown, cell, and Data Layer identifiers automatically.

## Validate the MPC teardown fix

The diagnostic branch now keeps cached-binding state in a process-wide 262,144-entry table instead of a 16-entry array per resource. A release snapshot scans that table and journals every live binding for the released generation. The crash log remains capped to 256 printed bindings to avoid recursively destabilizing the failure path; the `.rhiprov` journal is the complete source.

After the next repro, verify:

- `ReleaseBindingCoverage` reports `omitted_bindings=0`;
- the active binding count is no longer capped at 16;
- `SceneMapRemove` or `SceneMapReplaceOld` is followed by `BindingInvalidate` and `BindingRelease` rows for the retired generation;
- rebuilt bindings refer to the replacement resource generation;
- no later `BindingSubmit` or `CommandExecute` targets the retired generation;
- `record_flags` does not contain the text-truncated bit for the owner/contributor rows used in the verdict.

The scene keeps replaced uniform buffers alive while all cached raster/ray-tracing mesh commands are invalidated and rebuilt against the new MPC map. Only after recache completes can the retained old references leave scope.

## Responsibility interpretation

| Question | Primary evidence |
|---|---|
| Who allowed the MPC asset to become unreachable? | Asset lifecycle, GC snapshots, reference-chain rows, Asset Manager load/unload provenance. |
| Who destroyed or replaced the world instance? | Instance destroy/update events and scene-map insert/replace/remove rows. |
| Who performed the final RHI release? | `ReleaseReason`, `FinalRelease`, `ReferenceCensus`, caller PCs, and priority stack frames. |
| Who retained or created the stale binding? | Binding lifecycle, owner labels, binding ID, contributor ID, actor/component/cell/Data Layer records. |
| Who submitted/executed stale work? | `CommandEnqueue`, `CommandExecute`, causal ID, command-owner label, and stack frames. |

`PhysicalFree` is recorded after the C++ `delete Resource` expression completes; it is not the underlying allocator free stack. Use the same-run Memory Insights trace to recover allocation/free call stacks for the containing allocation.

## Troubleshooting

### No journal startup line

- Confirm the build is Development or Debug/DebugGame, not Test or Shipping.
- Confirm the executable contains the current instrumentation branch.
- Confirm `Journal 1` is present and the project log directory is writable.
- Search for `disabled_or_start_failure_drops` or an unavailable journal path.

### `command_uses=0`

- Confirm the exact `-ExecCmds` string reached the launched executable.
- Ensure commas remain inside the quoted `-ExecCmds` value.
- Do not place these options in UBT build arguments; they are runtime launch arguments.

### Discovery TSV is empty

- Verify the target string matches the actual MPC name/path.
- Search a shorter case-insensitive fragment with `--contains`.
- Use `--address` from the assertion if the target path was not associated before failure.
- Check queue/disk/write omission counters.

### Responsibility report has low confidence

- Confirm `CommandUses=1`, `PriorityStacks=1`, and `Contributors=1`.
- Inspect coverage rows and omission counters.
- Confirm the journal was not capped before the crash.
- Verify the selected ID is the failing generation rather than another generation at a reused address.

### Decoder reports a truncated record

The final binary record may not have been completely written if the process terminated before the failure flush completed. Preserve the original file, check `failure_flush`, and use an earlier complete journal if available.

The journal text payload is now 2,048 characters, compact retained names/paths were expanded, and provenance builders use dynamic `FString` formatting. `record_flags & 0x0001` still explicitly marks an exceptionally long value that exceeded the bounded crash-safe journal record; do not treat such a row as a complete owner label.

## Minimal handoff package

Provide the following when asking for analysis:

- the `.rhiprov` journal;
- the generated `MPC-discovery.tsv`;
- the selected generation TSV;
- the generated responsibility Markdown report;
- the PS5 log containing the assertion and journal summary;
- the Unreal Insights trace;
- exact build commit and symbols;
- a short description of the World Partition/Data Layer actions immediately before the crash.
