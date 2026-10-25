# MetaSiberia NeuroControl

Research prototype inside the existing Substrata/Metasiberia Qt client. The
`integration/neurocontrol` worktree is linked to the game's Git repository; it is
not a separate application. BrainFlow is vendored in
`third_party/brainflow` (MIT licence). Data and generated models remain local.

The [Windows integration guide](../docs/NEUROCONTROL_WINDOWS_INTEGRATION.md)
covers isolated OFF/ON builds, dependencies, LSL setup, MCP, safety, and the
review steps before merging to `master`.

## Reproduce the S001 experiment on Windows

From the repository root:

```powershell
python -m venv neurocontrol/.venv
.\neurocontrol\.venv\Scripts\python.exe -m pip install -r neurocontrol/requirements-lock.txt
.\neurocontrol\.venv\Scripts\python.exe neurocontrol/tools/download_physionet.py
.\neurocontrol\.venv\Scripts\python.exe neurocontrol/tools/audit_dataset.py
.\neurocontrol\.venv\Scripts\python.exe neurocontrol/tools/prepare_train.py
cmake -S neurocontrol/tests -B neurocontrol/build/tests -G "Visual Studio 17 2022" -A x64
cmake --build neurocontrol/build/tests --config RelWithDebInfo --target neuro_classifier_check
.\neurocontrol\.venv\Scripts\python.exe neurocontrol/tests/check_cpp_parity.py neurocontrol/build/tests/RelWithDebInfo/neuro_classifier_check.exe
```

The downloader retrieves original `S001R06.edf`, `S001R10.edf`, and
`S001R14.edf` from PhysioNet and records hashes in the ignored raw directory.
The separate audit opens every EDF with MNE, checks all samples and event
annotations, compares PhysioNet SHA-256 checksums, and writes ignored
`datasets/raw/audit.json`. The UI reads this report before enabling training.
The trainer preserves their event onsets, offsets, sample rate, 10-10 electrode
names, standard `colin27_1005` 3D positions in metres, and original file hashes
in ignored `datasets/processed/*.json`. The positions come from a standard
montage; they were not measured individually for subject S001. Each
`.f32` file contains original selected EEG channels in volts, interleaved by
sample. The native client applies the **causal** bandpass when replaying, at
160 samples/second. EDF is converted by the Python adapter; BrainFlow Playback
File Board is not used for EDF because it expects BrainFlow recording files.

The model is trained on R06/R10; R14 stays entirely held out. Non-overlapping
2-second windows start 0.5 seconds after each annotation begins. Only T0 rest
and T2 both-feet imagery are binary training classes. T1 both-hands imagery is
a separate negative challenge set, **not** a STOP training label. The fixed
0.75 threshold is applied without tuning it on R14. See [results](docs/RESULTS.md).

## OpenNeuro WALK / STOP pilot

This is a separate model for imagined walking and imagined standing still; it
does not replace `models/S001_feet_vs_rest.json`. It uses OpenNeuro ds007788
`sub-01/ses-02`: open-loop `training` for fit, with complete `stop6min` and
`walk6min` EDF recordings held out for evaluation. To reproduce it after the
locked environment above is ready:

```powershell
.\neurocontrol\.venv\Scripts\python.exe neurocontrol/tools/download_openneuro_walk_stop.py
.\neurocontrol\.venv\Scripts\python.exe neurocontrol/tools/train_walk_stop.py
```

The separate model is `models/S01_walk_vs_stop.json`; source protocol, label
rules, held-out metrics, confusion matrix and C++ compatibility evidence are
in [the pilot report](docs/OPENNEURO_WALK_STOP_PILOT.md). The current single-
participant result is not suitable for avatar control.

## Универсальный локальный импорт для MCP и Qt

`neurocontrol/tools/prepare_recording.py` читает EDF/EDF+ и BDF через MNE,
сохраняет измеренные исходные каналы и частоту, выбирает EEG-каналы в точном
порядке, записывает аннотации EDF и подходящие BIDS `*_events.tsv`, а затем
создаёт пару `*.f32` + `*.json`. Если распознан монтаж EEGBCI с полным
сенсомоторным набором, сохраняется прежний порядок 17 каналов; для других
монтажей сохраняются все EEG-каналы в порядке записи. Ресэмплинг до 160 Гц
выполняется существующим адаптером MNE и записывается в журнал преобразований.
Исходная запись не изменяется.

MCP рекурсивно индексирует EDF/EDF+ и BDF под `datasets/raw`, а пары F32/JSON
под `datasets/processed`. Вызов `neuro.load_recording` принимает ID из
`neuro.list_recordings` или полный путь только внутри одной из этих папок.
Символические ссылки, ведущие наружу, отклоняются. Подготовленный F32
проверяется по JSON, размерам, точному соседнему пути и SHA-256. Сырые файлы
импортируются асинхронно; загрузка не запускает воспроизведение и управление.
Модель затем проверяется по частоте и точному порядку каналов.

## Qt client

Open `Window → NeuroControl`. Choose an EDF/BDF using `Запись EDF…`; the UI runs
`tools/prepare_recording.py` asynchronously and loads the generated signals and
events when conversion finishes. This step needs a Python environment with MNE
and NumPy. For the MCP importer, use the Python environment with MNE, NumPy,
and SciPy. Then load a compatible model such as `models/S001_feet_vs_rest.json`
and start playback. The panel displays the
EEG trace, state, confidence and diagnostics. Avatar control is off by default
and requires the explicit checkbox. BrainFlow Synthetic Board is a signal-path
check; it cannot move the avatar. The direct live EEG driver remains a
placeholder until hardware and channel calibration exist. LSL live and saved
LSL recordings support visualization, diagnostics and local recording without
classifier or avatar control. Test turn buttons use a distinct
`SimulatedTest` source and can include forward motion. Ordinary keyboard and
gamepad input retain priority. A stale estimate, poor signal, hidden panel,
playback end, or user stop command removes NeuroControl movement.

## Build boundary

Build the Windows Qt `gui_client` only (`USE_SDL=OFF`). The BrainFlow C++
binding and its runtime DLLs are linked and copied by `gui_client/CMakeLists.txt`.
The subtree contains a narrow Windows change in `get_dll_dir.h`: it replaces
ATL string use with WinAPI because the local Build Tools lacks `atlstr.h`.
Do not build a Windows server, SDL or Emscripten client for this prototype.
Do not launch the installed client or publish a release as part of this work.

## Sources and limits

- [PhysioNet EEGMMIDB v1.0.0](https://physionet.org/content/eegmmidb/1.0.0/), ODC Attribution 1.0, DOI 10.13026/C28G6P.
- [BrainFlow board documentation](https://brainflow.readthedocs.io/en/stable/SupportedBoards.html): Synthetic Board generates artificial data; Playback File Board replays BrainFlow files.
- [MNE EEGBCI decoding example](https://mne.tools/stable/auto_examples/decoding/decoding_csp_eeg.html) for CSP background.

The classifier distinguishes both-feet imagery from rest in a small S001
experiment. This does not establish recognition of natural walking or turns.
No physical headset, person-specific calibration, or independently labelled
turning imagery has been tested. It is not a medical device.
The [external EEG architecture](docs/MEDICAL_EEG_ARCHITECTURE.md) records the
existing adapters and the next calibration/LSL design boundary.
