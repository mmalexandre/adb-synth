BUILD_DIR := build
CONFIG := Release
STANDALONE_APP := $(BUILD_DIR)/AdbSynth_artefacts/Standalone/AdbSynth
ML_BOOTSTRAP_PYTHON ?= python3
ML_VENV ?= .venv
ML_PYTHON ?= $(ML_VENV)/bin/python
ML_REQUIREMENTS := ml/requirements.txt
ML_TORCH_REQUIREMENTS := ml/requirements-torch.txt
ML_TORCH_INDEX_URL ?= https://download.pytorch.org/whl/cpu
ML_VENV_STAMP := $(ML_VENV)/.requirements-installed
ML_TMP_DIR ?= .tmp/ml
ML_DATA_DIR ?= $(ML_TMP_DIR)/train
ML_COUNT ?= 20000
ML_EPOCHS ?= 20
ML_CHECKPOINT ?= ml/checkpoints/model.pt

.PHONY: all configure build run clean ml-data ml-train ml-export ml-predict ml-clean

all: build

configure:
	cmake -B $(BUILD_DIR) -S .

build: configure
	cmake --build $(BUILD_DIR) --config $(CONFIG)

run: build
	$(STANDALONE_APP)

clean:
	rm -rf $(BUILD_DIR)

ml-data: build $(ML_VENV_STAMP)
	PYTHONPATH=ml $(ML_PYTHON) ml/generate.py --renderer $(BUILD_DIR)/AdbSynthRender --outdir $(ML_DATA_DIR) --count $(ML_COUNT)

ml-train: ml-data
	PYTHONPATH=ml $(ML_PYTHON) ml/train.py --data $(ML_DATA_DIR) --epochs $(ML_EPOCHS) --checkpoint $(ML_CHECKPOINT)

ml-export:
	@test -f "$(ML_CHECKPOINT)" || (echo "Checkpoint not found: $(ML_CHECKPOINT)" && exit 1)
	@test -f "$(ML_DATA_DIR)/schema.json" || (echo "Schema not found: $(ML_DATA_DIR)/schema.json; run make ml-data first" && exit 1)
	PYTHONPATH=ml $(ML_PYTHON) ml/export_torchscript.py --schema $(ML_DATA_DIR)/schema.json --checkpoint $(ML_CHECKPOINT) --output $(ML_TMP_DIR)/model_scripted.pt

ml-predict:
	@test -n "$(AUDIO)" || (echo "Usage: make ml-predict AUDIO=sample.wav" && exit 1)
	@test -f "$(ML_CHECKPOINT)" || (echo "Checkpoint not found: $(ML_CHECKPOINT)" && exit 1)
	@test -f "$(ML_DATA_DIR)/schema.json" || (echo "Schema not found: $(ML_DATA_DIR)/schema.json; run make ml-data first" && exit 1)
	PYTHONPATH=ml $(ML_PYTHON) ml/predict.py "$(AUDIO)" --schema $(ML_DATA_DIR)/schema.json --checkpoint $(ML_CHECKPOINT)

ml-clean:
	rm -rf $(ML_TMP_DIR) $(ML_CHECKPOINT)

$(ML_VENV_STAMP): $(ML_REQUIREMENTS) $(ML_TORCH_REQUIREMENTS)
	$(ML_BOOTSTRAP_PYTHON) -m venv $(ML_VENV)
	$(ML_PYTHON) -m pip install -r $(ML_REQUIREMENTS)
	$(ML_PYTHON) -m pip install --index-url $(ML_TORCH_INDEX_URL) -r $(ML_TORCH_REQUIREMENTS)
	touch $@