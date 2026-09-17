"""Export the trained estimator as a TorchScript model for LibTorch inference."""

from __future__ import annotations

import argparse
from pathlib import Path

import torch

from model import ParameterEstimator
from schema import load_schema


CLIP_SAMPLES = 16384


class InferenceModel(torch.nn.Module):
    def __init__(self, model: ParameterEstimator, names: list[str]) -> None:
        super().__init__()
        self.model = model
        self.names = names

    def forward(self, audio: torch.Tensor) -> tuple[torch.Tensor, ...]:
        outputs = self.model(audio)
        return tuple(self.model.heads[name].decode(outputs[name]) for name in self.names)


def main() -> None:
    parser = argparse.ArgumentParser(description="Export the AdbSynth estimator to TorchScript.")
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    specs = load_schema(args.schema)
    model = ParameterEstimator(specs)
    checkpoint = torch.load(args.checkpoint, map_location="cpu", weights_only=True)
    model.load_state_dict(checkpoint["state_dict"])
    model.eval()

    names = [spec.id for spec in specs]
    inference = InferenceModel(model, names).eval()
    example = torch.zeros(1, CLIP_SAMPLES)
    scripted = torch.jit.trace(inference, example)
    scripted.save(str(args.output))

    with torch.no_grad():
        scripted(example)
    print(f"TorchScript model written to {args.output}")


if __name__ == "__main__":
    main()