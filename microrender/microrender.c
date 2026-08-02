#include "pico/stdlib.h"
#ifndef MR_STRESS_PICO_SERIAL
#define MR_STRESS_PICO_SERIAL 1
#endif
#if MR_STRESS_PICO_SERIAL
#include <stdio.h>
#endif
#if defined(MR_STRESS_TEST) && MR_STRESS_TEST
#include "mr_pico_stress_demo.h"
#else
#include "mr_pico_demo.h"
#endif

int main(void) {
#if MR_STRESS_PICO_SERIAL
  stdio_init_all();

  /* Give USB serial time to enumerate when plugged into a PC. */
  sleep_ms(1500);
  printf("\nMicroRender RP2350 / Pico 2 bring-up\n");
  printf("ILI9341 SPI RGB565 tiled DMA test\n");
#endif

#if defined(MR_STRESS_TEST) && MR_STRESS_TEST
  mr_pico_stress_demo_main();
#else
  mr_pico_demo_main();
#endif

  while (true) {
    tight_loop_contents();
  }
}
