# Hybrid FTP

C++20 skeleton for a Hybrid FTP application. The control channel uses TCP and the
file-data channel uses a custom reliable protocol over UDP.

## Supported build environments

- Windows with WinSock and MinGW/MSVC.
- Linux, including GitHub Codespaces, with POSIX sockets.

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The platform socket API is selected at compile time. No socket framework or reliable
UDP library is used.
