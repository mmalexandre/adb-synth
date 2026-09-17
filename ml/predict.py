"""Estimates the synth parameters behind an audio file."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import soundfile as sf
import torch

from features import CLIP_SAMPLES
from model import ParameterEstimator
from schema import load_schema


def load_clip(path: Path, clip_samples: int = CLIP_SAMPLES) -> torch.Tensor:
    audio, _ = sf.read(path, dtype="float32", always_2d=True)
    audio = audio[:, 0]

    if len(audio) < clip_samples:
        audio = np.pad(audio, (0, clip_samples - len(audio)))

    start = (len(audio) - clip_samples) // 2
    return torch.from_numpy(np.ascontiguousarray(audio[start : start + clip_samples])).unsqueeze(0)


def main() -> None:
    parser = argparse.ArgumentParser(description="Predict AdbSynth parameters from audio.")
    parser.add_argument("audio", type=Path)
    parser.add_argument("--checkpoint", type=Path, default=Path("ml/checkpoints/model.pt"))
    parser.add_argument("--schema", type=Path, required=True)
    args = parser.parse_args()

    specs = load_schema(args.schema)
    model = ParameterEstimator(specs)
    checkpoint = torch.load(args.checkpoint, map_location="cpu", weights_only=True)
    model.load_state_dict(checkpoint["state_dict"])
    model.eval()

    with torch.no_grad():
        decoded = model.decode(model(load_clip(args.audio)))

    prediction = {}
    for spec in specs:
        value = float(decoded[spec.id][0])
        prediction[spec.id] = spec.choices[int(value)] if spec.kind == "choice" else value

    print(json.dumps(prediction, indent=2))


if __name__ == "__main__":
    main()
