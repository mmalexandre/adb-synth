"""Small standard-library dashboard for observing a running training job."""

from __future__ import annotations

import json
import subprocess
import threading
import time
from collections import deque
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any


PAGE = r"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AdbSynth training</title>
  <style>
    :root { color-scheme: dark; --bg: #101416; --panel: #192124; --line: #2e3b3d; --text: #e8efed; --muted: #91a5a2; --accent: #f1b85b; }
    * { box-sizing: border-box; }
    body { margin: 0; background: radial-gradient(circle at top right, #243638, var(--bg) 48%); color: var(--text); font: 15px/1.45 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
    main { width: min(1180px, 100%); margin: 0 auto; padding: 32px 20px 40px; }
    header { display: flex; align-items: baseline; justify-content: space-between; gap: 16px; margin-bottom: 24px; }
    h1 { margin: 0; font: 700 28px/1.1 Georgia, serif; letter-spacing: 0; }
    #status { color: var(--accent); font-size: 12px; text-transform: uppercase; }
    #graphs { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 16px; }
    section { border: 1px solid var(--line); background: color-mix(in srgb, var(--panel) 92%, transparent); padding: 16px; }
    .full-width { grid-column: 1 / -1; }
    h2 { margin: 0 0 12px; font-size: 13px; font-weight: 600; color: var(--muted); }
    canvas { display: block; width: 100%; height: 220px; }
    #gpu-info { display: grid; gap: 10px; }
    #gpu-panel { grid-column: 1 / -1; }
    .gpu-card { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px 24px; }
    .gpu-row { display: flex; justify-content: space-between; gap: 16px; border-bottom: 1px solid var(--line); padding-bottom: 8px; }
    .gpu-row:last-child { border-bottom: 0; padding-bottom: 0; }
    .gpu-label { color: var(--muted); }
    .gpu-value { color: var(--accent); text-align: right; }
    #output { height: 280px; overflow: auto; white-space: pre-wrap; overflow-wrap: anywhere; color: #c4d0cd; font-size: 12px; }
    @media (max-width: 760px) { main { padding: 22px 12px 28px; } #graphs { grid-template-columns: 1fr; } header { display: block; } #status { display: block; margin-top: 8px; } }
  </style>
</head>
<body>
  <main>
    <header><h1>AdbSynth / training</h1><span id="status">connecting</span></header>
    <section id="gpu-panel"><h2>GPU</h2><div id="gpu-info">Telemetry unavailable</div></section>
    <div id="graphs"></div>
    <section><h2>stdout</h2><div id="output"></div></section>
  </main>
  <script>
    const graphs = new Map();
    const output = document.querySelector('#output');
    const status = document.querySelector('#status');
        const state = { epochs: [], progress: null, gpu: [], graphs: [], finished: false };

        function makeGraph(title, key, fullWidth = false) {
      const section = document.createElement('section');
            if (fullWidth) section.classList.add('full-width');
      section.innerHTML = `<h2>${title}</h2><canvas></canvas>`;
      document.querySelector('#graphs').append(section);
      const canvas = section.querySelector('canvas');
      graphs.set(key, { canvas, values: [] });
      return graphs.get(key);
    }

    makeGraph('train loss', 'train_loss', true);
    makeGraph('validation loss', 'validation_loss');

    function draw(graph) {
      const canvas = graph.canvas;
      const ratio = window.devicePixelRatio || 1;
      const width = canvas.clientWidth;
      const height = canvas.clientHeight;
      canvas.width = width * ratio;
      canvas.height = height * ratio;
      const ctx = canvas.getContext('2d');
      ctx.scale(ratio, ratio);
      ctx.clearRect(0, 0, width, height);
            const values = graph.values.filter(Number.isFinite);
      const low = Math.min(...values), high = Math.max(...values);
            const pad = { left: 48, right: 12, top: 22, bottom: 28 };
            const plotWidth = width - pad.left - pad.right;
            const plotHeight = height - pad.top - pad.bottom;
            ctx.font = '10px ui-monospace, monospace'; ctx.fillStyle = '#91a5a2';
            if (!values.length) {
                ctx.fillText('waiting for data', pad.left, pad.top + plotHeight / 2);
                return;
            }
            const span = high - low || 1;
            ctx.fillText(high.toPrecision(4), 2, pad.top + 4);
            ctx.fillText(low.toPrecision(4), 2, height - pad.bottom + 4);
            ctx.fillText('epoch', width - 42, height - 6);
            ctx.strokeStyle = '#2e3b3d'; ctx.lineWidth = 1;
            for (let index = 0; index <= 4; index++) {
                const y = pad.top + plotHeight * index / 4;
                ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(width - pad.right, y); ctx.stroke();
            }
            ctx.fillText('1', pad.left, height - 6);
            if (values.length > 1) ctx.fillText(String(values.length), width - pad.right - 8, height - 6);
      ctx.strokeStyle = '#f1b85b'; ctx.lineWidth = 2; ctx.beginPath();
      values.forEach((value, index) => {
                const x = pad.left + plotWidth * (values.length === 1 ? 0 : index / (values.length - 1));
                const y = pad.top + plotHeight * (1 - (value - low) / span);
        index ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      });
      ctx.stroke();
    }

    function render() {
      for (const graph of graphs.values()) draw(graph);
    }

    function applyState(next) {
      Object.assign(state, next);
            for (const graph of state.graphs) {
                if (!graphs.has(graph.key)) makeGraph(graph.title, graph.key, graph.full_width);
            }
            graphs.get('train_loss').values = state.epochs.map(item => item.train_loss);
            graphs.get('validation_loss').values = state.epochs.map(item => item.validation_loss);
      for (const item of state.epochs) for (const [name, metrics] of Object.entries(item.metrics || {})) {
        for (const [metric, value] of Object.entries(metrics)) {
          const key = `${name}.${metric}`;
          if (!graphs.has(key)) makeGraph(key, key);
          graphs.get(key).values = state.epochs.map(epoch => epoch.metrics?.[name]?.[metric]).filter(Number.isFinite);
        }
      }
            if (state.progress) {
                const eta = state.progress.eta_seconds == null ? 'ETA --' : `ETA ${formatDuration(state.progress.eta_seconds)}`;
                status.textContent = `epoch ${state.progress.epoch}/${state.progress.total_epochs} | ${state.progress.percent}% | ${eta}`;
            }
      if (state.finished) status.textContent = 'finished';
            renderGpu();
      render();
    }

        function formatDuration(seconds) {
            const minutes = Math.floor(seconds / 60);
            const remaining = Math.round(seconds % 60);
            return minutes ? `${minutes}m ${String(remaining).padStart(2, '0')}s` : `${remaining}s`;
        }

        function renderGpu() {
            const section = document.querySelector('#gpu-panel');
            const info = section.querySelector('#gpu-info');
            info.replaceChildren();
            if (!state.gpu.length) {
                info.textContent = 'Telemetry unavailable';
                return;
            }
            for (const gpu of state.gpu) {
                const card = document.createElement('div'); card.className = 'gpu-card';
                const rows = [
                    ['GPU', `${gpu.gpu}${gpu.xcp == null ? '' : ` / XCP ${gpu.xcp}`}`],
                    ['VRAM', `${gpu.vram_used} / ${gpu.vram_total} ${gpu.vram_unit}`],
                    ['Power', `${gpu.power} ${gpu.power_unit}`],
                    ['Hotspot', `${gpu.hotspot} ${gpu.hotspot_unit}`],
                    ['GFX clock', `${gpu.gfx_clock} ${gpu.gfx_clock_unit}`],
                    ['GFX load', `${gpu.gfx_load}${gpu.gfx_load_unit}`],
                    ['Memory load', `${gpu.mem_load}${gpu.mem_load_unit}`],
                    ['Encoder', `${gpu.encoder}${gpu.encoder_unit}`],
                    ['Decoder', `${gpu.decoder}${gpu.decoder_unit}`],
                ];
                for (const [label, value] of rows) {
                    const row = document.createElement('div'); row.className = 'gpu-row';
                    row.innerHTML = `<span class="gpu-label">${label}</span><span class="gpu-value">${value}</span>`;
                    card.append(row);
                }
                info.append(card);
            }
        }

        const stdoutLines = [''];
        function appendStdout(text) {
            for (const character of text) {
                if (character === '\r') stdoutLines[stdoutLines.length - 1] = '';
                else if (character === '\n') stdoutLines.push('');
                else stdoutLines[stdoutLines.length - 1] += character;
            }
            output.textContent = stdoutLines.join('\n');
            output.scrollTop = output.scrollHeight;
        }

        const source = new EventSource('/events');
    source.onopen = () => { status.textContent = 'connected'; };
    source.onerror = () => { if (!state.finished) status.textContent = 'reconnecting'; };
    source.addEventListener('state', event => applyState(JSON.parse(event.data)));
    source.addEventListener('output', event => appendStdout(JSON.parse(event.data).text));
    window.addEventListener('resize', render);
  </script>
</body>
</html>
"""


class _Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args: Any) -> None:
        pass

    def do_GET(self) -> None:
        dashboard: TrainingDashboard = self.server.dashboard  # type: ignore[attr-defined]
        if self.path == "/":
            payload = PAGE.encode()
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        if self.path == "/events":
            dashboard.add_client(self)
            return
        self.send_error(HTTPStatus.NOT_FOUND)


class TrainingDashboard:
    def __init__(
        self, host: str = "127.0.0.1", port: int = 8765,
        graph_definitions: list[dict[str, Any]] | None = None,
    ) -> None:
        self._lock = threading.Lock()
        self._clients: list[Any] = []
        self._events: deque[str] = deque(maxlen=2000)
        self._state: dict[str, Any] = {
            "epochs": [], "progress": None, "gpu": [],
            "graphs": graph_definitions or [], "finished": False,
        }
        self._gpu_stop = threading.Event()
        self._gpu_thread = threading.Thread(target=self._sample_gpu, daemon=True)
        self._progress_started: float | None = None
        self._server = ThreadingHTTPServer((host, port), _Handler)
        self._server.dashboard = self  # type: ignore[attr-defined]
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)

    @property
    def url(self) -> str:
        host, port = self._server.server_address
        return f"http://{host}:{port}"

    def start(self) -> None:
        self._thread.start()
        self._gpu_thread.start()

    def stop(self) -> None:
        self._gpu_stop.set()
        self._server.shutdown()
        self._server.server_close()

    def add_client(self, handler: BaseHTTPRequestHandler) -> None:
        handler.send_response(HTTPStatus.OK)
        handler.send_header("Content-Type", "text/event-stream")
        handler.send_header("Cache-Control", "no-cache")
        handler.send_header("Connection", "keep-alive")
        handler.end_headers()
        with self._lock:
            self._clients.append(handler)
            events = list(self._events)
            state = self._event("state", self._state)
        try:
            handler.wfile.write((state + "\n" + "\n".join(events) + "\n\n").encode())
            handler.wfile.flush()
            while True:
                handler.wfile.write(b": keep-alive\n\n")
                handler.wfile.flush()
                threading.Event().wait(15)
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            with self._lock:
                if handler in self._clients:
                    self._clients.remove(handler)

    def publish_output(self, text: str) -> None:
        self._publish("output", {"text": text})

    def update_progress(self, epoch: int, total_epochs: int, completed: int, total: int) -> None:
        overall_completed = (epoch - 1) * total + completed
        overall_total = total_epochs * total
        if self._progress_started is None:
            self._progress_started = time.monotonic()
        elapsed = time.monotonic() - self._progress_started
        eta_seconds = None
        if overall_completed > 0:
            eta_seconds = round(elapsed * (overall_total - overall_completed) / overall_completed)
        with self._lock:
            self._state["progress"] = {
                "epoch": epoch, "total_epochs": total_epochs,
                "percent": round(100 * overall_completed / max(1, overall_total)),
                "eta_seconds": eta_seconds,
            }
            payload = self._event("state", {"progress": self._state["progress"]})
        self._send(payload)

    def record_epoch(self, epoch: int, train_loss: float, validation_loss: float, metrics: dict) -> None:
        with self._lock:
            self._state["epochs"].append({
                "epoch": epoch, "train_loss": train_loss,
                "validation_loss": validation_loss, "metrics": metrics,
            })
            payload = self._event("state", {"epochs": self._state["epochs"]})
        self._send(payload)

    def finish(self) -> None:
        with self._lock:
            self._state["finished"] = True
            payload = self._event("state", {"finished": True})
        self._send(payload)

    def _publish(self, event: str, data: dict[str, Any]) -> None:
        payload = self._event(event, data)
        with self._lock:
            self._events.append(payload)
        self._send(payload)

    def _event(self, event: str, data: dict[str, Any]) -> str:
        return f"event: {event}\ndata: {json.dumps(data)}\n"

    def _sample_gpu(self) -> None:
        while not self._gpu_stop.is_set():
            telemetry = self._read_gpu()
            with self._lock:
                self._state["gpu"] = telemetry
                payload = self._event("state", {"gpu": telemetry})
            self._send(payload)
            self._gpu_stop.wait(2)

    @staticmethod
    def _read_gpu() -> list[dict[str, Any]]:
        try:
            result = subprocess.run(
                ["amd-smi", "monitor", "--json"],
                capture_output=True,
                check=True,
                text=True,
                timeout=2,
            )
            rows = json.loads(result.stdout)
        except (OSError, ValueError, subprocess.SubprocessError):
            return []

        def reading(row: dict[str, Any], key: str, default_unit: str = "") -> tuple[Any, str]:
            value = row.get(key, {})
            if not isinstance(value, dict):
                return value, default_unit
            return value.get("value", "--"), value.get("unit", default_unit)

        return [
            {
                "gpu": row.get("gpu", index),
                "xcp": row.get("xcp"),
                "vram_used": reading(row, "vram_used", "GB")[0],
                "vram_total": reading(row, "vram_total", "GB")[0],
                "vram_unit": reading(row, "vram_used", "GB")[1],
                "power": reading(row, "power_usage", "W")[0],
                "power_unit": reading(row, "power_usage", "W")[1],
                "hotspot": reading(row, "hotspot_temperature", "C")[0],
                "hotspot_unit": reading(row, "hotspot_temperature", "C")[1],
                "gfx_clock": reading(row, "gfx_clk", "MHz")[0],
                "gfx_clock_unit": reading(row, "gfx_clk", "MHz")[1],
                "gfx_load": reading(row, "gfx", "%")[0],
                "gfx_load_unit": reading(row, "gfx", "%")[1],
                "mem_load": reading(row, "mem", "%")[0],
                "mem_load_unit": reading(row, "mem", "%")[1],
                "encoder": reading(row, "encoder", "%")[0],
                "encoder_unit": reading(row, "encoder", "%")[1],
                "decoder": reading(row, "decoder", "%")[0],
                "decoder_unit": reading(row, "decoder", "%")[1],
            }
            for index, row in enumerate(rows if isinstance(rows, list) else [])
        ]

    def _send(self, payload: str) -> None:
        with self._lock:
            clients = list(self._clients)
        for client in clients:
            try:
                client.wfile.write((payload + "\n").encode())
                client.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                with self._lock:
                    if client in self._clients:
                        self._clients.remove(client)