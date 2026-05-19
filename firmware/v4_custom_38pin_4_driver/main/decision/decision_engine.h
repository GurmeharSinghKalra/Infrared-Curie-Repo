#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// The decision engine task — the brain of the robot.
// Consumes events from the event bus, validates them against the state machine,
// and writes to the shared robot state. This is the ONLY module that mutates state.
void task_decision(void *arg);

#ifdef __cplusplus
}
#endif
