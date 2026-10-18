#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include "esp_transport_tcp.h"
#include "fn_esp_http_client.h"

// Only transport is scripted. The client, headers and ESP-IDF parser are real.
// An exhausted response behaves like a held-open peer reaching its read timeout.
struct esp_transport_item_t
{
    int port = 80;
    int reads_past_response = 0;
    std::deque<std::string> chunks;
    std::string request;
};
struct esp_transport_list_t
{
    std::unique_ptr<esp_transport_item_t> tcp;
};

extern "C"
{
esp_transport_list_handle_t esp_transport_list_init() { return new esp_transport_list_t; }
esp_transport_handle_t esp_transport_tcp_init() { return new esp_transport_item_t; }
esp_err_t esp_transport_list_add(esp_transport_list_handle_t list, esp_transport_handle_t tcp,
                                 const char *scheme)
{
    REQUIRE(std::string(scheme) == "http");
    list->tcp.reset(tcp);
    return ESP_OK;
}
esp_err_t esp_transport_list_destroy(esp_transport_list_handle_t list)
{
    delete list;
    return ESP_OK;
}
esp_transport_handle_t esp_transport_list_get_transport(esp_transport_list_handle_t list,
                                                       const char *scheme)
{
    REQUIRE(std::string(scheme) == "http");
    return list->tcp.get();
}
esp_err_t esp_transport_set_default_port(esp_transport_handle_t t, int port)
{
    t->port = port;
    return ESP_OK;
}
int esp_transport_get_default_port(esp_transport_handle_t t) { return t->port; }
int esp_transport_connect(esp_transport_handle_t, const char *, int, int) { return 0; }
int esp_transport_connect_async(esp_transport_handle_t, const char *, int, int) { std::abort(); }
int esp_transport_close(esp_transport_handle_t) { return 0; }
int esp_transport_get_errno(esp_transport_handle_t) { return ETIMEDOUT; }
esp_tls_error_handle_t esp_transport_get_error_handle(esp_transport_handle_t) { return nullptr; }
int esp_transport_write(esp_transport_handle_t t, const char *data, int len, int)
{
    t->request.append(data, len);
    return len;
}
int esp_transport_read(esp_transport_handle_t t, char *data, int len, int)
{
    if (t->chunks.empty())
    {
        ++t->reads_past_response;
        errno = ETIMEDOUT;
        return -1;
    }
    auto &chunk = t->chunks.front();
    int count = std::min(len, static_cast<int>(chunk.size()));
    std::memcpy(data, chunk.data(), count);
    chunk.erase(0, count);
    if (chunk.empty())
        t->chunks.pop_front();
    return count;
}
}

namespace fujinet
{
char *http_auth_basic(const char *, const char *) { std::abort(); }
char *http_auth_digest(const char *, const char *, esp_http_auth_data_t *) { std::abort(); }
}

using namespace fujinet;

struct Session
{
    struct Cleanup
    {
        void operator()(esp_http_client *client) const { esp_http_client_cleanup(client); }
    };
    std::string body;
    std::string etag;
    int finishes = 0;
    std::unique_ptr<esp_http_client, Cleanup> client;

    explicit Session(esp_http_client_method_t method = HTTP_METHOD_GET)
    {
        esp_http_client_config_t config{};
        config.url = "http://example.invalid/resource";
        config.method = method;
        config.user_data = this;
        config.event_handler = [](esp_http_client_event_t *event) -> esp_err_t
        {
            auto &session = *static_cast<Session *>(event->user_data);
            if (event->event_id == HTTP_EVENT_ON_DATA)
                session.body.append(static_cast<char *>(event->data), event->data_len);
            if (event->event_id == HTTP_EVENT_ON_FINISH)
                ++session.finishes;
            if (event->event_id == HTTP_EVENT_ON_HEADER &&
                std::string(event->header_key) == "ETag")
                session.etag = event->header_value;
            return ESP_OK;
        };
        client.reset(esp_http_client_init(&config));
        REQUIRE(client != nullptr);
    }
    esp_transport_item_t &peer() { return *client->transport_list->tcp; }
};

