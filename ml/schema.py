"""Loads the parameter schema exported by AdbSynthRender.

Nothing here hardcodes parameter names or ranges: adding a parameter to
Source/ParameterSchema.h is enough for the generator, the model heads and the
metrics to pick it up.
"""

from __future__ import annotations

import json
import math
import random
import subprocess
from dataclasses import dataclass
from pathlib import Path

# Render-only fields that describe the recording conditions rather than the patch.
NUISANCE_FIELDS = ("id", "file", "phase", "duration", "sample_rate", "channels")
OSCILLATOR_FREQUENCY_IDS = ("frequency", "frequency2")


@dataclass(frozen=True)
class ParameterSpec:
    id: str
    label: str
    unit: str
    kind: str
    min: float
    max: float
    log_scale: bool
    default: float
    choices: list[str]

    def sample(self, rng: random.Random) -> float:
        if self.kind == "choice":
            return float(rng.randrange(max(1, len(self.choices))))
        if self.kind == "bool":
            return float(rng.random() < 0.5)
        if self.log_scale:
            return math.exp(rng.uniform(math.log(self.min), math.log(self.max)))
        return rng.uniform(self.min, self.max)


def canonicalize_oscillator_frequencies(parameters: dict) -> dict:
    """Use low/high ordering because the mixed audio has no oscillator identity."""
    first_id, second_id = OSCILLATOR_FREQUENCY_IDS
    if first_id in parameters and second_id in parameters:
        parameters[first_id], parameters[second_id] = sorted(
            (parameters[first_id], parameters[second_id])
        )
    return parameters


def load_schema(path: Path) -> list[ParameterSpec]:
    payload = json.loads(Path(path).read_text())
    return [
        ParameterSpec(
            id=entry["id"],
            label=entry["label"],
            unit=entry["unit"],
            kind=entry["kind"],
            min=float(entry["min"]),
            max=float(entry["max"]),
            log_scale=bool(entry["log_scale"]),
            default=float(entry["default"]),
            choices=list(entry.get("choices", [])),
        )
        for entry in payload["parameters"]
    ]


def export_schema(renderer: Path, path: Path) -> list[ParameterSpec]:
    """Runs the C++ renderer so the Python side can never drift from the plug-in."""
    path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [str(renderer), "--dump-schema"], check=True, capture_output=True, text=True
    )
    path.write_text(result.stdout)
    return load_schema(path)
