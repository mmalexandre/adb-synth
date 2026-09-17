BUILD_DIR := build
CONFIG := Release
STANDALONE_APP := $(BUILD_DIR)/AdbSynth_artefacts/Standalone/AdbSynth
ML_PYTHON ?= python3
ML_TMP_DIR ?= .tmp/ml
ML_DATA_DIR ?= $(ML_TMP_DIR)/train
ML_COUNT ?= 20000
ML_EPOCHS ?= 20
ML_CHECKPOINT ?= ml/checkpoints/model.pt

.PHONY: all configure build run clean ml-data ml-train ml-predict ml-clean

all: build

configure:
	cmake -B $(BUILD_DIR) -S .

build: configure
	cmake --build $(BUILD_DIR) --config $(CONFIG)

run: build
	$(STANDALONE_APP)

clean:
	rm -rf $(BUILD_DIR)

ml-data: build
	PYTHONPATH=ml $(ML_PYTHON) ml/generate.py --renderer $(BUILD_DIR)/AdbSynthRender --outdir $(ML_DATA_DIR) --count $(ML_COUNT)

ml-train: ml-data
	PYTHONPATH=ml $(ML_PYTHON) ml/train.py --data $(ML_DATA_DIR) --epochs $(ML_EPOCHS) --checkpoint $(ML_CHECKPOINT)

ml-predict:
	@test -n "$(AUDIO)" || (echo "Usage: make ml-predict AUDIO=sample.wav" && exit 1)
	@test -f "$(ML_CHECKPOINT)" || (echo "Checkpoint not found: $(ML_CHECKPOINT)" && exit 1)
	@test -f "$(ML_DATA_DIR)/schema.json" || (echo "Schema not found: $(ML_DATA_DIR)/schema.json; run make ml-data first" && exit 1)
	PYTHONPATH=ml $(ML_PYTHON) ml/predict.py "$(AUDIO)" --schema $(ML_DATA_DIR)/schema.json --checkpoint $(ML_CHECKPOINT)

ml-clean:
	rm -rf $(ML_TMP_DIR) $(ML_CHECKPOINT)