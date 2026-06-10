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

} // namespace current_lab::net
