"""Generates a training dataset by driving the C++ offline renderer."""

from __future__ import annotations

import argparse
import json
import random
import subprocess
from pathlib import Path

from schema import export_schema

DEFAULT_SAMPLE_RATE = 44100


def build_manifest(specs, count: int, seed: int, duration: float, sample_rate: int) -> list[dict]:
    rng = random.Random(seed)
    patches = []

    for index in range(count):
        patch = {
            "id": f"{index:07d}",
            "phase": rng.uniform(0.0, 6.283185307179586),
            "duration": duration,
            "sample_rate": sample_rate,
            "channels": 1,
        }
        for spec in specs:
            patch[spec.id] = spec.sample(rng)
        patches.append(patch)

    return patches


def main() -> None:
    parser = argparse.ArgumentParser(description="Render an AdbSynth training dataset.")
    parser.add_argument("--renderer", type=Path, default=Path("build/AdbSynthRender"))
    parser.add_argument("--outdir", type=Path, required=True)
    parser.add_argument("--count", type=int, default=20000)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--duration", type=float, default=0.5)
    parser.add_argument("--sample-rate", type=int, default=DEFAULT_SAMPLE_RATE)
    args = parser.parse_args()

    args.outdir.mkdir(parents=True, exist_ok=True)
    specs = export_schema(args.renderer, args.outdir / "schema.json")

    manifest_path = args.outdir / "patches.jsonl"
    patches = build_manifest(specs, args.count, args.seed, args.duration, args.sample_rate)

    with manifest_path.open("w") as stream:
        for patch in patches:
            stream.write(json.dumps(patch) + "\n")

    subprocess.run(
        [str(args.renderer), "--manifest", str(manifest_path), "--outdir", str(args.outdir)],
        check=True,
    )

    print(f"{len(patches)} clips in {args.outdir}")


if __name__ == "__main__":
    main()
