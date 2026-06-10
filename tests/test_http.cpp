#include <gtest/gtest.h>
#include <locale>
#include <string>
#include "net/HttpClient.h"

using namespace current_lab::net;

// Pure-function tests for buildHttpPostRequest / parseHttpResponse only.
// httpPost does real network I/O and is deliberately not exercised here.

namespace {

std::string chunkedResponse(const std::string& wireBody) {
    return "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + wireBody;
}

HttpResponse mustParse(const std::string& raw) {
    HttpResponse resp;
    std::string err;
    EXPECT_TRUE(parseHttpResponse(raw, &resp, &err)) << "error: " << err;
    return resp;
}

// numpunct facet with thousands grouping, to prove HTTP numbers ignore the
// global locale (the implementation imbues std::locale::classic()).
struct GroupedNumpunct : std::numpunct<char> {
    char do_thousands_sep() const override { return ','; }
    std::string do_grouping() const override { return "\3"; }
};

struct GlobalLocaleGuard {
    std::locale prev;
    explicit GlobalLocaleGuard(const std::locale& loc) : prev(std::locale::global(loc)) {}
    ~GlobalLocaleGuard() { std::locale::global(prev); }
};

} // namespace

// --- buildHttpPostRequest: exact wire bytes ----------------------------------

TEST(BuildHttpPostRequest, ExactRequestBytesWithEmptyExtraHeaders) {
    std::string request = buildHttpPostRequest("127.0.0.1", 8765, "/find?q=a%20b&x=1",
                                               "{\"a\":1}", "application/json", "");
    EXPECT_EQ(request,
              "POST /find?q=a%20b&x=1 HTTP/1.1\r\n"
              "Host: 127.0.0.1:8765\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: 7\r\n"
              "Connection: close\r\n"
              "\r\n"
              "{\"a\":1}");
}

TEST(BuildHttpPostRequest, EmptyBodyHasContentLengthZero) {
    std::string request =
        buildHttpPostRequest("localhost", 11434, "/api/generate", "", "application/json", "");
    EXPECT_EQ(request,
              "POST /api/generate HTTP/1.1\r\n"
              "Host: localhost:11434\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: 0\r\n"
              "Connection: close\r\n"
              "\r\n");
    // With an empty body the request ends exactly at the header terminator.
    EXPECT_EQ(request.substr(request.size() - 4), "\r\n\r\n");
}

TEST(BuildHttpPostRequest, MultiKilobyteBodyContentLengthIsByteCount) {
    std::string body(8192, 'x');
    std::string request = buildHttpPostRequest("h", 80, "/", body, "text/plain", "");
    EXPECT_EQ(request,
              "POST / HTTP/1.1\r\n"
              "Host: h:80\r\n"
              "Content-Type: text/plain\r\n"
              "Content-Length: 8192\r\n"
              "Connection: close\r\n"
              "\r\n" +
                  body);
}

TEST(BuildHttpPostRequest, CrlfBytesInsideBodyAreCountedAndSentVerbatim) {
    std::string body = "ab\r\ncd\r\n"; // 8 bytes, CRLFs included in the count
    std::string request = buildHttpPostRequest("h", 80, "/p", body, "text/plain", "");
    EXPECT_EQ(request,
              "POST /p HTTP/1.1\r\n"
              "Host: h:80\r\n"
              "Content-Type: text/plain\r\n"
              "Content-Length: 8\r\n"
              "Connection: close\r\n"
              "\r\n"
              "ab\r\ncd\r\n");
}

TEST(BuildHttpPostRequest, MultipleExtraHeaderLinesPassThroughVerbatim) {
    std::string request =
        buildHttpPostRequest("api.local", 9090, "/v1/chat", "hi", "text/plain",
                             "X-Api-Key: secret\r\nAccept: application/json\r\n");
    EXPECT_EQ(request,
              "POST /v1/chat HTTP/1.1\r\n"
              "Host: api.local:9090\r\n"
              "Content-Type: text/plain\r\n"
              "Content-Length: 2\r\n"
              "Connection: close\r\n"
              "X-Api-Key: secret\r\n"
              "Accept: application/json\r\n"
              "\r\n"
              "hi");
}

