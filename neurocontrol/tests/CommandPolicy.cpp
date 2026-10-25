#include "NeuroCommand.h"
#include <iostream>

int main()
{
    using namespace neurocontrol;
    Command command;
    command.type = CommandType::Forward;
    command.confidence = 0.9;
    command.timestamp_seconds = 10.0;
    command.quality = SignalQuality::Good;
    command.source = CommandSource::RecordedEeg;
    if(!decideMotion(command, true, false, true, 10.5).forward) return 1;
    if(decideMotion(command, false, false, true, 10.5).forward) return 2;
    if(decideMotion(command, true, true, true, 10.5).forward) return 3;
    if(decideMotion(command, true, false, false, 10.5).forward) return 4;
    if(decideMotion(command, true, false, true, 11.3).forward) return 5;
    command.quality = SignalQuality::Poor;
    if(decideMotion(command, true, false, true, 10.5).forward) return 6;
    command.quality = SignalQuality::Good;
    command.source = CommandSource::SyntheticEeg;
    if(decideMotion(command, true, false, true, 10.5).forward) return 7;
    command.source = CommandSource::RecordedEeg;
    command.type = CommandType::TurnLeft;
    if(decideMotion(command, true, false, true, 10.5).turn != 0) return 8;
    command.experimental_turn_mapping = true;
    if(decideMotion(command, true, false, true, 10.5).turn != -1) return 9;
    command.type = CommandType::TurnRight;
    if(decideMotion(command, true, false, true, 10.5).turn != 1) return 10;
    command.experimental_turn_mapping = false;
    command.source = CommandSource::SimulatedTest;
    command.forward_held = true;
    const auto motion = decideMotion(command, true, false, true, 10.5);
    if(!motion.forward || motion.turn != 1) return 11;
    command.type = CommandType::TurnRight;
    if(decideMotion(command, true, false, true, 10.5).turn != 1) return 12;
    command.type = CommandType::Stop;
    if(decideMotion(command, true, false, true, 10.5).forward) return 13;
    command.type = CommandType::Forward;
    command.minimum_confidence = 0.80;
    command.confidence = 0.79;
    auto evaluation = evaluateMotion(command, true, false, true, 10.5);
    if(evaluation.motion.forward || evaluation.passed_safety || evaluation.reason != MotionBlockReason::ConfidenceBelowThreshold) return 14;
    command.minimum_confidence = 0.75;
    command.confidence = 0.75;
    evaluation = evaluateMotion(command, true, false, true, 10.5);
    if(!evaluation.motion.forward || !evaluation.passed_safety || evaluation.reason != MotionBlockReason::None) return 15;
    evaluation = evaluateMotion(command, false, false, true, 10.5);
    if(evaluation.reason != MotionBlockReason::ControllerDisabled) return 16;
    evaluation = evaluateMotion(command, true, true, true, 10.5);
    if(evaluation.reason != MotionBlockReason::PhysicalInputHasPriority) return 17;
    evaluation = evaluateMotion(command, true, false, false, 10.5);
    if(evaluation.reason != MotionBlockReason::AvatarMovementDisallowed) return 18;
    evaluation = evaluateMotion(command, true, false, true, 11.3);
    if(evaluation.reason != MotionBlockReason::CommandStaleOrClockInvalid) return 19;
    command.type = CommandType::Stop;
    command.confidence = 1.0;
    evaluation = evaluateMotion(command, true, false, true, 10.5);
    if(evaluation.reason != MotionBlockReason::StopCommand || evaluation.motion.forward) return 20;
    std::cout << "Command policy: source, focus, staleness, signal quality, threshold floor, STOP and turns passed\n";
    return 0;
}
