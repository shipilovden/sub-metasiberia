# Classification

`processing/NeuroClassifier` executes the exported causal SOS 8–30 Hz filter,
CSP log-power features and LDA model in native C++. `tools/prepare_train.py`
trains the model with MNE and scikit-learn. The C++ and Python paths are
compared on the held-out real recording by `tests/check_cpp_parity.py`.
