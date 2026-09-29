# Confidential RHI Provenance Analysis Prompt

Use this prompt in an approved environment where the confidential crash artifacts can be attached. Supported inputs include `.tsv`, `.txt`, `.md`, `.json`, and `.xlsx`. Prefer original decoder output over Excel copies because Excel may round 64-bit resource IDs, addresses, correlations, and caller PCs beyond 15 digits.

A `.txt` or `.md` file may contain unchanged tab-separated decoder output rather than prose or a Markdown table. Detect the format from the contents, parse tab-separated rows when present, and preserve all identifiers as text. Native decoder `.json` files contain `metadata`, `columns`, `records`, and `summary`; treat every value as text even when it looks numeric.

---

You are analyzing confidential Unreal Engine 5.8.2 PS5 crash-instrumentation output for an RHI uniform-buffer stale-reference crash involving `MPC_GlobalEnvironment`.

Do not browse the internet, upload the data elsewhere, or reproduce unrelated project information. Analyze only the attached files and quote only evidence relevant to the investigated resource generation.

## Attached files

The attachments may include:

- MPC discovery TSV/TXT/MD/JSON/XLSX
- Resource-generation ID TSV/TXT/MD/JSON/XLSX
- Responsibility report MD/TXT/XLSX
- Binding TSV/TXT/MD/JSON/XLSX
- Contributor TSV/TXT/MD/JSON/XLSX
- Causal-chain TSV/TXT/MD/JSON/XLSX
- Scene-refresh TSV/TXT/MD/JSON/XLSX
- Data Layer transition TSV/TXT/MD/JSON/XLSX
- Runtime-cell transition TSV/TXT/MD/JSON/XLSX
- Primitive teardown TSV/TXT/MD/JSON/XLSX
- PS5 log
- Optional symbolized callstack/address report

The current preliminary observation is:

```text
Release cause: MPCGameThreadDestroy
```

Treat that as the immediate release mechanism, not automatically as the root cause.

## Current capture observations to verify

The 2026-09-29 capture provides the following preliminary identifiers. Treat them as discovery hints and verify each value against the attached decoder output before using it in a verdict:

- retained resource generation ID: `1516848`;
- resource address: `0x107a13c220`;
- flags address: `0x107a13c228`;
- collection path: `/Game/Art/3rd_Party/SoStylized/Environment/MPC_GlobalEnvironment.MPC_GlobalEnvironment`;
- collection GUID: `29b56cb74cca1a3d8bba8787e4f00276`;
- instance: `/Engine/Transient.MaterialParameterCollectionInstance_2147381558`;
- world: `/Game/Maps/MainMap.MainMap`;
- candidate access-owner keys: `0xe178c134fdd391b2`, `0xe178c10ebf7d45ae`, and `0xe178c10f9923cd9a`;
- candidate materials: `MI_Water_Minimap`, `MI_UE4Man_ChestLogo`, and `M_UE4Man_Body`;
- candidate exact-command correlation: `435638533644002`.

The failure banner's `observed_id=15987178197214944733`, `observed_type=221`, and `old_packed=0xdddddddd` are freed-memory poison observations. Do not use the observed poison ID as the generation ID. The retained side table recovered generation `1516848`.

The retained release-owner markers show this provisional chain:

```text
WorldPostGCInvalidCollection
→ SceneParameterCollectionMapReplace
→ MPCInstanceFinishDestroy
→ MPCGameThreadDestroy / SafeRelease
→ FinalRelease
→ PhysicalFree
```

The release snapshot reports `active=80`, `table_capacity=262144`, `omitted=0`, and `failure_log_dumped=80/80`. Therefore 80 is the complete active set found for this generation in that capture, not a remaining 16-entry capacity cap. Every displayed retained binding has `live_copies=1` and `invalidated=0`; some had already been submitted and some had only been moved/stored.

The coverage summary reports the process-wide counter `binding_submit_stale=6`. Determine how many of those six rows carry generation `1516848`; do not assume every process-wide stale submission belongs to this MPC. Identify each matching target-generation `BindingSubmit` row and binding ID before selecting a responsible mesh. Do not assume the one retained `exact command owner` row is the command that caused the assertion unless its binding/correlation and cycle ordering join to `CommandExecute` or `InvalidUse`.

