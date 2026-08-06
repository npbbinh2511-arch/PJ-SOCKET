# Skeleton Architecture

## Scope and assumptions

This skeleton targets C++20, CMake, Windows and WinSock. TCP carries CRLF-framed FTP
commands/replies; UDP carries file payload through a student-built reliability layer.
Basic begins with ASCII STOR/RETR and one fixed data mode. No production algorithm or
complete command implementation is included.

## Dependency direction

```text
apps/server -> server -> control -> protocol, session
                                  -> transfer -> rdt
                                              -> filesystem
apps/client -> client -> control framing -> network

All modules -> common result types
network/protocol/filesystem/rdt do not depend on control or apps
```

The command layer validates and orchestrates. It never contains filesystem algorithms
or RDT state machines. `TransferContext` is a snapshot passed across the boundary.

## Basic now

- RAII socket ownership and WinSock runtime contracts.
- CRLF framing, command/reply, authentication and per-client session contracts.
- Server/client lifecycle, data-mode and transfer-coordinator contracts.
- UDP packet wire and Stop-and-Wait contracts.
- Safe-path repository contract and control/data test scenarios.

## Advanced / Excellent TODO only

- Binary and complete directory/metadata/mutation operations.
- Both Active and Passive modes plus hardened multi-client lifecycle.
- Go-Back-N/Selective Repeat, sliding window, flow/congestion control.
- Fault injection, transfer statistics and end-to-end SHA-256 verification.

