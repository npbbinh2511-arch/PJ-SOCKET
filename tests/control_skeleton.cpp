// Test framework intentionally not selected yet. Convert each scenario below into
// the framework agreed by the team; every test follows Arrange / Act / Assert.

// TODO(B): TCP framing: fragmented command, split CRLF, coalesced commands, overflow.
// TODO(B): Command parser: casing, preserved argument, malformed and injected input.
// TODO(B): Reply formatter: exact code/text/CRLF and newline-injection rejection.
// TODO(B): Authentication: USER/PASS order, bad credential, repeated USER.
// TODO(B): Session isolation: two sessions never share cwd/auth/rename/transfer state.
// TODO(B): PORT parser: six values, boundaries, computed port, malformed endpoint.
// TODO(B): PASV state: successful allocation, allocation failure, reset cleanup.
// TODO(B): RNFR/RNTO: RNTO first, valid pair, stale source, per-session isolation.
// TODO(B): Transfer state: idle -> preparing -> running -> idle/failure.
// TODO(B): ABOR: idle ABOR, active cancellation, duplicate ABOR, one terminal reply.

