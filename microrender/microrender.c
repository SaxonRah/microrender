#include "pico/stdlib.h"

#ifndef MR_PICO_USB_STDIO
#define MR_PICO_USB_STDIO 0
#endif
#ifndef MR_PICO_GAME_SERIAL
#define MR_PICO_GAME_SERIAL 0
#endif
#ifndef MR_STRESS_PICO_SERIAL
#define MR_STRESS_PICO_SERIAL 0
#endif

#if MR_PICO_USB_STDIO
#include "pico/stdio_usb.h"
#include <stdio.h>
#endif

#if defined(MR_STRESS_TEST) && MR_STRESS_TEST
#include "mr_pico_stress_demo.h"
#else
#include "mr_pico_demo.h"
#endif

int main(void) {
#if MR_PICO_USB_STDIO
  /* The screenshot receiver retries its command while USB enumerates, so
   * screenshot-only builds do not need an artificial boot delay. Verbose
   * serial builds retain a short delay so their startup banner is visible. */
  stdio_init_all();
  /* Binary RGB565 screenshots must not expand 0x0A bytes to CRLF. */
  stdio_set_translate_crlf(&stdio_usb, false);
#if MR_PICO_GAME_SERIAL || MR_STRESS_PICO_SERIAL
  sleep_ms(1500);
  printf("\nMicroRender RP2350 / Pico 2 bring-up\n");
  printf("ILI9341 SPI RGB565 renderer\n");
#endif
#endif

#if defined(MR_STRESS_TEST) && MR_STRESS_TEST
  mr_pico_stress_demo_main();
#else
  mr_pico_demo_main();
#endif

  while (true)
    tight_loop_contents();
}
