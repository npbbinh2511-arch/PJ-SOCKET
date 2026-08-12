# Task Assignment

| Area | Primary | Collaborators | Trạng thái hiện tại |
|---|---|---|---|
| UDP packet wire format and RDT | A | B, C | Hoàn tất validation, CRC32, Go-Back-N, timeout/retry, fault injection, peer validation và FIN handshake |
| TCP sockets and control framing | B | A | Hoàn tất lifecycle đa client, CRLF/partial I/O, shutdown và Linux SIGPIPE handling |
| Commands, authentication, sessions | B | C | Hoàn tất authentication, session isolation, dispatcher và bộ command theo đặc tả |
| Data connection and transfer orchestration | B | A, C | Hoàn tất Active/Passive hai chiều, HELLO peer discovery, socket lease và ABOR ordering |
| Filesystem and path policy | C | B | Hoàn tất sandbox, ASCII/Binary, directory/metadata/mutation, atomic replace và SHA-256 |
| CLI, logging, documentation and evidence | C | A, B | Hoàn tất CLI transfer/progress, logging, hướng dẫn chạy và test end-to-end |

Shared interfaces (`common`, `TransferContext`, reply/result contracts) require team
review. The primary owner authors changes; collaborators review integration effects.
