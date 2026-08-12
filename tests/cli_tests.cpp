#include "hftp/client/cli.h"

#include <cassert>
#include <string>
#include <vector>

#include "hftp/common/result.h"

int main() {
    std::vector<std::string> tokens;

    assert(hftp::client::tokenize_cli_line(
        R"(STOR "C:\Users\user\Desktop\test file.txt" remote.txt)", tokens));
    assert(tokens.size() == 3);
    assert(tokens[0] == "STOR");
    assert(tokens[1] == R"(C:\Users\user\Desktop\test file.txt)");
    assert(tokens[2] == "remote.txt");

    assert(hftp::client::tokenize_cli_line(
        R"(RETR remote.bin "C:/download folder/local.bin")", tokens));
    assert(tokens.size() == 3);
    assert(tokens[1] == "remote.bin");
    assert(tokens[2] == "C:/download folder/local.bin");

    assert(hftp::client::tokenize_cli_line(R"(HELP "")", tokens));
    assert(tokens.size() == 2);
    assert(tokens[1].empty());

    const auto unmatched = hftp::client::tokenize_cli_line(
        R"(STOR "C:\broken path.txt remote.txt)", tokens);
    assert(!unmatched);
    assert(tokens.empty());

    const auto injected = hftp::client::tokenize_cli_line(
        "NOOP\r\nQUIT", tokens);
    assert(!injected);
    assert(tokens.empty());

    constexpr auto digest =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const std::string local = std::string("Transfer complete bytes=10 sha256=") +
                              digest;
    const std::vector<std::string> matching{
        std::string("226 Transfer complete bytes=10 sha256=") + digest};
    const auto verified =
        hftp::client::verify_transfer_sha256(local, matching);
    assert(verified);
    assert(verified.message.find("Integrity verified") != std::string::npos);

    const std::vector<std::string> uppercase{
        "226 Transfer complete bytes=10 sha256="
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"};
    assert(hftp::client::verify_transfer_sha256(local, uppercase));

    const std::vector<std::string> mismatched{
        "226 Transfer complete bytes=10 sha256="
        "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
    const auto mismatch =
        hftp::client::verify_transfer_sha256(local, mismatched);
    assert(!mismatch);
    assert(mismatch.error == hftp::common::Error::integrity_error);

    const std::vector<std::string> missing_digest{
        "226 Transfer complete bytes=10"};
    const auto missing =
        hftp::client::verify_transfer_sha256(local, missing_digest);
    assert(!missing);
    assert(missing.error == hftp::common::Error::protocol_error);
}
