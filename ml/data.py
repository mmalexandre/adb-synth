"""Dataset over the rendered clips, with augmentation applied on the fly.

The renderer emits clean, dry audio; every nuisance variable is added here so the augmentation
policy can be changed without recompiling or re-rendering.

NOTE: if a future schema parameter controls output level, remove the gain augmentation below or the
model will be trained against a corrupted label.
"""

from __future__ import annotations

import json
import math
import random
from pathlib import Path

import numpy as np
import soundfile as sf
import torch
from torch.utils.data import Dataset

from features import CLIP_SAMPLES
from schema import ParameterSpec

GAIN_RANGE_DB = (-24.0, 0.0)
SNR_RANGE_DB = (10.0, 60.0)
FADE_SAMPLES = 256


class RenderedClips(Dataset):
    def __init__(
        self,
        root: Path,
        specs: list[ParameterSpec],
        augment: bool = True,
        clip_samples: int = CLIP_SAMPLES,
        entries: list[dict] | None = None,
    ) -> None:
        self.root = Path(root)
        self.specs = specs
        self.augment = augment
        self.clip_samples = clip_samples

        if entries is None:
            with (self.root / "labels.jsonl").open() as stream:
                entries = [json.loads(line) for line in stream if line.strip()]

        self.entries = entries

    def __len__(self) -> int:
        return len(self.entries)

    def _augment(self, audio: np.ndarray, rng: random.Random) -> np.ndarray:
        audio = audio * (10.0 ** (rng.uniform(*GAIN_RANGE_DB) / 20.0))

        signal_power = float(np.mean(audio**2))
        if signal_power > 0.0:
            snr = rng.uniform(*SNR_RANGE_DB)
            noise_power = signal_power / (10.0 ** (snr / 10.0))
            audio = audio + np.random.default_rng(rng.getrandbits(32)).normal(
                0.0, math.sqrt(noise_power), size=audio.shape
            ).astype(np.float32)

        fade = np.ones_like(audio)
        ramp = np.linspace(0.0, 1.0, FADE_SAMPLES, dtype=np.float32)
        fade[:FADE_SAMPLES] = ramp
        fade[-FADE_SAMPLES:] = ramp[::-1]
        return audio * fade

    def __getitem__(self, index: int) -> tuple[torch.Tensor, dict[str, torch.Tensor]]:
        entry = self.entries[index]
        audio, _ = sf.read(self.root / entry["file"], dtype="float32", always_2d=True)
        audio = audio[:, 0]

        rng = random.Random(index if not self.augment else random.randrange(1 << 30))

        if len(audio) < self.clip_samples:
            audio = np.pad(audio, (0, self.clip_samples - len(audio)))

        offset = rng.randrange(len(audio) - self.clip_samples + 1) if self.augment else 0
        audio = audio[offset : offset + self.clip_samples]

        if self.augment:
            audio = self._augment(audio, rng)

        targets = {
            spec.id: torch.tensor(float(entry[spec.id]), dtype=torch.float32) for spec in self.specs
        }
        return torch.from_numpy(np.ascontiguousarray(audio)), targets


def split(root: Path, specs: list[ParameterSpec], validation_fraction: float = 0.1, seed: int = 0):
    with (Path(root) / "labels.jsonl").open() as stream:
        entries = [json.loads(line) for line in stream if line.strip()]

    random.Random(seed).shuffle(entries)
    cut = max(1, int(len(entries) * validation_fraction))

    return (
        RenderedClips(root, specs, augment=True, entries=entries[cut:]),
        RenderedClips(root, specs, augment=False, entries=entries[:cut]),
    )
