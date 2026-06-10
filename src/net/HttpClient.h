#pragma once

#include <string>

// Minimal blocking HTTP/1.1 client over POSIX sockets. Plain HTTP only —
// intended for localhost services (AnkiConnect, llama.cpp / vLLM servers).
// For https endpoints run a local proxy; this stays dependency-free.
namespace current_lab::net {

struct HttpResponse {
    int status = 0;
    std::string body;
};

bool httpPost(const std::string& host, int port, const std::string& path,
              const std::string& body, const std::string& contentType,
              const std::string& extraHeaders, // raw "Header: value\r\n" lines, may be empty
              HttpResponse* out, std::string* error = nullptr, int timeoutMs = 15000);

// Pure request/response logic, split out of httpPost for unit testing.

// Assembles the exact bytes httpPost sends on the wire.
std::string buildHttpPostRequest(const std::string& host, int port, const std::string& path,
                                 const std::string& body, const std::string& contentType,
                                 const std::string& extraHeaders);

// Parses a raw HTTP/1.x response: status line, headers, body; undoes chunked
// transfer-encoding ("good enough for local APIs" — same scope as before).
bool parseHttpResponse(const std::string& raw, HttpResponse* out, std::string* error = nullptr);

} // namespace current_lab::net
