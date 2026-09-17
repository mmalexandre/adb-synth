"""Log-frequency spectrogram front end.

A log-spaced frequency axis turns a pitch change into a translation, which lets a very small CNN
generalise across the whole audible range from relatively few examples.
"""

from __future__ import annotations

import math

import torch

SAMPLE_RATE = 44100
CLIP_SAMPLES = 16384
N_FFT = 8192
HOP_LENGTH = 2048
N_BINS = 320
F_MIN = 20.0
F_MAX = 20000.0


def log_frequency_filterbank(
    sample_rate: int = SAMPLE_RATE,
    n_fft: int = N_FFT,
    n_bins: int = N_BINS,
    f_min: float = F_MIN,
    f_max: float = F_MAX,
) -> torch.Tensor:
    """Triangular filters whose centres are geometrically spaced between f_min and f_max."""
    edges = torch.exp(
        torch.linspace(math.log(f_min), math.log(f_max), n_bins + 2, dtype=torch.float64)
    )
    fft_freqs = torch.linspace(0.0, sample_rate / 2, n_fft // 2 + 1, dtype=torch.float64)

    lower, centre, upper = edges[:-2], edges[1:-1], edges[2:]
    rising = (fft_freqs[None, :] - lower[:, None]) / (centre - lower)[:, None]
    falling = (upper[:, None] - fft_freqs[None, :]) / (upper - centre)[:, None]
    weights = torch.clamp(torch.minimum(rising, falling), min=0.0)

    # Narrow low-frequency filters can fall entirely between FFT bins; fall back to the nearest bin.
    empty = weights.sum(dim=1) == 0
    if bool(empty.any()):
        nearest = torch.argmin((fft_freqs[None, :] - centre[:, None]).abs(), dim=1)
        weights[empty, nearest[empty]] = 1.0

    return weights.to(torch.float32)


class LogSpectrogram(torch.nn.Module):
    def __init__(self, sample_rate: int = SAMPLE_RATE) -> None:
        super().__init__()
        self.register_buffer("window", torch.hann_window(N_FFT))
        self.register_buffer("filterbank", log_frequency_filterbank(sample_rate))

    @property
    def n_bins(self) -> int:
        return int(self.filterbank.shape[0])

    def forward(self, audio: torch.Tensor) -> torch.Tensor:
        spectrum = torch.stft(
            audio,
            n_fft=N_FFT,
            hop_length=HOP_LENGTH,
            window=self.window,
            center=True,
            return_complex=True,
        ).abs()

        bands = torch.matmul(self.filterbank, spectrum)
        bands = torch.log1p(bands * 100.0)
        mean = bands.mean(dim=(1, 2), keepdim=True)
        std = bands.std(dim=(1, 2), keepdim=True).clamp_min(1e-5)
        return ((bands - mean) / std).unsqueeze(1)
