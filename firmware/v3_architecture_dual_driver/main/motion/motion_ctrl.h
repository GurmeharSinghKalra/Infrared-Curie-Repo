#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Motion control task — reads direction/speed/arms from robot state,
// applies acceleration ramping, drives motors and servos.
void task_motion(void *arg);

#ifdef __cplusplus
}
#endif
