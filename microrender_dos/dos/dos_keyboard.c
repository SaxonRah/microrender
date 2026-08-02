#include "dos_keyboard.h"

#ifndef __WATCOMC__
#error dos_keyboard.c targets Open Watcom 16-bit real mode DOS.
#endif

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <string.h>

/* Key state, one byte per scan code. Written from an interrupt handler and read
   from the main loop, so it is volatile. Bytes are updated atomically on x86,
   which is why this is a byte array rather than a packed bitfield: a
   read-modify-write on a shared bitfield would race with the handler. */
static volatile unsigned char dos_key_state[128];

static void(__interrupt __far *dos_prev_int9)(void);
static int dos_kbd_installed = 0;

/* Extended-key prefix (0xE0) seen on the previous interrupt. The arrow keys on
   a 101-key keyboard send E0 48 rather than the numeric-keypad 48, but both map
   to the same logical direction here, so the prefix is consumed and ignored. */
static volatile unsigned char dos_kbd_e0_pending;

static void __interrupt __far dos_int9_handler(void) {
  unsigned char scan;
  unsigned char code;
  unsigned char released;

  scan = inp(0x60);

  if (scan == 0xE0u) {
    dos_kbd_e0_pending = 1u;
  } else if (scan == 0xE1u) {
    /* Pause/Break prefix: ignore the whole sequence. */
    dos_kbd_e0_pending = 0u;
  } else {
    released = (unsigned char)(scan & 0x80u);
    code = (unsigned char)(scan & 0x7Fu);
    dos_key_state[code] = (unsigned char)(released ? 0u : 1u);
    dos_kbd_e0_pending = 0u;
  }

  /* Chain to the BIOS handler so shift/ctrl/alt flags, the keystroke queue and
     Ctrl-Alt-Del keep working, and so the BIOS issues the EOI and the keyboard
     controller acknowledge. Doing our own EOI here as well would send two. */
  _chain_intr(dos_prev_int9);
}

void dos_keyboard_install(void) {
  if (dos_kbd_installed)
    return;

  memset((void *)dos_key_state, 0, sizeof(dos_key_state));
  dos_kbd_e0_pending = 0u;

  dos_prev_int9 = _dos_getvect(0x09);
  _dos_setvect(0x09, dos_int9_handler);
  dos_kbd_installed = 1;
}

void dos_keyboard_remove(void) {
  if (!dos_kbd_installed)
    return;

  _dos_setvect(0x09, dos_prev_int9);
  dos_kbd_installed = 0;

  /* Drain anything the BIOS queued while we were installed, so the shell does
     not receive a burst of gameplay keystrokes on exit. */
  while (kbhit())
    (void)getch();
}

int dos_key_down(unsigned char scan_code) {
  if (scan_code >= sizeof(dos_key_state))
    return 0;
  return dos_key_state[scan_code] != 0u;
}

void dos_keyboard_clear(void) {
  memset((void *)dos_key_state, 0, sizeof(dos_key_state));
}

int dos_keyboard_installed(void) { return dos_kbd_installed; }
