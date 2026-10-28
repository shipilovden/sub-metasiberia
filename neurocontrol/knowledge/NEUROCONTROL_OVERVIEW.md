# MetaSiberia NeuroControl

NeuroControl is a research feature of the MetaSiberia Qt client for viewing EEG signals, inspecting supported data sources, and evaluating research classifiers. Available features depend on the client build and its installed runtime resources.

Supported source types include prerecorded EEG, a synthetic test source, live LSL streams, and saved LSL recordings. Source selection and connection are explicit actions. Live LSL supports display, diagnostics, recording, and an opt-in shadow classifier. The shadow path maps exact EEG channel names into model order, converts supported voltage units to volts, checks five seconds of measured receive rate against the nominal rate, and uses a causal anti-alias FIR resampler when model and input rates differ. Montage/reference metadata must agree, or the operator must explicitly attest to a missing contract; known mismatches block classification. This mode reports diagnostic predictions and never sends avatar commands. Poor source timestamps remain unverified and cannot support device-latency claims. Saved LSL playback remains visualization-only.

Model output is specific to the selected model and input data. It is not a literal reading of thoughts or proof of a user's intention. NeuroControl is not a medical device and is not intended for diagnosis, treatment, or control of physical equipment.
