#include "net/HttpClient.h"

#include <cstring>
#include <sstream>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace current_lab::net {

namespace {

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

} // namespace

bool httpPost(const std::string& host, int port, const std::string& path,
              const std::string& body, const std::string& contentType,
              const std::string& extraHeaders, HttpResponse* out,
              std::string* error, int timeoutMs) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0 || !result)
        return fail(error, "cannot resolve host " + host);

    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(result);
        return fail(error, "cannot create socket");
    }

    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    bool connected = connect(fd, result->ai_addr, result->ai_addrlen) == 0;
    freeaddrinfo(result);
    if (!connected) {
        close(fd);
        return fail(error, "cannot connect to " + host + ":" + portStr);
    }

    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\n"
            << "Host: " << host << ":" << portStr << "\r\n"
            << "Content-Type: " << contentType << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n"
            << extraHeaders
            << "\r\n"
            << body;

    std::string data = request.str();
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            close(fd);
            return fail(error, "send failed");
        }
        sent += static_cast<size_t>(n);
    }

    std::string raw;
    char buf[4096];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
    }
    close(fd);

    if (raw.empty())
        return fail(error, "empty response (timeout?)");

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