Coverage is incomplete for contributor attribution: `contributor_descriptors=65536`, `contributor_descriptor_omissions=44424`, `contributor_without_actor=9`, and `contributor_without_runtime_cell=189`. Data Layer, cell-transition, primitive-teardown, and reference-census row omissions were reported as zero. Treat a missing actor, cell, or Data Layer join as unknown when its contributor descriptor may have been omitted; it is not evidence that streaming was uninvolved.

Command ownership also experienced `command_owner_overwrites=9617`. Preserve exact correlation IDs and cycle ordering, and report when overwrite/eviction prevents a direct join.

## Data-integrity checks

Before analyzing:

1. Identify every attached file and worksheet.
2. Confirm which resource generation ID is being investigated.
3. Confirm all relevant rows belong to the same generation ID.
4. Sort events using `cycles` as the authoritative ordering and `seconds` as the readable timestamp.
5. Check whether Excel converted any IDs, addresses, callers, correlations, or relationship IDs to scientific notation, rounded numbers, floating-point values, dates, or values ending in suspicious repeated zeroes.
6. Prefer the original TSV value when it conflicts with Excel.
7. Treat hexadecimal addresses and all 64-bit identifiers as text.
8. Report any suspected Excel precision loss before drawing conclusions.
9. Do not merge different generations merely because they used the same resource address.
10. Inspect file contents rather than trusting the extension: `.txt` and `.md` may contain raw tab-separated decoder rows.
11. For decoder text output, preserve leading `# version=...` metadata and use the following tab-separated header row as the column definition.
12. For an actual responsibility Markdown report, parse its headings, verdict, evidence lists, and coverage gaps as Markdown rather than TSV.
13. For native decoder JSON, parse the `records` array using the `columns` list as the canonical order, retain the `metadata` and `summary` objects, and do not coerce any string field to a floating-point number.
14. Preserve `owner_key` as hexadecimal text and `collection_guid` as canonical lowercase 32-hex-digit text. Use exact equality for both; never join records by a truncated owner label or a partial GUID.

## Investigation objective

Determine, separately:

1. Why the MPC asset or world instance was destroyed.
2. Which higher-level operation requested destruction.
3. Which callsite performed the final RHI reference decrement.
4. Which binding retained or acquired the stale uniform-buffer generation.
5. Which actor, component, material, render proxy, runtime cell, and Data Layer contributed that binding.
   Join owner records by exact `owner_key` and confirm the material's exact `collection_guid`; do not infer either value from a truncated label.
6. Which command submitted and executed the stale reference.
7. Whether World Partition or a Data Layer transition causally initiated the teardown.
8. Whether the evidence proves a use-after-release or merely shows suspicious retention.

Do not collapse these into one owner.

## Required analysis procedure

### A0. Expand discovery into the complete relationship closure

`--contains MPC_GlobalEnvironment` is only a discovery pass. It cannot enumerate bindings whose text contains only a material, proxy, actor, owner key, or relationship ID. Never use the name-filtered output as the final analysis dataset.

After discovering the retained generation, export these as separate datasets because decoder filters are conjunctive:

1. the complete generation with `--id 1516848`;
2. every access owner for collection GUID `29b56cb74cca1a3d8bba8787e4f00276`;
3. every candidate owner key;
4. every binding that is live at release, submitted stale, invalidated, or referenced by the failure chain;
5. each contributor linked to those bindings;
6. each causal/correlation ID linked to stale enqueue or execution;
7. each scene-refresh, Data Layer transition, cell transition, and primitive-teardown ID reached from those records.

Do not combine `--id` with `--binding`, `--owner-key`, `--collection-guid`, or transition filters unless deliberately computing their intersection. Relationship rows may store the join ID in another field and may not carry the target resource ID.

Use the generation export to find every target-generation `BindingSubmit` row ordered after `FinalRelease` or otherwise recorded while the target was stale. Compare that target count with the process-wide count of six, then export only the matching binding IDs individually. Prioritize bindings that also reach `CommandEnqueue`, `CommandExecute`, or `InvalidUse`; do not export thousands of unrelated meshes merely because their materials reference the same MPC.

### A. Identify the target generation

Use the responsibility report and discovery data to identify:

- resource generation ID;
- resource address;
- atomic-flags address;
- MPC collection path;
- MPC collection GUID (`collection_guid`);
- MPC access owner key (`owner_key`);
- MPC instance path;
- world path;
- creation timestamp;
- final-release timestamp;
- physical-free timestamp.

If multiple generation IDs appear for the same address, analyze each separately and identify which one reached the assertion.