TEST(BuildHttpPostRequest, ExtraHeadersAreNotNormalized) {
    // The header doc says "raw lines": the function inserts the bytes as-is.
    // Omitting the trailing CRLF means the blank-line terminator becomes this
    // header's line ending and NO blank line separates headers from body.
    std::string request =
        buildHttpPostRequest("h", 80, "/", "B", "text/plain", "X-No-Crlf: oops");
    EXPECT_EQ(request,
              "POST / HTTP/1.1\r\n"
              "Host: h:80\r\n"
              "Content-Type: text/plain\r\n"
              "Content-Length: 1\r\n"
              "Connection: close\r\n"
              "X-No-Crlf: oops\r\n"
              "B");
}

TEST(BuildHttpPostRequest, HostHeaderIsHostColonPort) {
    std::string request =
        buildHttpPostRequest("192.168.0.42", 1, "/", "", "text/plain", "");
    EXPECT_NE(request.find("Host: 192.168.0.42:1\r\n"), std::string::npos);
}

TEST(BuildHttpPostRequest, LargePortNumberRenderedPlain) {
    std::string request =
        buildHttpPostRequest("example.com", 65535, "/", "", "text/plain", "");
    EXPECT_NE(request.find("Host: example.com:65535\r\n"), std::string::npos);
}

TEST(BuildHttpPostRequest, NumbersAreLocaleIndependent) {
    // A global locale with digit grouping must not leak into the wire format
    // ("Content-Length: 12,345" would be malformed HTTP).
    GlobalLocaleGuard guard(std::locale(std::locale::classic(), new GroupedNumpunct));
    std::string body(12345, 'y');
    std::string request =
        buildHttpPostRequest("grouped.example", 65535, "/", body, "text/plain", "");
    EXPECT_NE(request.find("Content-Length: 12345\r\n"), std::string::npos);
    EXPECT_NE(request.find("Host: grouped.example:65535\r\n"), std::string::npos);
    EXPECT_EQ(request.find(','), std::string::npos);
}

TEST(BuildHttpPostRequest, BodyWithEmbeddedNulByteIsCountedAndEmitted) {
    std::string body("a\0b", 3);
    std::string request = buildHttpPostRequest("h", 80, "/", body, "text/plain", "");
    std::string expected =
        "POST / HTTP/1.1\r\n"
        "Host: h:80\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 3\r\n"
        "Connection: close\r\n"
        "\r\n";
    expected += body;
    EXPECT_EQ(request, expected); // operator== compares all 3 body bytes incl. NUL
}

// --- parseHttpResponse: status line -------------------------------------------

TEST(ParseHttpResponse, ExtractsStatus200AndPlainBody) {
    HttpResponse resp = mustParse("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi");
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(resp.body, "hi");
}

TEST(ParseHttpResponse, ExtractsStatus404) {
    HttpResponse resp = mustParse("HTTP/1.1 404 Not Found\r\n\r\nmissing");
    EXPECT_EQ(resp.status, 404);
    EXPECT_EQ(resp.body, "missing");
}

TEST(ParseHttpResponse, ExtractsStatus500) {
    HttpResponse resp = mustParse("HTTP/1.0 500 Internal Server Error\r\n\r\n");
    EXPECT_EQ(resp.status, 500);
    EXPECT_EQ(resp.body, "");
}

TEST(ParseHttpResponse, ReasonPhraseIsIgnored) {
    HttpResponse resp =
        mustParse("HTTP/1.1 200 Anything 999 Goes Here\r\n\r\nbody");
    EXPECT_EQ(resp.status, 200); // atoi stops at the space before the reason
    EXPECT_EQ(resp.body, "body");
}

TEST(ParseHttpResponse, FourDigitStatusParsedVerbatim) {
    // No range validation: the digits after the first space are taken as-is.
    HttpResponse resp = mustParse("HTTP/1.1 1000 Weird\r\n\r\n");
    EXPECT_EQ(resp.status, 1000);
}

