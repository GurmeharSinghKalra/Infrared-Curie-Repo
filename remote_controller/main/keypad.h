#ifndef KEYPAD_H
#define KEYPAD_H

#ifdef __cplusplus
extern "C" {
#endif

void keypad_init(void);
char keypad_get_key(void);

#ifdef __cplusplus
}
#endif

#endif // KEYPAD_H
