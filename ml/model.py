"""Shared audio trunk plus one head per schema parameter."""

from __future__ import annotations

import torch
from torch import nn

from features import LogSpectrogram
from heads import build_head
from schema import ParameterSpec


class ConvBlock(nn.Module):
    def __init__(self, in_channels: int, out_channels: int) -> None:
        super().__init__()
        self.body = nn.Sequential(
            nn.Conv2d(in_channels, out_channels, kernel_size=(5, 3), padding=(2, 1)),
            nn.BatchNorm2d(out_channels),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(kernel_size=(2, 1)),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.body(x)


class ParameterEstimator(nn.Module):
    def __init__(self, specs: list[ParameterSpec], width: int = 32, embedding: int = 256) -> None:
        super().__init__()
        self.specs = specs
        self.spectrogram = LogSpectrogram()

        self.trunk = nn.Sequential(
            ConvBlock(1, width),
            ConvBlock(width, width),
            ConvBlock(width, width * 2),
            ConvBlock(width * 2, width * 2),
        )

        pooled_bins = self.spectrogram.n_bins // 16
        self.embed = nn.Sequential(
            nn.Flatten(),
            nn.Linear(width * 2 * pooled_bins, embedding),
            nn.ReLU(inplace=True),
            nn.Dropout(0.1),
        )

        self.heads = nn.ModuleDict({spec.id: build_head(spec, embedding) for spec in specs})

    def forward(self, audio: torch.Tensor) -> dict[str, torch.Tensor]:
        features = self.trunk(self.spectrogram(audio))
        # Pool time away but keep the frequency axis: the head needs absolute position, not just shape.
        trunk = self.embed(features.mean(dim=3))
        return {name: head(trunk) for name, head in self.heads.items()}

    def loss(self, outputs: dict[str, torch.Tensor], targets: dict[str, torch.Tensor]) -> torch.Tensor:
        return torch.stack(
            [self.heads[name].loss(outputs[name], targets[name]) for name in self.heads]
        ).sum()

    def metrics(
        self, outputs: dict[str, torch.Tensor], targets: dict[str, torch.Tensor]
    ) -> dict[str, dict[str, float]]:
        return {
            name: head.metrics(outputs[name], targets[name]) for name, head in self.heads.items()
        }

    def decode(self, outputs: dict[str, torch.Tensor]) -> dict[str, torch.Tensor]:
        return {name: head.decode(outputs[name]) for name, head in self.heads.items()}
