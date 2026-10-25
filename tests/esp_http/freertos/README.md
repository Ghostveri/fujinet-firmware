# tests/esp_http/freertos

FreeRTOS include support for the synchronous host HTTP client tests.

## Layout

| File | Defines |
| --- | --- |
| `FreeRTOS.h` | Empty include support; no scheduler implementation |

## How it fits

The [ESP client header](../../../lib/fn_esp_http_client/fn_esp_http_client.h)
includes it through the [component test](../) include path.

## Build

Used only by the host `esp_http_client_tests` target; firmware uses ESP-IDF FreeRTOS.