### B. Reconstruct the release chain

For the target generation, inspect these operations in chronological order:

```text
MPCAssetManagerUnload
MPCAssetBeginDestroy
MPCAssetFinishDestroy
GCPreSnapshot
GCPostSurvived
GCPostCollected
MPCGCReferenceChain
SceneMapInsert
SceneMapRemove
SceneMapReplaceOld
SceneMapReplaceNew
ReleaseOwner
ReleaseCause
MPCInstanceFinishDestroy
MPCGameThreadDestroy
FinalRelease
DestructorBegin
PhysicalFree
```

The decoder may represent some causes in the `detail` or `text` column rather than the `operation` column.

For `MPCGameThreadDestroy`, look immediately backward for the higher-level cause:

- `MPCInstanceFinishDestroy`;
- `MPCAssetFinishDestroy`;
- `WorldPostGCInvalidCollection`;
- `WorldReplacedMPCInstance`;
- Asset Manager unload;
- scene-map removal or replacement.

Explain the difference between:

- the system that decided to destroy the instance;
- `GameThread_Destroy`, which queued the destruction;
- the render-thread `SafeRelease`;
- the callsite that performed `FinalRelease`.

Do not blame `MPCGameThreadDestroy` merely because it is the last explicit release marker.

### C. Identify the final reference decrement

Inspect:

```text
ReferenceCensus
ReferenceCensusCoverage
FinalRelease
StackFrame
```

For every `ReferenceCensus` row, decode or report:

- `addrefs`;
- `releases`;
- `final_releases`;
- saturation flags;
- caller address;
- last-cycle timestamp.

The callsite with `final_releases > 0` is the final-decrement callsite.

If caller addresses have not been symbolized, label them:

```text
Unresolved caller PC — requires exact-build symbols
```

Do not invent a function name.

### D. Determine whether stale use is proven

Using the target generation only, compare every event against `FinalRelease` and `PhysicalFree`.

Inspect:

```text
ReleaseBindingSnapshot
ReleaseBindingCoverage
BindingSubmit
CommandEnqueue
CommandExecute
InvalidUse
ExternalUse
```

Classify the evidence:

- `InvalidUse` after `FinalRelease`: confirmed invalid reference operation.
- `CommandExecute` after `PhysicalFree`: confirmed execution against a dead generation.
- `CommandExecute` after `FinalRelease`: strong stale-command evidence.
- `BindingSubmit` after `FinalRelease`: strong stale-binding evidence.
- `CommandEnqueue` after `FinalRelease`: stale work was produced after release.
- `live_copies > 0` at release without later use: suspicious retention, not confirmed use.
- No post-release events: release is known, but stale consumption is not proven.

Use strict cycle ordering. Do not order two events merely because their rounded `seconds` values match.

### E. Identify stale binding ownership

For every binding related to the target generation, reconstruct:

```text
BindingCreate
BindingStore
BindingCopy
BindingMove
BindingOwner
BindingContributor
BindingSubmit
BindingInvalidate
BindingRelease
PrimitiveTeardownBinding
```

For each suspicious binding, report:

- binding ID;
- first creation/store time;
- owner text;
- contributor ID;
- last operation before release;
- live-copy count at release;
- whether it was invalidated;
- whether it was released;
- whether it was submitted after final release;
- whether it was submitted after physical free.

Distinguish these cases:

1. No invalidation was recorded.
2. Invalidation was recorded too late.
3. Invalidation occurred, but another copy survived.
4. Binding was copied or moved after invalidation.
5. Binding was correctly released and is probably not responsible.
6. Coverage is insufficient to decide.

### F. Resolve primitive contributors

For every suspicious `contributor_id`, join:

```text
ContributorRegistered
ContributorActor
ContributorComponent
ContributorWorldPartition
ContributorDataLayer
ContributorRetired
ContributorCoverageOmitted
BindingContributor
```

Extract:

- actor path;
- component path;
- material/render-proxy/resource/level label;
- runtime-cell GUID;
- runtime-cell package and debug name;
- external Data Layer;
- Data Layer names;
- runtime and effective Data Layer states;
- spatial-loading state and whether that state was known;
- contributor retirement time.

Identify the exact contributor associated with the post-release binding submission. Do not list unrelated contributors as responsible.

### G. Resolve command responsibility

For each suspicious causal/correlation ID, join:

