# ESP HTTP client component regression

Compiles the actual `lib/fn_esp_http_client` client, header and utility sources
with an existing ESP-IDF HTTP parser. Only the transport is scripted: response
chunks are returned verbatim; a read after they are exhausted records an unwanted
read and returns a timeout immediately. No network, sleeps or downloaded fixtures.
The test does not implement HTTP parsing or duplicate the completion predicate.

## Layout

| File | Purpose |
| --- | --- |
| `ClientTests.cpp` | doctest cases, per-client scripted transport and fail-fast unused auth/async paths |
| `CMakeLists.txt` | Standalone or PC-suite target, linking production sources and existing SDK parser |
| `esp_system.h` | Allocation-check macro and fail-fast unused random path |
| `esp_tls.h` | Plain HTTP declarations; TLS is not compiled |
| `freertos/FreeRTOS.h` | Empty declaration support; synchronous client does not use RTOS services |

CMake generates the empty host `sdkconfig.h` in the build directory.

## Run

Use an existing ESP-IDF source tree containing `components/http_parser` and
`components/tcp_transport/include` (validated with ESP-IDF 5.4). Nothing is fetched.
The standalone target avoids configuring the full PC firmware:

```sh
cmake -S tests/esp_http -B /tmp/esp-http-tests -DESP_HTTP_CLIENT_IDF_PATH="$IDF_PATH"
cmake --build /tmp/esp-http-tests -j2
ctest --test-dir /tmp/esp-http-tests --output-on-failure
```

For the PC suite, pass the same CMake cache variable or set `IDF_PATH` before
configuration. Without an SDK path the PC suite explicitly skips this optional
target; an explicitly supplied invalid path fails configuration. The parser's
include directory precedes `tests/esp_stubs/http_parser.h`, which contains only
method names and cannot exercise parsing.

## Scope

Perform and open/fetch/read exercise GET304/CL123/body0, HEAD and204 controls;
declared representation length remains123 and no DATA callback occurs for those
fixtures. Both a normal204 without CL and a deliberately nonconforming nonzero
CL204 fixture ensure status framing wins. Ordinary200 body delivery and repeated EOF reads,
and same-host redirects to200/304 are controls. The scripted timeout measures
attempted transport reads, not ESP latency or real socket timeout behavior.

The split100 case explicitly characterizes an existing limitation: the API still
returns100 before consuming the separate final200. A passing characterization
does **not** mean informational-response handling is fixed. TLS, authentication,
cross-host redirects, real TCP/RTOS scheduling and hardware are not covered.
Wrapper DATA availability is outside this component test and is unchanged by the
completion fix. Run the same test against the parent client source to see304
completion/read-count failures while ordinary200 remains correct.

## Regression check

Using the same tests and ESP-IDF5.4 parser in separate native host build
directories, parent `5674a013514e6370c0100f5f6295aa2069218ff3` fails18 of206
assertions in the no-body/redirect cases. The candidate passes all206 assertions
in five cases. Ordinary200, normal204 and the known split100 characterization
also pass separately against both sources. Adjacent `HttpHeaderTests.cpp`
passes six cases/85 assertions.

| GET304 / CL123 / body0 | Parent | Candidate |
| --- | ---: | ---: |
| Reads after supplied response, perform | 1 | 0 |
| Reads after supplied response, two streaming reads | 2 | 0 |
| Declared representation length | 123 | 123 |
| DATA bytes | 0 | 0 |

These are deterministic component results, not hardware timings or full firmware
build results. CI needs an existing SDK path to run this optional target.
