#ifndef DOS_KEYBOARD_H
#define DOS_KEYBOARD_H

/* Raw INT 9 keyboard state for the MicroRender DOS frontend.
 *
 * The BIOS keystroke queue that kbhit()/getch() read from reports key *events*
 * subject to the typematic repeat delay, not key *state*. A game polling it
 * sees the player stand still for roughly half a second after each direction
 * press before movement begins. This installs a scan-code handler that tracks
 * which keys are physically down, so dos_key_down() answers the question the
 * game actually asks.
 *
 * The previous INT 9 vector is saved and chained to, so the BIOS still updates
 * its own state (shift flags, Ctrl-Alt-Del, the keystroke queue). Callers must
 * pair dos_keyboard_install() with dos_keyboard_remove() before exiting,
 * including on abnormal exit paths, or DOS is left with a dangling vector into
 * freed memory.
 *
 * Open Watcom 16-bit real mode only.
 */

/* Scan codes used by the frontend (set 1, make codes). */
#define DOS_SC_ESC 0x01
#define DOS_SC_1 0x02
#define DOS_SC_Q 0x10
#define DOS_SC_W 0x11
#define DOS_SC_A 0x1E
#define DOS_SC_S 0x1F
#define DOS_SC_D 0x20
#define DOS_SC_P 0x19
#define DOS_SC_ENTER 0x1C
#define DOS_SC_SPACE 0x39
#define DOS_SC_GRAVE 0x29
#define DOS_SC_KP8 0x48
#define DOS_SC_KP2 0x50
#define DOS_SC_KP4 0x4B
#define DOS_SC_KP6 0x4D
#define DOS_SC_UP 0x48
#define DOS_SC_DOWN 0x50
#define DOS_SC_LEFT 0x4B
#define DOS_SC_RIGHT 0x4D

void dos_keyboard_install(void);
void dos_keyboard_remove(void);

/* Non-zero while the key with this scan code is held down. */
int dos_key_down(unsigned char scan_code);

/* Clear all tracked key state. Useful after a mode switch. */
void dos_keyboard_clear(void);

/* Non-zero if the handler is currently installed. */
int dos_keyboard_installed(void);

#endif /* DOS_KEYBOARD_H */
