
#ifndef _KEYBOARD_H
#define _KEYBOARD_H

enum {
  eKeyMemoryMapped = 0,
  eKeyPortMapped,
};

extern byte g_byKeyboardMode;

void InitKeyboard(void);
void ClearKeyboardMemory(void);
void SimulateKeyDown(unsigned int c);
void SimulateKeyUp(unsigned int  c);

#endif

/* END OF FILE */
