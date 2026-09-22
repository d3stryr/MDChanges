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
}

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


def record_matches(
    record: Record,
    resource_id: Optional[int],
    resource_address: Optional[int],
    flags_address: Optional[int],
) -> bool:
    if resource_id is not None and record.resource_id != resource_id:
        return False
    if resource_address is not None and record.resource_address != resource_address:
        return False
    if flags_address is not None and record.flags_address != flags_address:
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("journal", type=pathlib.Path)
    parser.add_argument("--id", type=parse_integer, dest="resource_id")
    parser.add_argument("--address", type=parse_integer, dest="resource_address")
    parser.add_argument("--flags", type=parse_integer, dest="flags_address")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    output_stream = (
        args.output.open("w", encoding="utf-8", newline="")
        if args.output
        else sys.stdout
    )

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
                "\trecord_flags\ttext",
                file=output_stream,
            )

            for record in iter_records(stream):
                total += 1
                if not record_matches(
                    record,
                    args.resource_id,
                    args.resource_address,
                    args.flags_address,
                ):
                    continue

                matched += 1
                seconds = (
                    (record.cycles - header.start_cycles)
                    * header.seconds_per_cycle
                )
                print(
                    "{:.9f}\t{}\t{}\t{}\t{}\t0x{:x}\t0x{:x}\t{}\t{}"
                    "\t{}\t0x{:08x}\t0x{:x}\t{}\t0x{:04x}\t{}".format(
                        seconds,
                        record.cycles,
                        KIND_NAMES.get(record.kind, f"Unknown({record.kind})"),
                        OPERATION_NAMES.get(
                            record.operation, f"Unknown({record.operation})"
                        ),
                        record.resource_id,
                        record.resource_address,
                        record.flags_address,
                        record.resource_type,
                        RESOURCE_TYPE_NAMES.get(
                            record.resource_type,
                            f"Unknown({record.resource_type})",
                        ),
                        record.thread_id,
                        record.packed_value,
                        record.caller_address,
                        record.correlation_id,
                        record.flags,
                        sanitize_tsv(record.text),
                    ),
                    file=output_stream,
                )
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
