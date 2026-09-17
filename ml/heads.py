"""Prediction heads, one per schema parameter.

The head type is derived from the parameter kind, so a new parameter in the C++ schema gets a
suitable head with no changes here:

    float + log_scale  -> LogBinnedHead    (cents classification, sub-bin decoding)
    float              -> LinearHead       (normalised regression)
    choice             -> CategoricalHead
    bool               -> BinaryHead
"""

from __future__ import annotations

import math

import torch
from torch import nn

from schema import ParameterSpec

BIN_WIDTH_CENTS = 20.0
TARGET_SIGMA_CENTS = 25.0
DECODE_RADIUS_BINS = 4


class ParameterHead(nn.Module):
    def __init__(self, spec: ParameterSpec, features: int, outputs: int) -> None:
        super().__init__()
        self.spec = spec
        self.linear = nn.Linear(features, outputs)

    def forward(self, trunk: torch.Tensor) -> torch.Tensor:
        return self.linear(trunk)


class LogBinnedHead(ParameterHead):
    """Predicts a distribution over log-spaced bins instead of regressing the raw value.

    Direct regression across three decades converges slowly and produces octave errors; binning in
    cents with soft targets trains quickly and still decodes to sub-bin precision.
    """

    def __init__(self, spec: ParameterSpec, features: int) -> None:
        span_cents = 1200.0 * math.log2(spec.max / spec.min)
        n_bins = int(math.ceil(span_cents / BIN_WIDTH_CENTS)) + 1
        super().__init__(spec, features, n_bins)
        self.register_buffer("bin_cents", torch.arange(n_bins, dtype=torch.float32) * BIN_WIDTH_CENTS)

    def to_cents(self, value: torch.Tensor) -> torch.Tensor:
        return 1200.0 * torch.log2(value.clamp_min(1e-6) / self.spec.min)

    def loss(self, logits: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
        cents = self.to_cents(target).unsqueeze(1)
        weights = torch.exp(-0.5 * ((self.bin_cents.unsqueeze(0) - cents) / TARGET_SIGMA_CENTS) ** 2)
        weights = weights / weights.sum(dim=1, keepdim=True).clamp_min(1e-8)
        return -(weights * torch.log_softmax(logits, dim=1)).sum(dim=1).mean()

    def decode(self, logits: torch.Tensor) -> torch.Tensor:
        probabilities = torch.softmax(logits, dim=1)
        peak = probabilities.argmax(dim=1)

        offsets = torch.arange(
            -DECODE_RADIUS_BINS, DECODE_RADIUS_BINS + 1, device=logits.device
        ).unsqueeze(0)
        indices = (peak.unsqueeze(1) + offsets).clamp(0, probabilities.shape[1] - 1)

        local = probabilities.gather(1, indices)
        cents = self.bin_cents[indices]
        centre = (local * cents).sum(dim=1) / local.sum(dim=1).clamp_min(1e-8)
        return self.spec.min * torch.pow(2.0, centre / 1200.0)

    def metrics(self, logits: torch.Tensor, target: torch.Tensor) -> dict[str, float]:
        predicted = self.decode(logits)
        error = (1200.0 * torch.log2(predicted / target.clamp_min(1e-6))).abs()
        return {
            "median_cents": float(error.median()),
            "within_50_cents": float((error < 50.0).float().mean()),
            "octave_errors": float((error > 600.0).float().mean()),
        }


class LinearHead(ParameterHead):
    def __init__(self, spec: ParameterSpec, features: int) -> None:
        super().__init__(spec, features, 1)

    def normalise(self, value: torch.Tensor) -> torch.Tensor:
        return (value - self.spec.min) / (self.spec.max - self.spec.min)

    def loss(self, logits: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
        return nn.functional.smooth_l1_loss(
            torch.sigmoid(logits.squeeze(1)), self.normalise(target), beta=0.05
        )

    def decode(self, logits: torch.Tensor) -> torch.Tensor:
        return self.spec.min + torch.sigmoid(logits.squeeze(1)) * (self.spec.max - self.spec.min)

    def metrics(self, logits: torch.Tensor, target: torch.Tensor) -> dict[str, float]:
        error = (self.decode(logits) - target).abs()
        return {"mean_abs_error": float(error.mean())}


class CategoricalHead(ParameterHead):
    def __init__(self, spec: ParameterSpec, features: int) -> None:
        super().__init__(spec, features, max(1, len(spec.choices)))

    def loss(self, logits: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
        return nn.functional.cross_entropy(logits, target.long())

    def decode(self, logits: torch.Tensor) -> torch.Tensor:
        return logits.argmax(dim=1).float()

    def metrics(self, logits: torch.Tensor, target: torch.Tensor) -> dict[str, float]:
        return {"accuracy": float((self.decode(logits) == target).float().mean())}


class BinaryHead(ParameterHead):
    def __init__(self, spec: ParameterSpec, features: int) -> None:
        super().__init__(spec, features, 1)

    def loss(self, logits: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
        return nn.functional.binary_cross_entropy_with_logits(logits.squeeze(1), target.float())

    def decode(self, logits: torch.Tensor) -> torch.Tensor:
        return (logits.squeeze(1) > 0).float()

    def metrics(self, logits: torch.Tensor, target: torch.Tensor) -> dict[str, float]:
        return {"accuracy": float((self.decode(logits) == target).float().mean())}


def build_head(spec: ParameterSpec, features: int) -> ParameterHead:
    if spec.kind == "choice":
        return CategoricalHead(spec, features)
    if spec.kind == "bool":
        return BinaryHead(spec, features)
    if spec.log_scale:
        return LogBinnedHead(spec, features)
    return LinearHead(spec, features)
