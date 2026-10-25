"""Evaluate a saved CSP + LDA model on one prepared, annotated recording.

The script reads only the selected local model, sample-major float32 recording
and its metadata. It writes a new JSON report and never overwrites model or
training metrics.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
from datetime import datetime, timezone

import numpy as np
from scipy.signal import sosfilt


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def event_code(value: str) -> str:
    match = re.search(r"\b(T[012])\b", value or "")
    if not match:
        raise ValueError("Модель не указывает код разметки T0/T1/T2 для обоих классов")
    return match.group(1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True, type=pathlib.Path)
    parser.add_argument("--metadata", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    model_path = args.model.resolve(strict=True)
    metadata_path = args.metadata.resolve(strict=True)
    output_path = args.output.resolve()
    if model_path.suffix.lower() != ".json" or metadata_path.suffix.lower() != ".json":
        raise ValueError("Ожидаются JSON-файл модели и JSON-метаданные записи")
    model = json.loads(model_path.read_text(encoding="utf-8"))
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    if model.get("format") != "metasiberia-neurocontrol-csp-lda-v1":
        raise ValueError("Формат модели не поддерживается оценщиком CSP + LDA")
    if metadata.get("format") != "metasiberia-neurocontrol-f32-v1":
        raise ValueError("Формат подготовленной записи не поддерживается")

    data_name = metadata.get("data_file")
    if not isinstance(data_name, str) or pathlib.Path(data_name).name != data_name:
        raise ValueError("Имя файла отсчётов в метаданных недопустимо")
    data_path = (metadata_path.parent / data_name).resolve(strict=True)
    if data_path.parent != metadata_path.parent.resolve():
        raise ValueError("Файл отсчётов находится вне каталога выбранной записи")
    if metadata.get("data_sha256") != sha256(data_path):
        raise ValueError("SHA-256 подготовленных отсчётов не совпадает с метаданными")

    channels = model.get("channels")
    if not isinstance(channels, list) or channels != metadata.get("channels"):
        raise ValueError("Запись несовместима с моделью: порядок или состав каналов отличается")
    sample_rate = int(model.get("sample_rate_hz", 0))
    if sample_rate <= 0 or sample_rate != int(metadata.get("sample_rate_hz", 0)):
        raise ValueError("Запись несовместима с моделью: отличается частота дискретизации")
    channel_count = len(channels)
    sample_count = int(metadata.get("samples", 0))
    expected_bytes = sample_count * channel_count * np.dtype("<f4").itemsize
    if sample_count <= 0 or data_path.stat().st_size != expected_bytes:
        raise ValueError("Размер файла отсчётов не соответствует метаданным")
    samples = np.fromfile(data_path, dtype="<f4").reshape(sample_count, channel_count).T.astype(np.float64)

    preprocessing = model.get("preprocessing", {})
    offset_seconds = float(preprocessing.get("event_offset_seconds", 0.5))
    window_samples = int(model.get("window_samples", 0))
    step_samples = int(model.get("step_samples", window_samples))
    if not 0 <= offset_seconds <= 3 or window_samples <= 0 or step_samples <= 0:
        raise ValueError("Параметры окна модели некорректны")
    sos_values = model.get("sos_filter", model.get("sos_bandpass_8_30_hz"))
    filters = np.asarray(sos_values, dtype=np.float64)
    csp = np.asarray(model.get("csp_filters"), dtype=np.float64)
    coefficients = np.asarray(model.get("lda_coefficients"), dtype=np.float64)
    intercept = float(model.get("lda_intercept"))
    if filters.ndim != 2 or filters.shape[1] != 6 or csp.ndim != 2 or csp.shape[1] != channel_count or \
            coefficients.ndim != 1 or coefficients.size != csp.shape[0] or not np.isfinite(intercept):
        raise ValueError("Параметры фильтра или CSP + LDA модели некорректны")
    threshold = float(model.get("forward_threshold", 0.75))
    if not 0.5 < threshold < 1:
        raise ValueError("Порог положительного класса модели вне допустимого диапазона")
    negative_code = event_code(model.get("negative_class_ru", model.get("negative_class", "")))
    positive_code = event_code(model.get("positive_class_ru", model.get("positive_class", "")))
    if negative_code == positive_code:
        raise ValueError("Коды разметки классов модели совпадают")

    filtered = sosfilt(filters, samples, axis=1)
    epochs: list[np.ndarray] = []
    labels: list[int] = []
    offset = round(offset_seconds * sample_rate)
    for annotation in metadata.get("annotations", []):
        label = annotation.get("label")
        if label not in (negative_code, positive_code):
            continue
        first = int(annotation.get("start_sample", -1)) + offset
        end = int(annotation.get("end_sample", -1))
        if first < 0 or end > sample_count or end < first:
            raise ValueError("Границы события в метаданных записи некорректны")
        for start in range(first, end - window_samples + 1, step_samples):
            epochs.append(filtered[:, start:start + window_samples])
            labels.append(1 if label == positive_code else 0)
    truth = np.asarray(labels, dtype=np.int32)
    if truth.size == 0 or not np.any(truth == 0) or not np.any(truth == 1):
        raise ValueError("В выбранной записи нет полного набора размеченных окон обоих классов")

    epoch_array = np.asarray(epochs, dtype=np.float64)
    projected = np.einsum("kc,nct->nkt", csp, epoch_array)
    features = np.log(np.maximum(np.mean(np.square(projected), axis=2), 1e-30))
    decision = features @ coefficients + intercept
    probability = 1.0 / (1.0 + np.exp(-np.clip(decision, -700, 700)))
    predicted = (probability >= threshold).astype(np.int32)
    tn = int(np.count_nonzero((truth == 0) & (predicted == 0)))
    fp = int(np.count_nonzero((truth == 0) & (predicted == 1)))
    fn = int(np.count_nonzero((truth == 1) & (predicted == 0)))
    tp = int(np.count_nonzero((truth == 1) & (predicted == 1)))
    recall_negative = tn / (tn + fp) if tn + fp else 0.0
    recall_positive = tp / (tp + fn) if tp + fn else 0.0
    precision = tp / (tp + fp) if tp + fp else 0.0
    recall = recall_positive
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    report = {
        "format": "metasiberia-neurocontrol-evaluation-v1",
        "evaluated_at_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "model_id": model_path.stem,
        "model_sha256": sha256(model_path),
        "recording_id": metadata_path.stem,
        "recording_sha256": metadata.get("source_sha256"),
        "prepared_data_sha256": metadata.get("data_sha256"),
        "sample_rate_hz": sample_rate,
        "channels": channels,
        "event_offset_seconds": offset_seconds,
        "window_samples": window_samples,
        "step_samples": step_samples,
        "model_threshold": threshold,
        "controller_safety_floor": 0.75,
        "effective_runtime_threshold": max(threshold, 0.75),
        "evaluation_threshold": threshold,
        "negative_event": negative_code,
        "positive_event": positive_code,
        "negative_class": model.get("negative_class_ru", model.get("negative_class")),
        "positive_class": model.get("positive_class_ru", model.get("positive_class")),
        "evaluation_protocol": "Размеченные окна выбранной записи; фильтрация и CSP + LDA соответствуют сохранённой модели. Порог не подбирался на этой записи.",
        "window_count": int(truth.size),
        "negative_windows": int(np.count_nonzero(truth == 0)),
        "positive_windows": int(np.count_nonzero(truth == 1)),
        "accuracy": (tn + tp) / int(truth.size),
        "balanced_accuracy": (recall_negative + recall_positive) / 2,
        "precision_positive": precision,
        "recall_positive": recall_positive,
        "f1_positive": f1,
        "confusion_matrix_rows_actual_negative_positive": [[tn, fp], [fn, tp]],
        "false_positive_negative_class": fp,
        "false_negative_positive_class": fn,
        "avatar_motion_tested": False,
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps(report, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
