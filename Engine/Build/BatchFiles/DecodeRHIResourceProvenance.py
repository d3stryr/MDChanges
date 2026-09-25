#!/usr/bin/env python3
"""Decode a UE RHI resource provenance .rhiprov journal.

Examples:
  python DecodeRHIResourceProvenance.py RHIResourceProvenance-....rhiprov --id 1047167
  python DecodeRHIResourceProvenance.py RHIResourceProvenance-....rhiprov --address 0x106b8df2e0
  python DecodeRHIResourceProvenance.py RHIResourceProvenance-....rhiprov --address 0x106b8df2e0 --output match.tsv
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys
from typing import BinaryIO, Iterator, NamedTuple, Optional


FILE_HEADER = struct.Struct("<8sHHIBBHIIQdQ")
RECORD_HEADER = struct.Struct("<IHHIBBBBHHIIQQQQQQ")
FILE_MAGIC = b"RHIPROV\x00"
RECORD_MAGIC = 0x52504852
SUPPORTED_VERSION = 1

KIND_NAMES = {
    1: "Create",
    2: "DebugName",
    3: "OwnerName",
    4: "OwnerPath",
    5: "Lifecycle",
    6: "CommandUse",
    7: "StackFrame",
    8: "Marker",
}

OPERATION_NAMES = {
    0: "Create",
    1: "AddRef",
    2: "Release",
    3: "FinalRelease",
    4: "MarkForDelete",
    5: "MarkForDeleteAlreadySet",
    6: "DeleteCheck",
    7: "DeleteBegin",
    8: "DeleteCancelled",
    9: "DestructorBegin",
    10: "PhysicalFree",
    11: "CommandEnqueue",
    12: "CommandExecute",
    13: "UpdateRequest",
    14: "UpdateExecute",
    15: "ExternalUse",
    16: "OwnerAssociation",
    17: "ReleaseReason",
    18: "BindingStore",
    19: "InvalidUse",
    20: "AccessOwner",
    21: "ReleaseOwner",
    22: "CommandOwner",
    23: "BindingCreate",
    24: "BindingCopy",
    25: "BindingMove",
    26: "BindingSubmit",
    27: "BindingRelease",
    28: "BindingOwner",
    29: "BindingInvalidate",
    30: "CausalRequest",
    31: "CausalExecute",
    32: "CausalLink",
    33: "CausalResource",
    34: "GCPreSnapshot",
    35: "GCPostSurvived",
    36: "GCPostCollected",
    37: "GCCoverageOmitted",
    38: "SceneMapInsert",
    39: "SceneMapRemove",
    40: "SceneMapReplaceOld",
    41: "SceneMapReplaceNew",
    42: "ReleaseCause",
    43: "ReleaseBindingSnapshot",
    44: "ReleaseBindingCoverage",
    45: "ContributorRegistered",
    46: "ContributorActor",
    47: "ContributorComponent",
    48: "ContributorWorldPartition",
    49: "ContributorDataLayer",
    50: "ContributorRetired",
    51: "BindingContributor",
    52: "ContributorCoverageOmitted",
    53: "DataLayerStateRequest",
    54: "DataLayerStateRejected",
    55: "DataLayerTargetStateChanged",
    56: "DataLayerEffectiveStateChanged",
    57: "DataLayerStateNoOp",
    58: "DataLayerTransitionCoverage",
    59: "CellStateRequest",
    60: "CellStateAccepted",
    61: "CellStateBlocked",
    62: "CellStateProgress",
    63: "CellStateCompleted",
    64: "CellTransitionSuperseded",
    65: "CellTransitionCoverage",
    66: "PrimitiveTeardownBegin",
    67: "PrimitiveTeardownCacheRemove",
    68: "PrimitiveTeardownBinding",
    69: "PrimitiveTeardownEnd",
    70: "PrimitiveTeardownCoverage",
    71: "MPCAssetPostLoad",
    72: "MPCAssetBeginDestroy",
    73: "MPCAssetFinishDestroy",
    74: "MPCGCReferenceChain",
    75: "ReferenceCensus",
    76: "ReferenceCensusCoverage",
    77: "MPCAssetManagerLoadRequest",
    78: "MPCAssetManagerLoadComplete",
    79: "MPCAssetManagerUnload",
}

DATA_LAYER_STATE_NAMES = {
    0: "Unloaded",
    1: "Loaded",
    2: "Activated",
}

DATA_LAYER_REJECTION_NAMES = {
    0: "NotRuntime",
    1: "ClientOnlyFromServer",
    2: "ServerOnlyFromClient",
    3: "AuthoritativeFromClient",
}

CELL_ACTION_NAMES = {
    0: "Unknown",
    1: "Load",
    2: "Activate",
    3: "Deactivate",
    4: "Unload",
    5: "Show",
    6: "Hide",
    7: "StreamingLevel",
}

CELL_BLOCK_REASON_NAMES = {
    0: "None",
    1: "StreamingDisabled",
    2: "BudgetExhausted",
    3: "CannotUnload",
    4: "FailedToLoad",
}

LEVEL_STREAMING_STATE_NAMES = {
    0: "Removed",
    1: "Unloaded",
    2: "FailedToLoad",
    3: "Loading",
    4: "LoadedNotVisible",
    5: "MakingVisible",
    6: "LoadedVisible",
    7: "MakingInvisible",
    255: "NotApplicable",
}

RELEASE_CAUSE_NAMES = {
    0: "Unknown",
    1: "MPCAssetBeginDestroy",
    2: "MPCAssetFinishDestroy",
    3: "MPCInstanceFinishDestroy",
    4: "MPCGameThreadDestroy",
    5: "MPCUniformBufferRecreate",
    6: "MPCUniformBufferInvalidReplacement",
    7: "WorldReplacedMPCInstance",
    8: "WorldPostGCInvalidCollection",
    9: "SceneMapRemove",
    10: "SceneMapReplace",
}

MPC_ASSET_STATE_FLAGS = (
    (0, "rooted"),
    (1, "standalone"),
    (2, "public"),
    (3, "transient"),
    (4, "begin_destroyed"),
    (5, "finish_destroyed"),
    (6, "was_loaded"),
    (7, "async_loading"),
    (8, "has_linker"),
)

GC_STATE_FLAGS = (
    (0, "collection_valid"),
    (1, "collection_rooted"),
    (2, "collection_standalone"),
    (3, "collection_public"),
    (4, "collection_transient"),
    (5, "collection_begin_destroyed"),
    (6, "collection_finish_destroyed"),
    (7, "instance_rooted"),
    (8, "partitioned_world"),
    (9, "runtime_cell_world"),
    (10, "game_world"),
    (11, "instance_begin_destroyed"),
)

RESOURCE_TYPE_NAMES = {
    0: "RRT_None",
    1: "RRT_SamplerState",
    2: "RRT_RasterizerState",
    3: "RRT_DepthStencilState",
    4: "RRT_BlendState",
    5: "RRT_VertexDeclaration",
    6: "RRT_VertexShader",
    7: "RRT_MeshShader",
    8: "RRT_AmplificationShader",
    9: "RRT_PixelShader",
    10: "RRT_GeometryShader",
    11: "RRT_RayTracingShader",
    12: "RRT_ComputeShader",
    13: "RRT_GraphicsPipelineState",
    14: "RRT_ComputePipelineState",
    15: "RRT_RayTracingPipelineState",
    16: "RRT_BoundShaderState",
    17: "RRT_UniformBufferLayout",
    18: "RRT_UniformBuffer",
    19: "RRT_Buffer",
    20: "RRT_Texture",
    21: "RRT_Texture2D_DEPRECATED",
    22: "RRT_Texture2DArray_DEPRECATED",
    23: "RRT_Texture3D_DEPRECATED",
    24: "RRT_TextureCube_DEPRECATED",
    25: "RRT_TextureReference",
    26: "RRT_TimestampCalibrationQuery_DEPRECATED",
    27: "RRT_GPUFence",
    28: "RRT_RenderQuery",
    29: "RRT_RenderQueryPool",
    30: "RRT_Viewport",
    31: "RRT_UnorderedAccessView",
    32: "RRT_ShaderResourceView",
    33: "RRT_RayTracingAccelerationStructure_DEPRECATED",
    34: "RRT_RayTracingGeometry",
    35: "RRT_RayTracingScene",
    36: "RRT_RayTracingShaderBindingTable",
    37: "RRT_StagingBuffer",
    38: "RRT_CustomPresent",
    39: "RRT_ShaderLibrary",
    40: "RRT_ShaderBundle",
    41: "RRT_WorkGraphShader",
    42: "RRT_WorkGraphPipelineState",
    43: "RRT_StreamSourceSlot",
    44: "RRT_ResourceCollection",
    45: "RRT_DescriptorRange",
}


class FileHeader(NamedTuple):
    version: int
    header_size: int
    pointer_size: int
    tchar_size: int
    queue_capacity: int
    identity_capacity: int
    start_cycles: int
    seconds_per_cycle: float
    maximum_file_bytes: int


class Record(NamedTuple):
    kind: int
    operation: int
    resource_type: int
    flags: int
    thread_id: int
    packed_value: int
    cycles: int
    resource_id: int
    resource_address: int
    flags_address: int
    caller_address: int
    correlation_id: int
    text: str


def parse_integer(value: str) -> int:
    return int(value, 0)


def read_exact(stream: BinaryIO, size: int) -> bytes:
    data = stream.read(size)
    if len(data) != size:
        raise EOFError(f"expected {size} bytes, received {len(data)}")
    return data


def read_file_header(stream: BinaryIO) -> FileHeader:
    raw = read_exact(stream, FILE_HEADER.size)
    (
        magic,
        version,
        header_size,
        endian_marker,
        pointer_size,
        tchar_size,
        _reserved,
        queue_capacity,
        identity_capacity,
        start_cycles,
        seconds_per_cycle,
        maximum_file_bytes,
    ) = FILE_HEADER.unpack(raw)

    if magic != FILE_MAGIC:
        raise ValueError(f"not an RHI provenance journal: magic={magic!r}")
    if version != SUPPORTED_VERSION:
        raise ValueError(f"unsupported journal version {version}")
    if endian_marker != 0x01020304:
        raise ValueError(f"unsupported endian marker 0x{endian_marker:08x}")
    if header_size < FILE_HEADER.size:
        raise ValueError(f"invalid file header size {header_size}")
    if header_size > FILE_HEADER.size:
        read_exact(stream, header_size - FILE_HEADER.size)

    return FileHeader(
        version,
        header_size,
        pointer_size,
        tchar_size,
        queue_capacity,
        identity_capacity,
        start_cycles,
        seconds_per_cycle,
        maximum_file_bytes,
    )


def iter_records(stream: BinaryIO) -> Iterator[Record]:
    while True:
        raw = stream.read(RECORD_HEADER.size)
        if not raw:
            return
        if len(raw) != RECORD_HEADER.size:
            raise EOFError("truncated record header at end of journal")

        (
            magic,
            version,
            header_size,
            record_size,
            kind,
            operation,
            resource_type,
            _reserved,
            flags,
            text_bytes,
            thread_id,
            packed_value,
            cycles,
            resource_id,
            resource_address,
            flags_address,
            caller_address,
            correlation_id,
        ) = RECORD_HEADER.unpack(raw)

        if magic != RECORD_MAGIC:
            raise ValueError(f"invalid record magic 0x{magic:08x}")
        if version != SUPPORTED_VERSION:
            raise ValueError(f"unsupported record version {version}")
        if header_size < RECORD_HEADER.size or record_size < header_size:
            raise ValueError(
                f"invalid record sizes: header={header_size}, record={record_size}"
            )

        if header_size > RECORD_HEADER.size:
            read_exact(stream, header_size - RECORD_HEADER.size)

        payload_size = record_size - header_size
        payload = read_exact(stream, payload_size)
        if text_bytes > payload_size:
            raise ValueError(
                f"text length {text_bytes} exceeds payload length {payload_size}"
            )
        text = payload[:text_bytes].decode("utf-8", errors="replace")

        yield Record(
            kind,
            operation,
            resource_type,
            flags,
            thread_id,
            packed_value,
            cycles,
            resource_id,
            resource_address,
            flags_address,
            caller_address,
            correlation_id,
            text,
        )


def sanitize_tsv(value: str) -> str:
    return value.replace("\t", " ").replace("\r", " ").replace("\n", " ")


def decode_packed_detail(record: Record) -> str:
    if record.operation in (34, 35, 36):
        enabled = [
            name for bit, name in GC_STATE_FLAGS
            if record.packed_value & (1 << bit)
        ]
        if not (record.packed_value & 1):
            enabled.insert(0, "collection_invalid")
        return ",".join(enabled)
    if record.operation == 37:
        return f"omitted_instances={record.packed_value}"
    if 38 <= record.operation <= 41:
        return (
            f"collection_index={record.packed_value & 0xffff},"
            f"collection_count={(record.packed_value >> 16) & 0xffff}"
        )
    if record.operation == 42:
        cause = record.packed_value & 0xff
        return f"release_cause={RELEASE_CAUSE_NAMES.get(cause, f'Unknown({cause})')}"
    if record.operation == 43:
        cause = record.packed_value & 0xff
        last_operation = (record.packed_value >> 8) & 0xff
        live_copies = (record.packed_value >> 16) & 0xff
        flags = []
        if record.packed_value & (1 << 24):
            flags.append("invalidated")
        if record.packed_value & (1 << 25):
            flags.append("submitted")
        flag_text = ",".join(flags) if flags else "none"
        return (
            f"release_cause={RELEASE_CAUSE_NAMES.get(cause, f'Unknown({cause})')},"
            f"binding_last_operation={OPERATION_NAMES.get(last_operation, f'Unknown({last_operation})')},"
            f"live_copies={live_copies},binding_flags={flag_text}"
        )
    if record.operation == 44:
        active_bindings = record.packed_value & 0x7fff
        omitted_bindings = (record.packed_value >> 15) & 0xffff
        tracking_enabled = bool(record.packed_value & (1 << 31))
        return (
            f"active_bindings={active_bindings},"
            f"omitted_bindings={omitted_bindings},"
            f"binding_tracking_enabled={str(tracking_enabled).lower()}"
        )
    if record.operation == 45:
        return (
            f"metadata_captured={str(bool(record.packed_value & 1)).lower()},"
            f"has_actor={str(bool(record.packed_value & (1 << 1))).lower()},"
            f"has_runtime_cell={str(bool(record.packed_value & (1 << 2))).lower()}"
        )
    if record.operation == 46:
        return (
            f"actor_data_layers={record.packed_value & 0xffff},"
            f"spatially_loaded={str(bool(record.packed_value & (1 << 16))).lower()},"
            f"has_runtime_cell={str(bool(record.packed_value & (1 << 17))).lower()}"
        )
    if record.operation == 48:
        return f"cell_data_layers={record.packed_value & 0xffff}"
    if record.operation == 49:
        return (
            f"data_layer_index={record.packed_value & 0xffff},"
            f"data_layer_count={(record.packed_value >> 16) & 0xffff}"
        )
    if record.operation == 52:
        if record.packed_value & (1 << 31):
            return (
                "omitted_contributor_descriptors="
                f"{record.packed_value & 0x7fffffff}"
            )
        return f"omitted_data_layers={record.packed_value}"
    if 53 <= record.operation <= 57:
        old_target = record.packed_value & 0x3
        new_target = (record.packed_value >> 2) & 0x3
        old_effective = (record.packed_value >> 4) & 0x3
        new_effective = (record.packed_value >> 6) & 0x3
        detail = (
            f"target={DATA_LAYER_STATE_NAMES.get(old_target, f'Unknown({old_target})')}"
            f"->{DATA_LAYER_STATE_NAMES.get(new_target, f'Unknown({new_target})')},"
            f"effective={DATA_LAYER_STATE_NAMES.get(old_effective, f'Unknown({old_effective})')}"
            f"->{DATA_LAYER_STATE_NAMES.get(new_effective, f'Unknown({new_effective})')},"
            f"recursive={str(bool(record.packed_value & (1 << 8))).lower()},"
            f"client_only={str(bool(record.packed_value & (1 << 9))).lower()},"
            f"server_only={str(bool(record.packed_value & (1 << 10))).lower()},"
            f"net_mode={(record.packed_value >> 11) & 0x7}"
        )
        if record.operation == 54:
            reason = (record.packed_value >> 16) & 0xf
            detail += (
                ",rejection="
                f"{DATA_LAYER_REJECTION_NAMES.get(reason, f'Unknown({reason})')}"
            )
        return detail
    if record.operation == 58:
        return (
            "omitted_data_layer_transitions="
            f"{record.packed_value & 0x7fffffff}"
        )
    if 59 <= record.operation <= 64:
        prior = record.packed_value & 0x3
        target = (record.packed_value >> 2) & 0x3
        observed = (record.packed_value >> 4) & 0x3
        action = (record.packed_value >> 6) & 0x7
        level_state = (record.packed_value >> 16) & 0xff
        block_reason = (record.packed_value >> 24) & 0xf
        flags = []
        if record.packed_value & (1 << 9):
            flags.append("always_loaded")
        if record.packed_value & (1 << 10):
            flags.append("spatially_loaded")
        if record.packed_value & (1 << 11):
            flags.append("has_data_layers")
        flag_text = ",".join(flags) if flags else "none"
        return (
            f"cell={DATA_LAYER_STATE_NAMES.get(prior, f'Unknown({prior})')}"
            f"->{DATA_LAYER_STATE_NAMES.get(target, f'Unknown({target})')},"
            f"observed={DATA_LAYER_STATE_NAMES.get(observed, f'Unknown({observed})')},"
            f"action={CELL_ACTION_NAMES.get(action, f'Unknown({action})')},"
            f"cell_flags={flag_text},"
            f"level_state={LEVEL_STREAMING_STATE_NAMES.get(level_state, f'Unknown({level_state})')},"
            f"blocked={CELL_BLOCK_REASON_NAMES.get(block_reason, f'Unknown({block_reason})')}"
        )
    if record.operation == 65:
        return (
            "omitted_cell_transitions="
            f"{record.packed_value & 0x7fffffff}"
        )
    if record.operation == 66:
        return (
            "reason=RemovedPrimitive,"
            f"cached_commands={record.packed_value & 0xffff},"
            f"static_meshes={(record.packed_value >> 16) & 0xffff}"
        )
    if record.operation == 67:
        return (
            f"cached_commands={record.packed_value & 0xffff},"
            f"mesh_relevances={(record.packed_value >> 16) & 0xffff}"
        )
    if record.operation == 68:
        binding_operation = record.packed_value & 0xff
        return (
            "binding_operation="
            f"{OPERATION_NAMES.get(binding_operation, f'Unknown({binding_operation})')}"
        )
    if record.operation == 69:
        return (
            f"remaining_cached_commands={record.packed_value & 0xffff},"
            f"remaining_static_meshes={(record.packed_value >> 16) & 0xffff}"
        )
    if record.operation == 70:
        return (
            "omitted_primitive_teardowns="
            f"{record.packed_value & 0x7fffffff}"
        )
    if 71 <= record.operation <= 73 or 77 <= record.operation <= 79:
        enabled = [
            name for bit, name in MPC_ASSET_STATE_FLAGS
            if record.packed_value & (1 << bit)
        ]
        return ",".join(enabled) if enabled else "no_asset_state_flags"
    if record.operation == 74:
        return f"reference_chains={record.packed_value}"
    if record.operation == 75:
        flags = []
        if record.packed_value & (1 << 28):
            flags.append("addref_saturated")
        if record.packed_value & (1 << 29):
            flags.append("release_saturated")
        if record.packed_value & (1 << 30):
            flags.append("final_release_saturated")
        flag_text = ",".join(flags) if flags else "none"
        return (
            f"addrefs={record.packed_value & 0xfff},"
            f"releases={(record.packed_value >> 12) & 0xfff},"
            f"final_releases={(record.packed_value >> 24) & 0xf},"
            f"census_flags={flag_text},last_cycles={record.correlation_id}"
        )
    if record.operation == 76:
        return f"omitted_reference_operations={record.packed_value}"
    return ""



REPORT_EVIDENCE_LIMIT = 12


def _append_report_evidence(bucket: list[Record], record: Record) -> None:
    key = (
        record.cycles,
        record.operation,
        record.resource_id,
        record.caller_address,
        record.correlation_id,
        record.text,
    )
    if any(
        (
            item.cycles,
            item.operation,
            item.resource_id,
            item.caller_address,
            item.correlation_id,
            item.text,
        ) == key
        for item in bucket
    ):
        return
    bucket.append(record)
    if len(bucket) > REPORT_EVIDENCE_LIMIT:
        del bucket[0]


def _report_event(record: Record, header: FileHeader) -> str:
    seconds = (record.cycles - header.start_cycles) * header.seconds_per_cycle
    detail = decode_packed_detail(record)
    suffix = "; ".join(part for part in (detail, sanitize_tsv(record.text)) if part)
    if suffix:
        suffix = f" — {suffix}"
    return (
        f"+{seconds:.6f}s {OPERATION_NAMES.get(record.operation, f'Unknown({record.operation})')} "
        f"thread={record.thread_id} caller=0x{record.caller_address:x} "
        f"correlation={record.correlation_id}{suffix}"
    )


def generate_responsibility_report(
    journal: pathlib.Path,
    resource_id: int,
    output_stream,
) -> int:
    buckets: dict[str, list[Record]] = {
        "root": [],
        "instance": [],
        "release": [],
        "binding": [],
        "command": [],
        "coverage": [],
    }
    operation_counts: dict[int, int] = {}
    binding_ids: set[int] = set()
    contributor_ids: set[int] = set()
    teardown_ids: set[int] = set()
    cell_addresses: set[int] = set()
    data_layer_addresses: set[int] = set()
    target_records: list[Record] = []
    total = 0

    with journal.open("rb") as stream:
        header = read_file_header(stream)
        for record in iter_records(stream):
            total += 1
            if record.resource_id != resource_id:
                continue

            operation_counts[record.operation] = operation_counts.get(record.operation, 0) + 1
            if record.operation in {
                3, 10, 11, 12, 19, 26, 36, 43, 72, 73, 75, 79
            }:
                target_records.append(record)
                if len(target_records) > 4096:
                    del target_records[:2048]

            if record.operation in (34, 35, 36, 37, 71, 72, 73, 74, 77, 78, 79):
                _append_report_evidence(buckets["root"], record)
            if record.operation in (38, 39, 40, 41, 42, 43, 44, 72, 73, 79):
                _append_report_evidence(buckets["instance"], record)
            if record.operation in (3, 4, 5, 6, 7, 8, 9, 10, 20, 21, 42, 75, 76):
                _append_report_evidence(buckets["release"], record)
            if record.operation in (18, 19, 23, 24, 25, 26, 27, 28, 29, 43, 44, 51):
                _append_report_evidence(buckets["binding"], record)
            if record.operation in (11, 12, 22, 30, 31, 32, 33):
                _append_report_evidence(buckets["command"], record)
            if record.operation in (37, 44, 52, 58, 65, 70, 76):
                _append_report_evidence(buckets["coverage"], record)

            if 23 <= record.operation <= 29 or record.operation in (43, 51):
                if record.correlation_id:
                    binding_ids.add(record.correlation_id)
            if record.operation == 51 and record.caller_address:
                contributor_ids.add(record.caller_address)

    if not target_records:
        print(f"# RHI Responsibility Report — resource {resource_id}", file=output_stream)
        print("", file=output_stream)
        print("No records matched this resource ID.", file=output_stream)
        return 2

    # Resolve cached bindings into primitive contributors and teardown correlations.
    with journal.open("rb") as stream:
        read_file_header(stream)
        for record in iter_records(stream):
            related_binding = (
                (23 <= record.operation <= 29 or record.operation in (43, 51))
                and record.correlation_id in binding_ids
            )
            teardown_binding = (
                record.operation == 68 and record.caller_address in binding_ids
            )
            if related_binding or teardown_binding:
                _append_report_evidence(buckets["binding"], record)
                if record.operation == 51 and record.caller_address:
                    contributor_ids.add(record.caller_address)
                if record.operation == 68:
                    if record.resource_id:
                        contributor_ids.add(record.resource_id)
                    if record.correlation_id:
                        teardown_ids.add(record.correlation_id)

    # Resolve contributor metadata and the primitive teardown that touched the binding.
    with journal.open("rb") as stream:
        read_file_header(stream)
        for record in iter_records(stream):
            related_contributor = (
                (45 <= record.operation <= 50 or record.operation == 52)
                and record.correlation_id in contributor_ids
            )
            related_teardown = (
                66 <= record.operation <= 70
                and (
                    record.resource_id in contributor_ids
                    or record.correlation_id in teardown_ids
                )
            )
            if related_contributor or related_teardown:
                _append_report_evidence(buckets["binding"], record)
                if record.operation == 48 and record.resource_address:
                    cell_addresses.add(record.resource_address)
                if record.operation == 49 and record.resource_address:
                    data_layer_addresses.add(record.resource_address)
                if record.operation in (52, 70):
                    _append_report_evidence(buckets["coverage"], record)

    # Resolve the Data Layer and runtime-cell transitions for those contributors.
    with journal.open("rb") as stream:
        read_file_header(stream)
        for record in iter_records(stream):
            related_data_layer = (
                53 <= record.operation <= 58
                and (
                    record.resource_address in data_layer_addresses
                    or record.operation == 58
                )
            )
            related_cell = (
                59 <= record.operation <= 65
                and (
                    record.resource_address in cell_addresses
                    or record.operation == 65
                )
            )
            if related_data_layer or related_cell:
                _append_report_evidence(buckets["instance"], record)
                if record.operation in (58, 65):
                    _append_report_evidence(buckets["coverage"], record)

    final_release_cycles = [
        record.cycles for record in target_records if record.operation == 3
    ]
    physical_free_cycles = [
        record.cycles for record in target_records if record.operation == 10
    ]
    death_cycle = min(final_release_cycles or physical_free_cycles or [0])
    stale_after_release = [
        record
        for record in target_records
        if death_cycle
        and record.cycles > death_cycle
        and record.operation in (11, 12, 19, 26)
    ]
    live_binding_snapshots = [
        record
        for record in target_records
        if record.operation == 43 and ((record.packed_value >> 16) & 0xff) > 0
    ]
    asset_teardown = [
        record for record in target_records if record.operation in (36, 72, 73, 79)
    ]
    has_census = operation_counts.get(75, 0) > 0

    if stale_after_release and final_release_cycles:
        confidence = "High"
        verdict = (
            "A cached binding or queued command retained this MPC uniform-buffer "
            "generation and used/submitted it after FinalRelease."
        )
    elif live_binding_snapshots and (final_release_cycles or physical_free_cycles):
        confidence = "Medium"
        verdict = (
            "The MPC generation entered RHI teardown while cached binding copies "
            "were still live; this journal does not contain a confirmed post-release use."
        )
    elif asset_teardown and (final_release_cycles or physical_free_cycles):
        confidence = "Medium"
        verdict = (
            "MPC asset/GC teardown causally preceded the RHI generation release, "
            "but stale-holder or post-release command evidence is incomplete."
        )
    elif final_release_cycles or physical_free_cycles:
        confidence = "Low"
        verdict = (
            "The final RHI release is present, but the journal does not establish "
            "which higher-level owner or stale binding caused the crash."
        )
    else:
        confidence = "Low"
        verdict = "No final release for this resource generation was found."

    gaps: list[str] = []
    if not buckets["root"]:
        gaps.append("No targeted GC/asset lifecycle evidence for this generation.")
    if not has_census:
        gaps.append("No persistent AddRef/Release callsite census reached FinalRelease.")
    if not binding_ids:
        gaps.append("No cached-binding ID was linked to this generation.")
    if not buckets["command"]:
        gaps.append("No command enqueue/execute causal rows were recorded; verify CommandUses=1.")
    for record in buckets["coverage"]:
        gaps.append(_report_event(record, header))
    if not gaps:
        gaps.append("No explicit bounded-capture omissions were recorded.")

    release_callers = sorted(
        {
            record.caller_address
            for record in target_records
            if record.operation in (3, 10, 42) and record.caller_address
        }
    )
    census_callers = sorted(
        {
            record.caller_address
            for record in target_records
            if record.operation == 75 and record.caller_address
        }
    )

    print(f"# RHI Responsibility Report — resource {resource_id}", file=output_stream)
    print("", file=output_stream)
    print(f"- Confidence: **{confidence}**", file=output_stream)
    print(f"- Verdict: {verdict}", file=output_stream)
    print(f"- Journal records scanned: {total}", file=output_stream)
    print(f"- Cached binding IDs: {', '.join(map(str, sorted(binding_ids))) or 'none'}", file=output_stream)
    print(f"- Primitive contributor IDs: {', '.join(map(str, sorted(contributor_ids))) or 'none'}", file=output_stream)
    print(
        "- Final-release callers: "
        + (", ".join(f"0x{caller:x}" for caller in release_callers) or "none"),
        file=output_stream,
    )
    print(
        "- Reference-census callsites: "
        + (", ".join(f"0x{caller:x}" for caller in census_callers) or "none"),
        file=output_stream,
    )

    sections = (
        ("1. Asset reachability / root loss", "root"),
        ("2. World instance and scene-map teardown", "instance"),
        ("3. Final RHI release and ref census", "release"),
        ("4. Stale binding and primitive contributors", "binding"),
        ("5. Command submission / execution", "command"),
    )
    for title, key in sections:
        print("", file=output_stream)
        print(f"## {title}", file=output_stream)
        print("", file=output_stream)
        evidence = buckets[key]
        if not evidence:
            print("- No matching evidence.", file=output_stream)
        else:
            for record in sorted(evidence, key=lambda item: item.cycles):
                print(f"- {_report_event(record, header)}", file=output_stream)

    print("", file=output_stream)
    print("## Coverage gaps", file=output_stream)
    print("", file=output_stream)
    for gap in gaps:
        print(f"- {gap}", file=output_stream)
    return 0


def record_matches(
    record: Record,
    resource_id: Optional[int],
    resource_address: Optional[int],
    flags_address: Optional[int],
    text_contains: Optional[str],
    causal_id: Optional[int],
    scene_refresh_id: Optional[int],
    binding_id: Optional[int],
    contributor_id: Optional[int],
    data_layer_transition_id: Optional[int],
    cell_transition_id: Optional[int],
    primitive_teardown_id: Optional[int],
) -> bool:
    if resource_id is not None and record.resource_id != resource_id:
        return False
    if resource_address is not None and record.resource_address != resource_address:
        return False
    if flags_address is not None and record.flags_address != flags_address:
        return False
    if text_contains is not None and text_contains.casefold() not in record.text.casefold():
        return False
    if causal_id is not None:
        record_causal_id = (
            record.caller_address
            if record.operation == 32
            else record.correlation_id
            if record.operation in (30, 31, 33)
            else 0
        )
        if record_causal_id != causal_id:
            return False
    if scene_refresh_id is not None:
        record_scene_refresh_id = (
            record.correlation_id if 38 <= record.operation <= 41 else 0
        )
        if record_scene_refresh_id != scene_refresh_id:
            return False
    if binding_id is not None:
        record_binding_id = (
            record.caller_address
            if record.operation == 68
            else record.correlation_id
            if 23 <= record.operation <= 29
            or record.operation in (43, 51)
            else 0
        )
        if record_binding_id != binding_id:
            return False
    if contributor_id is not None:
        record_contributor_id = (
            record.resource_id
            if 66 <= record.operation <= 69
            else record.caller_address
            if record.operation == 51
            else record.correlation_id
            if 45 <= record.operation <= 50 or record.operation == 52
            else 0
        )
        if record_contributor_id != contributor_id:
            return False
    if data_layer_transition_id is not None:
        record_transition_id = (
            record.correlation_id if 53 <= record.operation <= 57 else 0
        )
        if record_transition_id != data_layer_transition_id:
            return False
    if cell_transition_id is not None:
        record_transition_id = (
            record.correlation_id if 59 <= record.operation <= 64 else 0
        )
        if record_transition_id != cell_transition_id:
            return False
    if primitive_teardown_id is not None:
        record_teardown_id = (
            record.correlation_id if 66 <= record.operation <= 69 else 0
        )
        if record_teardown_id != primitive_teardown_id:
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("journal", type=pathlib.Path)
    parser.add_argument("--id", type=parse_integer, dest="resource_id")
    parser.add_argument("--address", type=parse_integer, dest="resource_address")
    parser.add_argument("--flags", type=parse_integer, dest="flags_address")
    parser.add_argument(
        "--causal",
        type=parse_integer,
        dest="causal_id",
        help="Emit the cross-thread MPC request/execute/resource chain for one causal id.",
    )
    parser.add_argument(
        "--contains",
        dest="text_contains",
        help="Only emit records whose text contains this value (case-insensitive). "
        "Use this to discover an MPC generation id from its owner path, then decode by --id.",
    )
    parser.add_argument(
        "--scene-refresh",
        type=parse_integer,
        dest="scene_refresh_id",
        help="Emit scene MPC-map mutations sharing one refresh correlation id.",
    )
    parser.add_argument(
        "--binding",
        type=parse_integer,
        dest="binding_id",
        help="Emit cached-binding lifecycle, release snapshot, and contributor-link rows for one binding id.",
    )
    parser.add_argument(
        "--contributor",
        type=parse_integer,
        dest="contributor_id",
        help="Emit primitive contributor metadata and cached-binding links for one contributor id.",
    )
    parser.add_argument(
        "--data-layer-transition",
        type=parse_integer,
        dest="data_layer_transition_id",
        help="Emit request, outcome, and effective-state rows for one Data Layer transition id.",
    )
    parser.add_argument(
        "--cell-transition",
        type=parse_integer,
        dest="cell_transition_id",
        help="Emit request, progress, completion, and supersession rows for one runtime-cell transition id.",
    )
    parser.add_argument(
        "--primitive-teardown",
        type=parse_integer,
        dest="primitive_teardown_id",
        help="Emit primitive removal, cache removal, and binding invalidation/release bridge rows for one teardown id.",
    )
    parser.add_argument(
        "--responsibility-report",
        action="store_true",
        help="Join the MPC lifecycle, release, binding, contributor, World Partition, and command evidence into a Markdown verdict. Requires --id.",
    )
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    if args.responsibility_report and args.resource_id is None:
        parser.error("--responsibility-report requires --id")

    output_stream = (
        args.output.open("w", encoding="utf-8", newline="")
        if args.output
        else sys.stdout
    )

    if args.responsibility_report:
        try:
            return generate_responsibility_report(
                args.journal,
                args.resource_id,
                output_stream,
            )
        finally:
            if args.output:
                output_stream.close()

    matched = 0
    total = 0
    try:
        with args.journal.open("rb") as stream:
            header = read_file_header(stream)
            print(
                "# version={} pointer_size={} tchar_size={} queue_capacity={} "
                "identity_capacity={} max_file_bytes={}".format(
                    header.version,
                    header.pointer_size,
                    header.tchar_size,
                    header.queue_capacity,
                    header.identity_capacity,
                    header.maximum_file_bytes,
                ),
                file=output_stream,
            )
            print(
                "seconds\tcycles\tkind\toperation\tid\tresource\tflags_address"
                "\ttype\ttype_name\tthread\tpacked\tcaller\tcorrelation"
                "\tbinding_id\tcausal_id\tparent_correlation"
                "\tscene_refresh_id\tcontributor_id\tdata_layer_transition_id"
                "\tcell_transition_id\tprimitive_teardown_id"
                "\towner_key\trecord_flags\tdetail\ttext",
                file=output_stream,
            )

            for record in iter_records(stream):
                total += 1
                if not record_matches(
                    record,
                    args.resource_id,
                    args.resource_address,
                    args.flags_address,
                    args.text_contains,
                    args.causal_id,
                    args.scene_refresh_id,
                    args.binding_id,
                    args.contributor_id,
                    args.data_layer_transition_id,
                    args.cell_transition_id,
                    args.primitive_teardown_id,
                ):
                    continue

                matched += 1
                seconds = (
                    (record.cycles - header.start_cycles)
                    * header.seconds_per_cycle
                )
                owner_key = 0
                if record.operation == 20:  # AccessOwner stores its key as correlation.
                    owner_key = record.correlation_id
                elif record.operation == 22:  # CommandOwner stores its key as caller.
                    owner_key = record.caller_address
                elif record.operation == 28:  # BindingOwner stores its key as caller.
                    owner_key = record.caller_address
                binding_id = (
                    record.caller_address
                    if record.operation == 68
                    else record.correlation_id
                    if 23 <= record.operation <= 29
                    or record.operation in (43, 51)
                    else 0
                )
                contributor_id = (
                    record.resource_id
                    if 66 <= record.operation <= 69
                    else record.caller_address
                    if record.operation == 51
                    else record.correlation_id
                    if 45 <= record.operation <= 50 or record.operation == 52
                    else 0
                )
                data_layer_transition_id = (
                    record.correlation_id
                    if 53 <= record.operation <= 57
                    else 0
                )
                cell_transition_id = (
                    record.correlation_id
                    if 59 <= record.operation <= 64
                    else 0
                )
                primitive_teardown_id = (
                    record.correlation_id
                    if 66 <= record.operation <= 69
                    else 0
                )
                causal_id = 0
                parent_correlation = 0
                if record.operation in (30, 31, 33):
                    causal_id = record.correlation_id
                elif record.operation == 32:
                    causal_id = record.caller_address
                    parent_correlation = record.correlation_id
                scene_refresh_id = (
                    record.correlation_id
                    if 38 <= record.operation <= 41
                    else 0
                )
                fields = [
                    f"{seconds:.9f}",
                    str(record.cycles),
                    KIND_NAMES.get(record.kind, f"Unknown({record.kind})"),
                    OPERATION_NAMES.get(
                        record.operation, f"Unknown({record.operation})"
                    ),
                    str(record.resource_id),
                    f"0x{record.resource_address:x}",
                    f"0x{record.flags_address:x}",
                    str(record.resource_type),
                    RESOURCE_TYPE_NAMES.get(
                        record.resource_type,
                        f"Unknown({record.resource_type})",
                    ),
                    str(record.thread_id),
                    f"0x{record.packed_value:08x}",
                    f"0x{record.caller_address:x}",
                    str(record.correlation_id),
                    str(binding_id),
                    str(causal_id),
                    str(parent_correlation),
                    str(scene_refresh_id),
                    str(contributor_id),
                    str(data_layer_transition_id),
                    str(cell_transition_id),
                    str(primitive_teardown_id),
                    f"0x{owner_key:x}",
                    f"0x{record.flags:04x}",
                    decode_packed_detail(record),
                    sanitize_tsv(record.text),
                ]
                print("\t".join(fields), file=output_stream)
    finally:
        if args.output:
            output_stream.close()

    print(
        f"Decoded {total} records; matched {matched}.",
        file=sys.stderr,
    )
    if matched == 0:
        print(
            "No records matched. Check the crash log's queue_drops, disk_cap_drops, "
            "write_failures, and disabled_or_start_failure_drops counters.",
            file=sys.stderr,
        )
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
