#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace neurocontrol {

enum class CommandType { Stop, Forward, TurnLeft, TurnRight };
enum class SignalQuality { Unknown, Good, Poor, Lost };
enum class CommandSource { RecordedEeg, SyntheticEeg, LiveEeg, SimulatedTest };

constexpr double kNeuroDataHeartbeatTimeoutSeconds = 1.25;

struct EEGDataHeartbeat {
    std::uint64_t stream_generation = 0;
    std::uint64_t sample_index = 0;
    double timestamp_seconds = 0.0; // monotonic client clock, updated after processing a sample
    bool available = false;
    bool valid = false;
};

struct Command {
    CommandType type = CommandType::Stop;
    double confidence = 0.0;
    double timestamp_seconds = 0.0; // monotonic client clock
    SignalQuality quality = SignalQuality::Unknown;
    CommandSource source = CommandSource::RecordedEeg;
    bool forward_held = false; // permits simultaneous forward and turn
    bool experimental_turn_mapping = false; // explicit class-to-turn mapping from a model
    double minimum_confidence = 0.75; // effective threshold after the controller safety floor
    std::uint64_t event_id = 0; // correlates classification, safety decision and avatar input
    double maximum_age_seconds = kNeuroDataHeartbeatTimeoutSeconds; // classifier cadence lease; legacy default
    std::uint64_t stream_generation = 0; // prevents commands from a previous recording/model/seek
    std::uint64_t sample_index = 0; // last EEG sample used by this classification
    double sample_timestamp_seconds = 0.0; // scheduled monotonic source time for that sample
};

struct MotionDecision {
    bool forward = false;
    int turn = 0; // -1 left, +1 right; RecordedEeg needs an explicit experimental mapping
};

enum class MotionBlockReason {
    None,
    ControllerDisabled,
    PhysicalInputHasPriority,
    AvatarMovementDisallowed,
    CommandStaleOrClockInvalid,
    EEGDataUnavailable,
    EEGDataStale,
    ClassifierDelayed,
    StreamGenerationMismatch,
    MotionCommandNotRearmed,
    SignalQualityNotGood,
    SyntheticSourceCannotControl,
    ConfidenceBelowThreshold,
    StopCommand,
    TurnMappingNotEnabled
};

struct MotionEvaluation {
    MotionDecision motion;
    MotionBlockReason reason = MotionBlockReason::None;
    double command_age_seconds = 0.0;
    double eeg_data_age_seconds = 0.0;
    bool passed_safety = false;
};

struct ControllerTrace {
    std::uint64_t event_id = 0;
    double evaluated_at_seconds = 0.0;
    double command_age_seconds = 0.0;
    double classifier_maximum_age_seconds = kNeuroDataHeartbeatTimeoutSeconds;
    double eeg_data_age_seconds = 0.0;
    double classification_input_age_seconds = 0.0;
    std::uint64_t stream_generation = 0;
    std::uint64_t heartbeat_sample_index = 0;
    std::uint64_t command_sample_index = 0;
    double observed_ground_speed_mps = 0.0;
    bool enabled = false;
    bool physical_input_active = false;
    bool avatar_movement_allowed = false;
    bool eeg_heartbeat_available = false;
    bool eeg_heartbeat_valid = false;
    bool motion_command_armed = false;
    bool passed_safety = false;
    bool forward_input_applied = false;
    int turn_input_applied = 0;
    MotionBlockReason reason = MotionBlockReason::None;
};