TEST_CASE("final no-body responses do not read beyond their headers")
{
    for (auto method : {HTTP_METHOD_GET, HTTP_METHOD_HEAD})
        for (int status : {200, 204, 304})
        {
            if (method == HTTP_METHOD_GET && status == 200)
                continue;
            for (int stream : {0, 1})
            {
                CAPTURE(method);
                CAPTURE(status);
                CAPTURE(stream);
                Session session(method);
                session.peer().chunks = {"HTTP/1.1 " + std::to_string(status) +
                    " Test\r\nContent-Length: 123\r\nETag: retained\r\n\r\n"};
                auto client = session.client.get();
                if (stream)
                {
                    REQUIRE(esp_http_client_open(client, 0) == ESP_OK);
                    CHECK(esp_http_client_fetch_headers(client) == 123);
                    char output[8];
                    CHECK(esp_http_client_read(client, output, sizeof(output)) == 0);
                    CHECK(esp_http_client_read(client, output, sizeof(output)) == 0);
                }
                else
                {
                    CHECK(esp_http_client_perform(client) == ESP_OK);
                    CHECK(session.finishes == 1);
                }
                CHECK(esp_http_client_get_status_code(client) == status);
                CHECK(esp_http_client_get_content_length(client) == 123);
                CHECK(client->response->data_process == 0);
                CHECK(session.body.empty());
                CHECK(session.etag == "retained");
                CHECK(client->is_chunk_complete);
                CHECK(session.peer().reads_past_response == 0);
                CHECK(session.peer().request.find(method == HTTP_METHOD_HEAD ? "HEAD " : "GET ") == 0);
            }
        }
}

TEST_CASE("204 without Content-Length completes without a body read")
{
    for (int stream : {0, 1})
    {
        Session session;
        session.peer().chunks = {"HTTP/1.1 204 No Content\r\n\r\n"};
        auto client = session.client.get();
        if (stream)
        {
            REQUIRE(esp_http_client_open(client, 0) == ESP_OK);
            REQUIRE(esp_http_client_fetch_headers(client) >= 0);
            char output[8];
            CHECK(esp_http_client_read(client, output, sizeof(output)) == 0);
        }
        else
            CHECK(esp_http_client_perform(client) == ESP_OK);
        CHECK(esp_http_client_get_status_code(client) == 204);
        CHECK(client->is_chunk_complete);
        CHECK(session.body.empty());
        CHECK(session.peer().reads_past_response == 0);
    }
}

TEST_CASE("ordinary 200 body is delivered through perform and streaming read")
{
    for (int stream : {0, 1})
    {
        Session session;
        session.peer().chunks = {"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n", "abc"};
        auto client = session.client.get();
        if (stream)
        {
            REQUIRE(esp_http_client_open(client, 0) == ESP_OK);
            REQUIRE(esp_http_client_fetch_headers(client) == 3);
            char output[8]{};
            CHECK(esp_http_client_read(client, output, sizeof(output)) == 3);
            CHECK(std::string(output, 3) == "abc");
            CHECK(esp_http_client_read(client, output, sizeof(output)) == 0);
        }
        else
        {
            CHECK(esp_http_client_perform(client) == ESP_OK);
            CHECK(session.finishes == 1);
        }
        CHECK(session.body == "abc");
        CHECK(client->response->data_process == 3);
        CHECK(esp_http_client_get_content_length(client) == 3);
        CHECK(session.peer().reads_past_response == 0);
    }
}

TEST_CASE("redirected final 304 and 200 retain their completion semantics")
{
    for (int status : {200, 304})
    {
        Session session;
        session.peer().chunks = {
            "HTTP/1.1 302 Found\r\nLocation: /target\r\nContent-Length: 0\r\n\r\n",
            "HTTP/1.1 " + std::to_string(status) + " Test\r\nContent-Length: " +
                (status == 304 ? "123\r\n\r\n" : "3\r\n\r\nabc")};
        auto client = session.client.get();
        CHECK(esp_http_client_perform(client) == ESP_OK);
        CHECK(esp_http_client_get_status_code(client) == status);
        CHECK(esp_http_client_get_content_length(client) == (status == 304 ? 123 : 3));
        CHECK(session.body == (status == 304 ? "" : "abc"));
        CHECK(session.finishes == 2);
        CHECK(session.peer().request.find("GET /target HTTP/1.1") != std::string::npos);
        CHECK(session.peer().reads_past_response == 0);
    }
}

TEST_CASE("split 100 remains a known limitation, not final-response coverage")
{
    Session session;
    session.peer().chunks = {"HTTP/1.1 100 Continue\r\n\r\n",
                            "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"};
    CHECK(esp_http_client_perform(session.client.get()) == ESP_OK);
    // Characterize existing premature completion; do not call this HTTP correctness.
    CHECK(esp_http_client_get_status_code(session.client.get()) == 100);
    CHECK(session.peer().chunks.size() == 1);
    CHECK(session.body.empty());
}
