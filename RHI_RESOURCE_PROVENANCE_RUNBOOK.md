# PS5 UE 5.8.2 RHI Resource Provenance Runbook

This runbook describes how to launch a Development PS5 build with the MPC/RHI provenance instrumentation, recover the journal after a crash, filter the recorded evidence, and produce the automated responsibility report.

## Preconditions

- Build configuration: `Development | PS5`.
- Engine branch: `diagnostics/rhi-resource-provenance`.
- Use the executable and symbols produced by the same build as the captured journal.
- Ensure the devkit has room for the journal and Unreal Insights trace. The journal is capped at 10 GiB with the recommended settings.
- The instrumentation is compiled out of Test and Shipping.

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

## Filter semantics

Normal filters are combined with logical **AND**. For example:

```powershell
py -3 $Decoder $Journal --id 10 --binding 20
```

returns only rows matching both conditions. Run relationship filters separately first; otherwise an accidental intersection can produce an empty result.

`--responsibility-report` is different: it requires `--id`, scans the full journal in bounded passes, and follows related binding, contributor, teardown, cell, and Data Layer identifiers automatically.

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

The final record may not have been completely written if the process terminated before the failure flush completed. Preserve the original file, check `failure_flush`, and use an earlier complete journal if available.

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
