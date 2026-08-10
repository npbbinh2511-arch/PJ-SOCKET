#include "hftp/control/crlf_framer.h"

#include <cassert>
#include <string>
#include <vector>

using hftp::control::CrlfFramer;

int main() {
    CrlfFramer complete;
    assert(complete.push("NOOP\r\n") == std::vector<std::string>{"NOOP"});

    CrlfFramer fragmented;
    assert(fragmented.push("US").empty());
    assert(fragmented.push("ER alice\r").empty());
    assert(fragmented.push("\n") == std::vector<std::string>{"USER alice"});

    CrlfFramer coalesced;
    assert(coalesced.push("NOOP\r\nPWD\r\nQUIT\r\n") ==
           (std::vector<std::string>{"NOOP", "PWD", "QUIT"}));

    CrlfFramer leftover;
    assert(leftover.push("PWD\r\nUSER al") == std::vector<std::string>{"PWD"});
    assert(leftover.push("ice\r\n") == std::vector<std::string>{"USER alice"});

    CrlfFramer empty_line;
    assert(empty_line.push("\r\n") == std::vector<std::string>{""});

    CrlfFramer exact_limit(4);
    assert(exact_limit.push("NOOP\r").empty());
    assert(!exact_limit.failed());
    assert(exact_limit.push("\n") == std::vector<std::string>{"NOOP"});

    CrlfFramer oversized_pending(4);
    assert(oversized_pending.push("ABCDE").empty());
    assert(oversized_pending.failed());
    assert(oversized_pending.push("\r\nNOOP\r\n").empty());
    assert(oversized_pending.failed());

    CrlfFramer oversized_complete(4);
    assert(oversized_complete.push("ABCDE\r\n").empty());
    assert(oversized_complete.failed());
}
