# Task Assignment

| Area | Primary | Collaborators | Basic responsibility | Later extension |
|---|---|---|---|---|
| UDP packet wire format and RDT | A | B, C | Packet validation, Stop-and-Wait contract | GBN/sliding window, faults, statistics, SHA-256 |
| TCP sockets and control framing | B | A | Server/client lifecycle, CRLF, partial I/O | Multi-client hardening |
| Commands, authentication, sessions | B | C | USER/PASS, state, replies, dispatch | Full approved command set |
| Data connection and transfer orchestration | B | A, C | Fixed PORT or PASV, STOR/RETR coordination | Both modes, ABOR races |
| Filesystem and path policy | C | B | Safe paths, ASCII read/write | Binary, directories, metadata, locking |
| CLI, logging, documentation and evidence | C | A, B | Basic status and honest GenAI log | Progress, demo and report polish |

Shared interfaces (`common`, `TransferContext`, reply/result contracts) require team
review. The primary owner authors changes; collaborators review integration effects.

