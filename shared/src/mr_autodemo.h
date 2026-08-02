#ifndef MR_AUTODEMO_H
#define MR_AUTODEMO_H

#include "mr_demo_input.h"

#ifdef __cplusplus
extern "C" {
#endif

void mr_autodemo_reset(void);
void mr_autodemo_input(unsigned long frame, mr_demo_input_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MR_AUTODEMO_H */
