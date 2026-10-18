#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define ESP_STATIC_ANALYZER_CHECK(condition, result) do { if (condition) return result; } while (0)
static inline uint32_t esp_random(void) { abort(); }
