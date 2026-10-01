#!/usr/bin/env python3
"""Retain real Vulkan allocation-only A/A, ABBA and BAAB measurements.

Uses the maintained GPU unit harness and an exact reference display source.
This is a host resource microbenchmark, not xemu/game qualification.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import statistics
import subprocess


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(args, cwd, log):
    with log.open("wb") as stream:
        subprocess.run(args, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--reference-ref", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--icd", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    source = build.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    run(["ninja", "tests/unit/test-xbox-vk-pvideo-gpu"], build,
        output / "candidate-build.log")
    reference = subprocess.check_output(
        ["git", "rev-parse", args.reference_ref], cwd=source, text=True
    ).strip()
    display_path = "hw/xbox/nv2a/pgraph/vk/display.c"
    reference_source = output / "reference-display.c"
    reference_source.write_bytes(subprocess.check_output(
        ["git", "show", f"{reference}:{display_path}"], cwd=source
    ))
    records = json.loads((build / "compile_commands.json").read_text())
    record = next(x for x in records if x["file"].endswith("test-xbox-vk-pvideo-gpu.c"))
    compile_args = shlex.split(record["command"])
    if "-MD" in compile_args:
        compile_args.remove("-MD")
    for flag in ("-MF", "-MQ"):
        if flag in compile_args:
            offset = compile_args.index(flag)
            del compile_args[offset:offset + 2]
    offset = compile_args.index("-o")
    original_object = compile_args[offset + 1]
    reference_object = output / "reference.o"
    compile_args[offset + 1] = str(reference_object)
    compile_args += [
        "-I" + str(source / "hw/xbox/nv2a/pgraph/vk"),
        '-DXEMU_PVIDEO_DISPLAY_SOURCE="' + str(reference_source) + '"',
    ]
    commands = subprocess.check_output(
        ["ninja", "-t", "commands", "tests/unit/test-xbox-vk-pvideo-gpu"],
        cwd=build, text=True,
    ).splitlines()
    link_args = shlex.split(commands[-1])
    link_args = [str(reference_object) if x == original_object else x for x in link_args]
    reference_exe = output / "pvideo-reference"
    link_args[link_args.index("-o") + 1] = str(reference_exe)
    run(compile_args, build, output / "reference-compile.log")
    run(link_args, build, output / "reference-link.log")
    candidate_exe = build / "tests/unit/test-xbox-vk-pvideo-gpu"
    cpus = sorted(os.sched_getaffinity(0))
    cpu = cpus[0]
    os.sched_setaffinity(0, {cpu})
    manifest = {
        "scope": "allocation-only host microbenchmark; no uploads, shaders or gameplay",
        "reference_commit": reference,
        "candidate_head_before_measurement": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=source, text=True
        ).strip(),
        "candidate_source_sha256": {
            p: sha(source / p) for p in (
                display_path, "hw/xbox/nv2a/pgraph/vk/renderer.h",
                "tests/unit/test-xbox-vk-pvideo-gpu.c",
            )
        },
        "reference_source_sha256": sha(reference_source),
        "reference_executable_sha256": sha(reference_exe),
        "candidate_executable_sha256": sha(candidate_exe),
        "reference_uses": "exact reference display.c, shared harness/current struct headers and toolchain",
        "cwd": str(build), "reference_compile": compile_args, "reference_link": link_args,
        "platform": platform.platform(), "cpu_affinity": [cpu],
        "icd": str(args.icd.resolve()), "icd_sha256": sha(args.icd.resolve()),
        "plan": {"aa": "AAAA", "abba": "ABBA", "baab": "BAAB"},
        "A": "reference", "B": "candidate", "requests_per_batch": 10001,
        "clock": "g_get_monotonic_time, microseconds, one warmup create/destroy outside batch",
        "results": [],
    }
    manifest_path = output / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    for phase, order in manifest["plan"].items():
        for ordinal, variant in enumerate(order, 1):
            label = f"{phase}-{ordinal}-{variant}"
            cache = output / label / "cache"
            cache.mkdir(parents=True)
            environment = dict(os.environ, XEMU_TEST_VK_PVIDEO="1",
                               VK_DRIVER_FILES=str(args.icd.resolve()),
                               XDG_CACHE_HOME=str(cache),
                               MESA_SHADER_CACHE_DIR=str(cache / "mesa"))
            executable = reference_exe if variant == "A" else candidate_exe
            result = subprocess.run([str(executable), "--benchmark"], env=environment, cwd=build,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (output / label / "stdout.log").write_bytes(result.stdout)
            fields = dict(line[2:].split("=", 1) for line in result.stdout.decode().splitlines()
                          if line.startswith("# ") and "=" in line)
            item = {"phase": phase, "ordinal": ordinal, "variant": variant,
                    "exit_code": result.returncode, "fields": fields,
                    "stdout_sha256": hashlib.sha256(result.stdout).hexdigest(),
                    "cache_files_before": [],
                    "cache_files_after": [
                        {"path": str(x.relative_to(cache)), "bytes": x.stat().st_size,
                         "sha256": sha(x)} for x in sorted(cache.rglob("*")) if x.is_file()
                    ]}
            item["cache_file_count"] = len(item["cache_files_after"])
            manifest["results"].append(item)
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
            if result.returncode:
                raise RuntimeError(f"{label} failed; logs and partial manifest retained")
            assert int(fields["requests"]) == 10001
            assert int(fields["image_creates"]) == (10001 if variant == "A" else 1)
            assert fields["live_allocations_after_teardown"] == "0"
    summary = {}
    for phase in ("abba", "baab"):
        values = {v: [float(x["fields"]["ns_per_request"]) for x in manifest["results"]
                      if x["phase"] == phase and x["variant"] == v] for v in ("A", "B")}
        a, b = (statistics.median(values[v]) for v in ("A", "B"))
        summary[phase] = {"reference_ns_per_request": a, "candidate_ns_per_request": b,
                          "time_saved_ns": a - b, "improvement_percent": 100 * (a - b) / a}
    aa = [float(x["fields"]["ns_per_request"]) for x in manifest["results"] if x["phase"] == "aa"]
    summary["aa_reference_range_ns"] = [min(aa), max(aa)]
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
