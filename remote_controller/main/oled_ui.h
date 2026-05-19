#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void controller_ui_init(void);
void controller_ui_show_action(const char *label);
void controller_ui_render(bool connected,
                          bool connecting,
                          const char *expression_label,
                          const char *speed_label,
                          int battery_percent);

#ifdef __cplusplus
}
#endif