TEST(ParseHttpResponse, NonNumericStatusParsesAsZeroAndStillSucceeds) {
    // Documented quirk: atoi("OK") == 0 and the parser does not reject it,
    // so a garbage status code yields success with status == 0.
    HttpResponse resp;
    std::string err;
    EXPECT_TRUE(parseHttpResponse("HTTP/1.1 OK\r\n\r\nbody", &resp, &err));
    EXPECT_EQ(resp.status, 0);
    EXPECT_EQ(resp.body, "body");
}

// --- parseHttpResponse: malformed input ----------------------------------------

TEST(ParseHttpResponse, EmptyInputIsMalformed) {
    HttpResponse resp;
    std::string err;
    EXPECT_FALSE(parseHttpResponse("", &resp, &err));
    EXPECT_EQ(err, "malformed response");
}

TEST(ParseHttpResponse, InputWithoutCrlfIsMalformed) {
    HttpResponse resp;
    std::string err;
    EXPECT_FALSE(parseHttpResponse("HTTP/1.1 200 OK", &resp, &err));
    EXPECT_EQ(err, "malformed response");
    // Bare-LF line endings are not accepted either: the parser requires CRLF.
    EXPECT_FALSE(parseHttpResponse("HTTP/1.1 200 OK\n\nbody", &resp, &err));
    EXPECT_EQ(err, "malformed response");
}

TEST(ParseHttpResponse, InputNotStartingWithHttpIsMalformed) {
    HttpResponse resp;
    std::string err;
    EXPECT_FALSE(parseHttpResponse("FTP/1.1 200 OK\r\n\r\nbody", &resp, &err));
    EXPECT_EQ(err, "malformed response");
    // The prefix check is case-sensitive.
    EXPECT_FALSE(parseHttpResponse("http/1.1 200 ok\r\n\r\nbody", &resp, &err));
    // First line shorter than "HTTP/" cannot match the prefix.
    EXPECT_FALSE(parseHttpResponse("HT\r\n\r\n", &resp, &err));
}

TEST(ParseHttpResponse, StatusLineWithoutAnySpaceIsMalformed) {
    HttpResponse resp;
    std::string err;
    EXPECT_FALSE(parseHttpResponse("HTTP/1.1\r\n\r\n", &resp, &err));
    EXPECT_EQ(err, "malformed status line");
}

TEST(ParseHttpResponse, SpaceOnlyAfterStatusLineIsStillMalformed) {
    // The first space in the whole input lies in a header, past the status
    // line end, which the parser rejects.
    HttpResponse resp;
    std::string err;
    EXPECT_FALSE(parseHttpResponse("HTTP/1.1\r\nServer: x\r\n\r\nbody", &resp, &err));
    EXPECT_EQ(err, "malformed status line");
}

// --- parseHttpResponse: body extraction ----------------------------------------

TEST(ParseHttpResponse, MissingHeaderBodySeparatorYieldsEmptyBody) {
    // Headers terminated by a single CRLF, never a blank line: parse still
    // succeeds, body is empty.
    HttpResponse resp = mustParse("HTTP/1.1 204 No Content\r\nServer: t\r\n");
    EXPECT_EQ(resp.status, 204);
    EXPECT_EQ(resp.body, "");
}

