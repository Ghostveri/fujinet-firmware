# tests/esp_http

Host regression tests for ESP HTTP no-body completion over a scripted transport.

## Layout

| File | Defines |
| --- | --- |
| `ClientTests.cpp` | doctest cases, per-client transport and unused auth/async traps |
| `CMakeLists.txt` | Standalone and PC-suite target; generates build-local `sdkconfig.h` |
| `esp_system.h` | Allocation-check macro and unused random trap |
| `esp_tls.h` | Empty plain-HTTP declaration support |
| [freertos/](freertos/) | FreeRTOS include support without scheduling |

## How it fits

`esp_http_client_tests` links the real [ESP client](../../lib/fn_esp_http_client/),
header utilities and ESP-IDF parser with [doctest](../../components_pc/doctest/).
It reuses [ESP stubs](../esp_stubs/); transport exhaustion records an unwanted
read and returns a timeout without network access or sleeps.

## Build

Host only; not linked into firmware. From the repository root, use an existing
ESP-IDF tree with `components/http_parser` and `components/tcp_transport/include`
(ESP-IDF 5.4 supported):

```sh
cmake -S tests/esp_http -B /tmp/esp-http-tests -DESP_HTTP_CLIENT_IDF_PATH="$IDF_PATH"
cmake --build /tmp/esp-http-tests -j2
ctest --test-dir /tmp/esp-http-tests --output-on-failure
```

[tests/CMakeLists.txt](../CMakeLists.txt) registers it for POSIX hosts with BSD
queue headers when `ESP_HTTP_CLIENT_IDF_PATH` or `IDF_PATH` is set; otherwise it
is skipped. Nothing is downloaded. SDK parser headers precede the method-only stub.

## Notes

Both APIs cover GET304/CL123/body0, HEAD200 and ordinary204, preserving metadata
and checking repeated reads. Ordinary200 bodies and same-host redirects to200/304
are controls. TLS, authentication, cross-host redirects, informational responses
(including split100), wrapper availability, real TCP/RTOS and hardware are excluded.