```text
CausalRequest
CausalExecute
CausalLink
CausalResource
CommandOwner
CommandEnqueue
CommandExecute
StackFrame
InvalidUse
```

Report separately:

- game-thread requester;
- render-command producer;
- RHI-command producer;
- execution thread;
- enqueue time;
- execute time;
- whether enqueue occurred before or after release;
- whether execution occurred before or after release;
- associated binding ID;
- associated resource generation.

If only caller PCs exist, preserve the exact addresses and state that symbol resolution is pending.

### H. Connect World Partition and Data Layer transitions

Join the suspicious contributor and teardown using:

```text
DataLayerStateRequest
DataLayerStateRejected
DataLayerTargetStateChanged
DataLayerEffectiveStateChanged
DataLayerStateNoOp
CellStateRequest
CellStateAccepted
CellStateBlocked
CellStateProgress
CellStateCompleted
CellTransitionSuperseded
PrimitiveTeardownBegin
PrimitiveTeardownCacheRemove
PrimitiveTeardownBinding
PrimitiveTeardownEnd
```

Determine whether the observed chain is:

```text
Data Layer transition
→ runtime-cell transition
→ primitive teardown/cache removal
→ MPC instance/scene-map release
→ surviving binding
→ post-release command
```

Only declare this causal if the IDs and ordered events connect. Temporal proximity alone is insufficient.

### I. Validate the teardown fix

For captures made after the cached-MPC teardown fix, verify that every binding in the release snapshot receives an invalidation/release before the retired resource reaches `FinalRelease`. Treat any of the following as a regression or coverage failure:

- `ReleaseBindingCoverage` reports a nonzero omitted count;
- the snapshot count is suspiciously fixed at 16;
- a live binding has no later `BindingInvalidate`/`BindingRelease`;
- `BindingSubmit` or `CommandExecute` targets the retired generation after `PhysicalFree`;
- an ownership/contributor row required for the verdict has `record_flags & 0x0001`.

The failure banner prints at most 256 binding rows by design. Use the journal—not the banner—to enumerate the complete process-wide binding table snapshot.

### J. Audit coverage

Inspect the responsibility report, PS5 log, and all coverage operations:

```text
GCCoverageOmitted
ReleaseBindingCoverage
ContributorCoverageOmitted
DataLayerTransitionCoverage
CellTransitionCoverage
PrimitiveTeardownCoverage
ReferenceCensusCoverage
```

Also inspect:

```text
queue_drops
disk_cap_drops
write_failures
disabled_or_start_failure_drops
failure_flush
command_uses
priority_stacks
```

State exactly which conclusions are weakened by missing evidence.

## Required output

### 1. Executive verdict

In five to ten sentences, state:

- what caused the MPC generation to be released;
- whether that release appears legitimate;
- whether stale retention/use is proven;
- the most likely responsible binding/contributor/command;
- overall confidence.

Use one of:

```text
Confirmed
High confidence
Medium confidence
Low confidence
Inconclusive
```

### 2. Responsibility matrix

| Responsibility | Identified owner/cause | Evidence | Confidence |
|---|---|---|---|
| Asset/root reachability loss | | | |
| World-instance destruction | | | |
| Scene-map removal/replacement | | | |
| Final RHI decrement | | | |
| Stale binding creation/retention | | | |
| Actor/component contributor | | | |
| Data Layer/runtime-cell initiator | | | |
| Command enqueue | | | |
| Command execution | | | |
| Invalid reference attempt | | | |

Do not put `MPCGameThreadDestroy` in every row.

### 3. Critical timeline

Create a chronological table containing only decision-relevant events:

| Order | Seconds | Cycles | Thread | Operation | Resource ID | Binding ID | Causal/correlation ID | Contributor/transition ID | Evidence |
|---:|---:|---:|---:|---|---:|---:|---:|---:|---|

Include at minimum:

- last healthy ownership/binding event;
- GC or asset teardown;
- instance/scene-map teardown;
- release snapshot;
- final release;
- physical free;
- stale submission/enqueue/execute;
- invalid-use assertion.

### 4. Release-chain analysis

Explain:

```text
Higher-level destroy cause
→ GameThread_Destroy request
→ render-thread DestroyCollectionCommand
→ SafeRelease
→ FinalRelease
→ PhysicalFree
```

Identify which steps are proven and which are inferred.

### 5. Stale-binding analysis

For each suspicious binding, provide:

