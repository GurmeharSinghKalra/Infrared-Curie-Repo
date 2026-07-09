#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Display control task — renders OLED eyes and MAX7219 mouth
// based on current robot state. Handles blinking and low-power dimming.
void task_display(void *arg);

#ifdef __cplusplus
}
#endif
