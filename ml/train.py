"""Trains the parameter estimator on a rendered dataset."""

from __future__ import annotations

import argparse
import time
from datetime import datetime
from pathlib import Path

import torch
from torch.utils.data import DataLoader

from data import split
from model import ParameterEstimator
from schema import load_schema
from training_dashboard import TrainingDashboard


def log(message: str, dashboard: TrainingDashboard | None = None) -> None:
    text = f"[{datetime.now():%Y-%m-%d %H:%M:%S}] {message}"
    print(text, flush=True)
    if dashboard is not None:
        dashboard.publish_output(text + "\n")


def pick_device(requested: str) -> torch.device:
    if requested != "auto":
        return torch.device(requested)
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")


def run_epoch(model, loader, device, optimiser=None, progress=None) -> tuple[float, dict]:
    training = optimiser is not None
    model.train(training)

    total_loss = 0.0
    batches = 0
    accumulated: dict[str, dict[str, float]] = {}

    with torch.set_grad_enabled(training):
        for audio, targets in loader:
            audio = audio.to(device)
            targets = {name: value.to(device) for name, value in targets.items()}

            outputs = model(audio)
            loss = model.loss(outputs, targets)

            if training:
                optimiser.zero_grad(set_to_none=True)
                loss.backward()
                optimiser.step()

            total_loss += float(loss.detach())
            batches += 1

            with torch.no_grad():
                batch_metrics = model.metrics(outputs, targets)

            for name, values in batch_metrics.items():
                bucket = accumulated.setdefault(name, {})
                for key, value in values.items():
                    bucket[key] = bucket.get(key, 0.0) + value

            if progress is not None:
                progress()

    averaged = {
        name: {key: value / max(1, batches) for key, value in values.items()}
        for name, values in accumulated.items()
    }
    return total_loss / max(1, batches), averaged


def format_metrics(metrics: dict) -> str:
    return " | ".join(
        f"{name}: " + ", ".join(f"{key}={value:.4g}" for key, value in values.items())
        for name, values in metrics.items()
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Train the AdbSynth parameter estimator.")
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch-size", type=int, default=64)
    parser.add_argument("--learning-rate", type=float, default=3e-4)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--checkpoint", type=Path, default=Path("ml/checkpoints/model.pt"))
    parser.add_argument("--dashboard-host", default="127.0.0.1")
    parser.add_argument("--dashboard-port", type=int, default=8765)
    args = parser.parse_args()

    dashboard = TrainingDashboard(args.dashboard_host, args.dashboard_port)
    dashboard.start()
    log(f"dashboard={dashboard.url}", dashboard)

    device = pick_device(args.device)
    specs = load_schema(args.data / "schema.json")
    train_set, validation_set = split(args.data, specs)

    train_loader = DataLoader(
        train_set, batch_size=args.batch_size, shuffle=True, num_workers=args.workers, drop_last=True
    )
    validation_loader = DataLoader(
        validation_set, batch_size=args.batch_size, shuffle=False, num_workers=args.workers
    )

    model = ParameterEstimator(specs).to(device)
    optimiser = torch.optim.AdamW(model.parameters(), lr=args.learning_rate)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimiser, T_max=args.epochs)

    parameter_count = sum(p.numel() for p in model.parameters())
    log(
        f"device={device} params={parameter_count/1e6:.2f}M "
        f"train={len(train_set)} val={len(validation_set)} "
        f"batch_size={args.batch_size} workers={args.workers}", dashboard
    )

    args.checkpoint.parent.mkdir(parents=True, exist_ok=True)
    best = float("inf")

    for epoch in range(1, args.epochs + 1):
        epoch_started = time.monotonic()
        total_iterations = len(train_loader) + len(validation_loader)
        completed_iterations = 0
        rendered_dots = 0
        progress_prefix = f"[{datetime.now():%Y-%m-%d %H:%M:%S}] epoch {epoch:3d}/{args.epochs} "

        def draw_progress() -> None:
            text = f"\r{progress_prefix}[{'.' * rendered_dots}{' ' * (55 - rendered_dots)}]"
            print(text, end="", flush=True)
            dashboard.publish_output(text)

        def update_progress() -> None:
            nonlocal completed_iterations, rendered_dots
            completed_iterations += 1
            target_dots = min(55, completed_iterations * 55 // max(1, total_iterations))
            if target_dots != rendered_dots:
                rendered_dots = target_dots
                draw_progress()
            dashboard.update_progress(epoch, args.epochs, completed_iterations, total_iterations)

        draw_progress()
        train_loss, _ = run_epoch(model, train_loader, device, optimiser, update_progress)
        validation_loss, metrics = run_epoch(
            model, validation_loader, device, progress=update_progress
        )
        scheduler.step()
        rendered_dots = 55
        draw_progress()
        print()
        dashboard.publish_output("\n")

        log(
            f"epoch {epoch:3d}/{args.epochs} finished in {time.monotonic() - epoch_started:.1f}s "
            f"train {train_loss:.4f} val {validation_loss:.4f} | {format_metrics(metrics)}",
            dashboard,
        )
        dashboard.record_epoch(epoch, train_loss, validation_loss, metrics)

        if validation_loss < best:
            best = validation_loss
            torch.save(
                {"state_dict": model.state_dict(), "schema": str(args.data / "schema.json")},
                args.checkpoint,
            )
            log(f"saved checkpoint {args.checkpoint}", dashboard)

    log(f"best validation loss {best:.4f} -> {args.checkpoint}", dashboard)
    dashboard.finish()


if __name__ == "__main__":
    main()
