#!/usr/bin/env python3
import pathlib
import struct
import sys

if len(sys.argv) != 4:
    raise SystemExit("usage: embed_stage1_spirv.py <vert.spv> <frag.spv> <out.hpp>")

vert_path = pathlib.Path(sys.argv[1])
frag_path = pathlib.Path(sys.argv[2])
out_path = pathlib.Path(sys.argv[3])


def words(path: pathlib.Path):
    data = path.read_bytes()
    if len(data) % 4 != 0:
        raise RuntimeError(f"SPIR-V size is not word-aligned: {path}")
    return struct.unpack(f"<{len(data)//4}I", data)


def emit(name: str, values):
    body = ",\n    ".join(f"0x{value:08x}u" for value in values)
    return f"inline constexpr std::uint32_t {name}[] = {{\n    {body}\n}};\n"

out_path.parent.mkdir(parents=True, exist_ok=True)
out_path.write_text(
    "#pragma once\n#include <cstdint>\n\nnamespace vortex::android::stage1_shaders {\n\n"
    + emit("kVertexSpirv", words(vert_path))
    + "\n"
    + emit("kFragmentSpirv", words(frag_path))
    + "\n} // namespace vortex::android::stage1_shaders\n",
    encoding="utf-8",
)