TEST(ParseHttpResponse, NonChunkedBodyKeepsEmbeddedCrlfAndBlankLines) {
    // Everything after the FIRST \r\n\r\n is the body, byte-for-byte —
    // including further \r\n\r\n sequences.
    HttpResponse resp = mustParse(
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nline1\r\nline2\r\n\r\ntail");
    EXPECT_EQ(resp.body, "line1\r\nline2\r\n\r\ntail");
}

TEST(ParseHttpResponse, NullOutputPointersAreTolerated) {
    // out may be null on success and error may be null on failure.
    EXPECT_TRUE(parseHttpResponse("HTTP/1.1 200 OK\r\n\r\nbody", nullptr, nullptr));
    EXPECT_FALSE(parseHttpResponse("", nullptr, nullptr));
}

// --- parseHttpResponse: chunked transfer-encoding -------------------------------

TEST(ParseHttpResponse, ChunkedSingleChunkIsDecoded) {
    HttpResponse resp = mustParse(chunkedResponse("5\r\nhello\r\n0\r\n\r\n"));
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(resp.body, "hello");
}

TEST(ParseHttpResponse, ChunkedMultipleChunksAreConcatenated) {
    // Chunk data is length-prefixed, so the 0xE chunk may contain \r\n\r\n.
    HttpResponse resp = mustParse(
        chunkedResponse("4\r\nWiki\r\n5\r\npedia\r\nE\r\n in\r\n\r\nchunks.\r\n0\r\n\r\n"));
    EXPECT_EQ(resp.body, "Wikipedia in\r\n\r\nchunks.");
}

TEST(ParseHttpResponse, ChunkedHexSizesUppercaseAndLowercase) {
    HttpResponse resp = mustParse(chunkedResponse(
        "A\r\n0123456789\r\n1a\r\nabcdefghijklmnopqrstuvwxyz\r\n0\r\n\r\n"));
    EXPECT_EQ(resp.body, "0123456789abcdefghijklmnopqrstuvwxyz");
}

TEST(ParseHttpResponse, ChunkedZeroChunkEndsStreamIgnoringTrailers) {
    // Everything after the terminating 0-chunk (trailers, stray bytes) is
    // treated as end-of-stream and discarded.
    HttpResponse resp = mustParse(
        chunkedResponse("5\r\nhello\r\n0\r\nX-Trailer: v\r\n\r\nLEFTOVER"));
    EXPECT_EQ(resp.body, "hello");
}

TEST(ParseHttpResponse, ChunkedTruncatedOnlyChunkYieldsEmptyBody) {
    // Declared length 5 but only 3 data bytes present: the whole chunk is
    // dropped, parse still succeeds.
    HttpResponse resp = mustParse(chunkedResponse("5\r\nhel"));
    EXPECT_EQ(resp.body, "");
}

TEST(ParseHttpResponse, ChunkedTruncatedSecondChunkKeepsDecodedPrefix) {
    // 0xA == 10 declared, only 5 bytes follow: decoding stops, earlier chunks
    // are kept.
    HttpResponse resp = mustParse(chunkedResponse("3\r\nabc\r\nA\r\nshort"));
    EXPECT_EQ(resp.body, "abc");
}

TEST(ParseHttpResponse, ChunkedGarbageChunkHeaderEndsStream) {
    // strtol(garbage, 16) == 0 and the loop breaks on chunkLen <= 0, so a
    // non-hex chunk header is treated exactly like the terminating 0-chunk.
    HttpResponse first = mustParse(chunkedResponse("zz\r\nignored\r\n0\r\n\r\n"));
    EXPECT_EQ(first.body, "");
    HttpResponse mid = mustParse(chunkedResponse("3\r\nabc\r\nQQ\r\nmore\r\n0\r\n\r\n"));
    EXPECT_EQ(mid.body, "abc"); // everything after the bad header is dropped
}

TEST(ParseHttpResponse, ChunkedNegativeChunkSizeEndsStream) {
    // strtol accepts a sign even in base 16; -5 <= 0 ends decoding.
    HttpResponse resp = mustParse(chunkedResponse("-5\r\nhello\r\n0\r\n\r\n"));
    EXPECT_EQ(resp.body, "");
}

TEST(ParseHttpResponse, LowercaseTransferEncodingHeaderIsRecognized) {
    HttpResponse resp = mustParse(
        "HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
    EXPECT_EQ(resp.body, "hello");
}

TEST(ParseHttpResponse, MixedCaseTransferEncodingIsNotRecognized) {
    // Only the two exact spellings are matched; any other capitalization
    // leaves the wire bytes (chunk framing included) in the body untouched.
    HttpResponse resp = mustParse(
        "HTTP/1.1 200 OK\r\nTransfer-encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
    EXPECT_EQ(resp.body, "5\r\nhello\r\n0\r\n\r\n");
}

TEST(ParseHttpResponse, ChunkedHeaderWithEmptyBodyYieldsEmptyBody) {
    HttpResponse resp =
        mustParse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(resp.body, "");
}

TEST(ParseHttpResponse, ChunkedBodyWithoutCrlfYieldsEmptyBody) {
    // No CRLF terminating the chunk-size line: decoding stops immediately.
    HttpResponse resp = mustParse(chunkedResponse("5hello"));
    EXPECT_EQ(resp.body, "");
}

TEST(ParseHttpResponse, ChunkedChunkExtensionsAfterSizeAreTolerated) {
    // strtol stops at ';' and the parser skips to the end of the size line,
    // so chunk extensions are ignored rather than fatal.
    HttpResponse resp = mustParse(chunkedResponse("5;name=val\r\nhello\r\n0\r\n\r\n"));
    EXPECT_EQ(resp.body, "hello");
}

TEST(ParseHttpResponse, ChunkedChunkDataMayContainCrlf) {
    // 7 data bytes including an embedded CRLF; the length prefix, not the
    // CRLF, delimits the chunk.
    HttpResponse resp = mustParse(chunkedResponse("7\r\nab\r\ncde\r\n0\r\n\r\n"));
    EXPECT_EQ(resp.body, "ab\r\ncde");
}

TEST(ParseHttpResponse, ChunkedSkipsTwoBytesAfterChunkDataWithoutValidatingCrlf) {
    // Documented quirk: after consuming chunk data the parser blindly skips
    // two bytes, never checking they are \r\n. "QQ" here is silently eaten
    // and decoding resyncs on the next size line.
    HttpResponse resp = mustParse(chunkedResponse("3\r\nabcQQ3\r\ndef\r\n0\r\n\r\n"));
    EXPECT_EQ(resp.body, "abcdef");
}

TEST(ParseHttpResponse, DoubleSpaceBeforeStatusCodeStillParses) {
    // atoi skips leading whitespace, so "HTTP/1.1  200" (two spaces) parses
    // the same as the single-space form.
    HttpResponse resp = mustParse("HTTP/1.1  200 OK\r\n\r\nbody");
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(resp.body, "body");
}

TEST(ParseHttpResponse, ChunkSizeWithLeadingWhitespaceIsAccepted) {
    // strtol skips leading whitespace before the hex digits, so a padded
    // chunk-size line still decodes.
    HttpResponse resp = mustParse(chunkedResponse("  5\r\nhello\r\n0\r\n\r\n"));
    EXPECT_EQ(resp.body, "hello");
}

TEST(ParseHttpResponse, ChunkedMissingTerminalZeroChunkKeepsDecodedData) {
    // Stream ends right after the last chunk's trailing CRLF with no 0-chunk:
    // the loop exits at end of input and the decoded data is kept.
    HttpResponse resp = mustParse(chunkedResponse("5\r\nhello\r\n"));
    EXPECT_EQ(resp.body, "hello");
}

TEST(ParseHttpResponse, ChunkedHugeDeclaredSizeStopsDecodingKeepsPrefix) {
    // 0xFFFFFFFF (or LONG_MAX after 32-bit overflow) exceeds the available
    // bytes either way, so decoding stops and earlier chunks are kept.
    HttpResponse resp =
        mustParse(chunkedResponse("3\r\nabc\r\nFFFFFFFF\r\nxyz\r\n0\r\n\r\n"));
    EXPECT_EQ(resp.body, "abc");
}

TEST(ParseHttpResponse, FailedParseLeavesOutputUntouched) {
    // On failure the parser returns before writing to *out.
    HttpResponse resp;
    resp.status = 123;
    resp.body = "keep";
    std::string err;
    EXPECT_FALSE(parseHttpResponse("not http at all", &resp, &err));
    EXPECT_EQ(resp.status, 123);
    EXPECT_EQ(resp.body, "keep");
}

TEST(ParseHttpResponse, TransferEncodingDetectionIsSubstringSearch) {
    // The header block is scanned with find(), not parsed per-header, so the
    // marker matching inside an unrelated header name still triggers chunk
    // decoding.
    HttpResponse resp = mustParse(
        "HTTP/1.1 200 OK\r\nX-Upstream-Transfer-Encoding: chunked\r\n\r\n"
        "5\r\nhello\r\n0\r\n\r\n");
    EXPECT_EQ(resp.body, "hello");
}
