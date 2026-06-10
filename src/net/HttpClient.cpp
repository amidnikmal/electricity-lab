#include "net/HttpClient.h"

#include <cstring>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace current_lab::net {

namespace {

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

// Thin platform shims so the request/response logic below stays single-path.
#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;

bool initSockets() {
    static const bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}

void closeSocket(socket_t s) { closesocket(s); }

void setSocketTimeouts(socket_t s, int timeoutMs) {
    DWORD tv = static_cast<DWORD>(timeoutMs); // Windows expects milliseconds
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
}

long sendSome(socket_t s, const char* data, size_t len) {
    return send(s, data, static_cast<int>(len), 0);
}

long recvSome(socket_t s, char* buf, size_t len) {
    return recv(s, buf, static_cast<int>(len), 0);
}
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;

bool initSockets() { return true; }

void closeSocket(socket_t s) { close(s); }

void setSocketTimeouts(socket_t s, int timeoutMs) {
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

long sendSome(socket_t s, const char* data, size_t len) {
    return static_cast<long>(send(s, data, len, 0));
}

long recvSome(socket_t s, char* buf, size_t len) {
    return static_cast<long>(recv(s, buf, len, 0));
}
#endif

} // namespace

bool httpPost(const std::string& host, int port, const std::string& path,
              const std::string& body, const std::string& contentType,
              const std::string& extraHeaders, HttpResponse* out,
              std::string* error, int timeoutMs) {
    if (!initSockets())
        return fail(error, "socket subsystem init failed");

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0 || !result)
        return fail(error, "cannot resolve host " + host);

    socket_t fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd == kInvalidSocket) {
        freeaddrinfo(result);
        return fail(error, "cannot create socket");
    }

    setSocketTimeouts(fd, timeoutMs);

    bool connected =
        connect(fd, result->ai_addr, static_cast<int>(result->ai_addrlen)) == 0;
    freeaddrinfo(result);
    if (!connected) {
        closeSocket(fd);
        return fail(error, "cannot connect to " + host + ":" + portStr);
    }

    std::string data = buildHttpPostRequest(host, port, path, body, contentType, extraHeaders);
    size_t sent = 0;
    while (sent < data.size()) {
        long n = sendSome(fd, data.data() + sent, data.size() - sent);
        if (n <= 0) {
            closeSocket(fd);
            return fail(error, "send failed");
        }
        sent += static_cast<size_t>(n);
    }

    std::string raw;
    char buf[4096];
    for (;;) {
        long n = recvSome(fd, buf, sizeof(buf));
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
    }
    closeSocket(fd);

    if (raw.empty())
        return fail(error, "empty response (timeout?)");

    return parseHttpResponse(raw, out, error);
}

std::string buildHttpPostRequest(const std::string& host, int port, const std::string& path,
                                 const std::string& body, const std::string& contentType,
                                 const std::string& extraHeaders) {
    std::ostringstream request;
    // HTTP numbers must not pick up locale digit grouping ("Content-Length: 1 234").
    request.imbue(std::locale::classic());
    request << "POST " << path << " HTTP/1.1\r\n"
            << "Host: " << host << ":" << port << "\r\n"
            << "Content-Type: " << contentType << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n"
            << extraHeaders
            << "\r\n"
            << body;
    return request.str();
}

bool parseHttpResponse(const std::string& raw, HttpResponse* out, std::string* error) {
    size_t statusEnd = raw.find("\r\n");
    if (statusEnd == std::string::npos || raw.compare(0, 5, "HTTP/") != 0)
        return fail(error, "malformed response");

    int status = 0;
    {
        size_t space = raw.find(' ');
        if (space == std::string::npos || space > statusEnd)
            return fail(error, "malformed status line");
        status = std::atoi(raw.c_str() + space + 1);
    }

    size_t headerEnd = raw.find("\r\n\r\n");
    std::string bodyOut = headerEnd == std::string::npos ? "" : raw.substr(headerEnd + 4);

    // Undo chunked transfer-encoding if present (good enough for local APIs).
    std::string headers = raw.substr(0, headerEnd == std::string::npos ? statusEnd : headerEnd);
    if (headers.find("Transfer-Encoding: chunked") != std::string::npos ||
        headers.find("transfer-encoding: chunked") != std::string::npos) {
        std::string decoded;
        size_t pos = 0;
        while (pos < bodyOut.size()) {
            size_t lineEnd = bodyOut.find("\r\n", pos);
            if (lineEnd == std::string::npos) break;
            long chunkLen = std::strtol(bodyOut.c_str() + pos, nullptr, 16);
            if (chunkLen <= 0) break;
            size_t chunkStart = lineEnd + 2;
            if (chunkStart + static_cast<size_t>(chunkLen) > bodyOut.size()) break;
            decoded.append(bodyOut, chunkStart, static_cast<size_t>(chunkLen));
            pos = chunkStart + static_cast<size_t>(chunkLen) + 2;
        }
        bodyOut = std::move(decoded);
    }

    if (out) {
        out->status = status;
        out->body = std::move(bodyOut);
    }
    return true;
}

} // namespace current_lab::net
