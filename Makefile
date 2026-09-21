BUILD_DIR := build
ML_BUILD_DIR := .tmp/ml-render-build
CONFIG := Release
STANDALONE_APP := $(BUILD_DIR)/AdbSynth_artefacts/Standalone/AdbSynth
ML_BOOTSTRAP_PYTHON ?= python3
ML_VENV ?= .venv
ML_PYTHON ?= /var/www/chino/.venv-whisper/bin/python
ML_REQUIREMENTS := ml/requirements.txt
ML_VENV_STAMP := $(ML_VENV)/.requirements-installed
ML_TMP_DIR ?= .tmp/ml
ML_DATA_DIR ?= $(ML_TMP_DIR)/train
ML_COUNT ?= 20000
ML_DURATION ?= 1.0
ML_EPOCHS ?= 500
ML_CHECKPOINT ?= ml/checkpoints/model.pt
ML_ROCM_ARCH ?= 11.0.0

.PHONY: all configure build run clean ml-data ml-renderer ml-train ml-export ml-predict ml-clean

all: build

configure:
	cmake -B $(BUILD_DIR) -S .

build: configure
	cmake --build $(BUILD_DIR) --config $(CONFIG)

run: build
	$(STANDALONE_APP)

run-standalone-synth:
	$(STANDALONE_APP)

clean:
	rm -rf $(BUILD_DIR)

ml-renderer:
	cmake -S . -B $(ML_BUILD_DIR) -DADBSYNTH_BUILD_PLUGIN=OFF
	cmake --build $(ML_BUILD_DIR) --target AdbSynthRender --config $(CONFIG)


ml-data:
	@$(MAKE) ml-renderer; \
	if test -f "$(ML_DATA_DIR)/labels.jsonl" && test -f "$(ML_DATA_DIR)/schema.json" && test -f "$(ML_DATA_DIR)/duration.txt" && test "$$(cat "$(ML_DATA_DIR)/duration.txt")" = "$(ML_DURATION)" && test "$$(wc -l < "$(ML_DATA_DIR)/labels.jsonl")" -eq "$(ML_COUNT)" && $(ML_BUILD_DIR)/AdbSynthRender --dump-schema | cmp -s - "$(ML_DATA_DIR)/schema.json"; then \
		echo "[$$(date '+%Y-%m-%d %H:%M:%S')] Reusing $(ML_COUNT) clips in $(ML_DATA_DIR)"; \
	else \
		echo "[$$(date '+%Y-%m-%d %H:%M:%S')] Rendering $(ML_COUNT) clips into $(ML_DATA_DIR)"; \
		$$(MAKE) ml-renderer; \
		PYTHONPATH=ml $(ML_PYTHON) ml/generate.py --renderer $(ML_BUILD_DIR)/AdbSynthRender --outdir $(ML_DATA_DIR) --count $(ML_COUNT) --duration $(ML_DURATION); \
	fi

ml-train: ml-data
	echo "[$$(date '+%Y-%m-%d %H:%M:%S')] Starting training for $(ML_EPOCHS) epochs";
	HSA_OVERRIDE_GFX_VERSION=$(ML_ROCM_ARCH) PYTHONPATH=ml $(ML_PYTHON) ml/train.py --data $(ML_DATA_DIR) --epochs $(ML_EPOCHS) --checkpoint $(ML_CHECKPOINT)

ml-export:
	@test -f "$(ML_CHECKPOINT)" || (echo "Checkpoint not found: $(ML_CHECKPOINT)" && exit 1)
	@test -f "$(ML_DATA_DIR)/schema.json" || (echo "Schema not found: $(ML_DATA_DIR)/schema.json; run make ml-data first" && exit 1)
	HSA_OVERRIDE_GFX_VERSION=$(ML_ROCM_ARCH) PYTHONPATH=ml $(ML_PYTHON) ml/export_torchscript.py --schema $(ML_DATA_DIR)/schema.json --checkpoint $(ML_CHECKPOINT) --output $(ML_TMP_DIR)/model_scripted.pt

ml-predict:
	@test -n "$(AUDIO)" || (echo "Usage: make ml-predict AUDIO=sample.wav" && exit 1)
	@test -f "$(ML_CHECKPOINT)" || (echo "Checkpoint not found: $(ML_CHECKPOINT)" && exit 1)
	@test -f "$(ML_DATA_DIR)/schema.json" || (echo "Schema not found: $(ML_DATA_DIR)/schema.json; run make ml-data first" && exit 1)
	HSA_OVERRIDE_GFX_VERSION=$(ML_ROCM_ARCH) PYTHONPATH=ml $(ML_PYTHON) ml/predict.py "$(AUDIO)" --schema $(ML_DATA_DIR)/schema.json --checkpoint $(ML_CHECKPOINT)

ml-clean:
	rm -rf $(ML_TMP_DIR) $(ML_CHECKPOINT)
