#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void input_start(void);
void input_receive(const uint8_t* report, size_t size);
#ifdef __cplusplus
}
#endif