| Binding ID | Created/stored by | Contributor | Invalidated? | Live at release? | Submitted after release? | Verdict |
|---:|---|---:|---|---|---|---|

### 6. Contributor identification

Provide the relevant:

- actor;
- component;
- material/render proxy;
- runtime cell;
- Data Layers;
- primitive teardown ID.

Explain why this contributor is connected to the stale binding.

### 7. Command analysis

Provide:

- causal ID;
- correlation ID;
- enqueue caller/thread/time;
- execute caller/thread/time;
- relative ordering to `FinalRelease` and `PhysicalFree`;
- symbolization status.

### 8. World Partition/Data Layer conclusion

State whether World Partition/Data Layer activity:

- initiated the valid teardown;
- failed to invalidate cached draw commands;
- is merely temporally nearby;
- or cannot be determined.

### 9. Coverage and limitations

List every omission/drop, unresolved caller PC, Excel precision concern, and absent relationship file.

For each limitation, state which conclusion it affects.

### 10. Final actionable conclusion

End with exactly these headings:

```text
Legitimate releaser:
Stale holder:
Stale command producer:
Stale command executor:
Responsible actor/component:
World Partition/Data Layer trigger:
Most likely engine defect:
Confidence:
```

If a field is not established, write:

```text
Not established by current capture
```

### 11. Next required decoder commands

If evidence is missing, provide exact commands using the discovered IDs:

```powershell
py -3 $Decoder $Journal --binding <id> --output ".\binding-<id>.tsv"
py -3 $Decoder $Journal --contributor <id> --output ".\contributor-<id>.tsv"
py -3 $Decoder $Journal --causal <id> --output ".\causal-<id>.tsv"
py -3 $Decoder $Journal --scene-refresh <id> --output ".\scene-refresh-<id>.tsv"
py -3 $Decoder $Journal --data-layer-transition <id> --output ".\data-layer-<id>.tsv"
py -3 $Decoder $Journal --cell-transition <id> --output ".\cell-<id>.tsv"
py -3 $Decoder $Journal --primitive-teardown <id> --output ".\teardown-<id>.tsv"
```

Replace placeholders with actual IDs found in the attachments.


### Current-capture extraction set

Run these as separate commands:

```powershell
$Decoder = ".\DecodeRHIResourceProvenance.py"
$Journal = ".\capture.rhiprov"

py -3 $Decoder $Journal --contains "MPC_GlobalEnvironment" --format json --output ".\01-mpc-discovery.json"
py -3 $Decoder $Journal --id 1516848 --format json --output ".\02-generation-1516848.json"
py -3 $Decoder $Journal --id 1516848 --responsibility-report --output ".\03-responsibility-1516848.md"
py -3 $Decoder $Journal --collection-guid 29b56cb74cca1a3d8bba8787e4f00276 --format json --output ".\04-collection-owners.json"
py -3 $Decoder $Journal --owner-key 0xe178c134fdd391b2 --format json --output ".\05-owner-water-minimap.json"
py -3 $Decoder $Journal --owner-key 0xe178c10ebf7d45ae --format json --output ".\06-owner-chest-logo.json"
py -3 $Decoder $Journal --owner-key 0xe178c10f9923cd9a --format json --output ".\07-owner-body.json"
py -3 $Decoder $Journal --causal 435638533644002 --format json --output ".\08-causal-candidate.json"
```

Next, read `02-generation-1516848.json`. Treat `435638533644002` as a command correlation first, not automatically as a causal ID; run `--causal` only if matching CausalRequest/CausalExecute/CausalResource rows prove that interpretation. Identify every target-generation stale-submission binding ID plus its contributor and transition IDs, and run the existing `--binding`, `--contributor`, `--scene-refresh`, `--data-layer-transition`, `--cell-transition`, and `--primitive-teardown` commands once per discovered ID. Do not guess absent IDs from labels.

## Accuracy rules

- Never infer a function name from an unsymbolized address.
- Never treat the raw resource address as a unique lifetime identifier.
- Never treat temporal proximity as causality without a matching relationship ID.
- Never blame the final releaser solely because it performed the last decrement.
- Never describe `PhysicalFree` as the allocator free stack.
- Never claim stale use unless the same generation has a qualifying event after release.
- Clearly distinguish facts, strong inferences, hypotheses, and missing evidence.
- Cite every conclusion using the attachment filename, worksheet, operation, cycle/time, and relevant ID.