inline MotionEvaluation evaluateMotionWithMaximumAge(const Command& command, bool enabled,
    bool physical_input_active, bool avatar_movement_allowed, double now_seconds, double maximum_age_seconds)
{
    MotionEvaluation result;
    result.command_age_seconds = now_seconds - command.timestamp_seconds;
    if(!enabled) result.reason = MotionBlockReason::ControllerDisabled;
    else if(physical_input_active) result.reason = MotionBlockReason::PhysicalInputHasPriority;
    else if(!avatar_movement_allowed) result.reason = MotionBlockReason::AvatarMovementDisallowed;
    else if(!std::isfinite(result.command_age_seconds) || result.command_age_seconds < 0.0 ||
        !std::isfinite(maximum_age_seconds) || maximum_age_seconds <= 0.0 || result.command_age_seconds > maximum_age_seconds)
        result.reason = MotionBlockReason::CommandStaleOrClockInvalid;
    else if(command.quality != SignalQuality::Good) result.reason = MotionBlockReason::SignalQualityNotGood;
    else if(command.source == CommandSource::SyntheticEeg) result.reason = MotionBlockReason::SyntheticSourceCannotControl;
    else if(!std::isfinite(command.confidence) || !std::isfinite(command.minimum_confidence) ||
        command.confidence < std::max(0.75, command.minimum_confidence))
        result.reason = MotionBlockReason::ConfidenceBelowThreshold;
    else if(command.type == CommandType::Stop) result.reason = MotionBlockReason::StopCommand;
    else
    {
        result.passed_safety = true;
        result.motion.forward = command.type == CommandType::Forward ||
            (command.source == CommandSource::SimulatedTest && command.forward_held &&
             (command.type == CommandType::TurnLeft || command.type == CommandType::TurnRight));
        if(command.source == CommandSource::SimulatedTest ||
            (command.source == CommandSource::RecordedEeg && command.experimental_turn_mapping))
        {
            if(command.type == CommandType::TurnLeft) result.motion.turn = -1;
            if(command.type == CommandType::TurnRight) result.motion.turn = 1;
        }
        if(!result.motion.forward && result.motion.turn == 0)
        {
            result.passed_safety = false;
            result.reason = MotionBlockReason::TurnMappingNotEnabled;
        }
    }
    return result;
}

inline MotionEvaluation evaluateMotion(const Command& command, bool enabled,
    bool physical_input_active, bool avatar_movement_allowed, double now_seconds)
{
    return evaluateMotionWithMaximumAge(command, enabled, physical_input_active,
        avatar_movement_allowed, now_seconds, kNeuroDataHeartbeatTimeoutSeconds);
}

inline MotionEvaluation evaluateRecordedEEGMotion(const Command& command, const EEGDataHeartbeat& heartbeat,
    std::uint64_t current_stream_generation, bool motion_command_armed, bool enabled,
    bool physical_input_active, bool avatar_movement_allowed, double now_seconds)
{
    MotionEvaluation result;
    result.command_age_seconds = now_seconds - command.timestamp_seconds;
    result.eeg_data_age_seconds = now_seconds - heartbeat.timestamp_seconds;
    if(!enabled) result.reason = MotionBlockReason::ControllerDisabled;
    else if(physical_input_active) result.reason = MotionBlockReason::PhysicalInputHasPriority;
    else if(!avatar_movement_allowed) result.reason = MotionBlockReason::AvatarMovementDisallowed;
    else if(command.stream_generation != current_stream_generation || heartbeat.stream_generation != current_stream_generation)
        result.reason = MotionBlockReason::StreamGenerationMismatch;
    else if(!heartbeat.available || !heartbeat.valid) result.reason = MotionBlockReason::EEGDataUnavailable;
    else if(!std::isfinite(result.eeg_data_age_seconds) || result.eeg_data_age_seconds < 0.0 ||
        result.eeg_data_age_seconds > kNeuroDataHeartbeatTimeoutSeconds)
        result.reason = MotionBlockReason::EEGDataStale;
    else if(!std::isfinite(result.command_age_seconds) || result.command_age_seconds < 0.0 ||
        !std::isfinite(command.maximum_age_seconds) || command.maximum_age_seconds <= 0.0 ||
        result.command_age_seconds > command.maximum_age_seconds)
        result.reason = MotionBlockReason::ClassifierDelayed;
    else if(command.type != CommandType::Stop && !motion_command_armed)
        result.reason = MotionBlockReason::MotionCommandNotRearmed;
    else
        return evaluateMotionWithMaximumAge(command, enabled, physical_input_active,
            avatar_movement_allowed, now_seconds, command.maximum_age_seconds);
    return result;
}

inline MotionDecision decideMotion(const Command& command, bool enabled,
    bool physical_input_active, bool avatar_movement_allowed, double now_seconds)
{
    return evaluateMotion(command, enabled, physical_input_active, avatar_movement_allowed, now_seconds).motion;
}

} // namespace neurocontrol
