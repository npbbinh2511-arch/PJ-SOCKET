# Hybrid FTP – C++20

Đồ án mô phỏng FTP lai với hai kênh độc lập:

- TCP dùng cho lệnh, reply và trạng thái session.
- UDP dùng cho dữ liệu, được bảo vệ bởi Custom RDT tự xây dựng.

Project không sử dụng FTP framework, RDT framework hoặc thư viện truyền file bên thứ ba.

## Thành viên và phạm vi

- Nguyễn Phi Bảo Bình – 25127020: TCP control channel, session, server đa luồng, Active/Passive và CLI.
- Nguyễn Mạnh Cường – 25127024: UDP RDT, packet, ACK, timeout, retransmission và sliding window.
- Đậu Cao Dũng – 25127301: filesystem sandbox, command thao tác file, ASCII/Binary, SHA-256, LIST/NLST, progress và logging.

## Chức năng hiện có

- Server xử lý đồng thời nhiều client, mỗi client có session độc lập.
- Active và Passive UDP mode.
- Custom RDT Go-Back-N: sequence number, ACK, CRC32, timeout, retransmission, duplicate suppression, cửa sổ trượt và FIN handshake.
- HELLO/probe gắn với transfer ID để thiết lập đúng UDP peer trong cả bốn luồng Active/Passive upload/download.
- Truyền ASCII theo NVT và truyền Binary không biến đổi dữ liệu.
- SHA-256 thuần C++; client tự động đối chiếu hash đầu-cuối trong reply
  `226` sau upload/download và báo mismatch là transfer lỗi.
- Sandbox filesystem chống `..`, đường dẫn native tuyệt đối và symlink thoát khỏi FTP root.
- Các lệnh: `USER`, `PASS`, `QUIT`, `NOOP`, `HELP`, `PWD`, `CWD`, `CDUP`, `MKD`, `RMD`, `LIST`, `NLST`, `STAT`, `SIZE`, `MDTM`, `TYPE`, `MODE`, `PORT`, `PASV`, `RETR`, `STOR`, `STOU`, `APPE`, `DELE`, `RNFR`, `RNTO`, `HASH`, `ABOR`.
- CLI tự thương lượng data channel, hiển thị tiến trình, nhận listing và đọc/ghi file cục bộ.
- Server log địa chỉ client, command, session ID và số session đang hoạt động.

## Build trên Windows

Yêu cầu CMake 3.20 trở lên và MinGW-w64 UCRT64:

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Nếu source từng được build ở ổ đĩa khác, hãy xóa đúng thư mục `build` rồi configure lại. Không tái sử dụng `CMakeCache.txt` cũ.

## Build trên Linux/GitHub Codespaces

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Project chỉ sử dụng C++20, standard library, native socket API và Threads của hệ điều hành.
Workflow `.github/workflows/ci.yml` tự build với warning nghiêm ngặt và chạy toàn bộ test trên Ubuntu khi push hoặc mở pull request.

## Chạy server và client

Server:

```text
hftp_server [tcp_port] [ftp_root] [username] [password] [passive_ipv4]
```

Ví dụ Windows:

```powershell
.\build\hftp_server.exe 2121 .\server_root student socket2026 192.168.1.10
```

Ví dụ Linux:

```bash
./build/hftp_server 2121 ./server_root student socket2026 192.168.1.10
```

Client:

```powershell
.\build\hftp_client.exe 127.0.0.1 2121
```

Trong CLI, chọn `PASSIVE` hoặc `ACTIVE [client_ipv4]`. Đây là helper cục bộ; client sẽ tự gửi `PASV`/`PORT` trước mỗi transfer. Luồng thử:

```text
USER student
PASS socket2026
PWD
MKD uploads
CWD uploads
TYPE I
PASSIVE
STOR "C:\du-lieu\sample.bin" sample.bin
HASH sample.bin
LIST
RETR sample.bin "C:\du-lieu\downloaded.bin"
ACTIVE 192.168.1.20
NLST
QUIT
```

Các cú pháp transfer phía CLI:

```text
STOR <local> [remote]
RETR <remote> [local]
APPE <local> [remote]
STOU <local>
LIST [remote_path]
NLST [remote_path]
```

Đường dẫn có khoảng trắng phải đặt trong dấu nháy kép. Khi hai máy cùng mạng LAN, `passive_ipv4` của server và địa chỉ truyền cho `ACTIVE` phải là IPv4 LAN mà máy còn lại truy cập được. Firewall cần cho phép TCP control port và UDP data port động. Sau mỗi transfer file, server trả số byte và SHA-256 trong reply `226`.

## Kiểm thử

Test bao phủ:

- Packet malformed, checksum header/payload, ACK và FIN.
- RDT loopback, retransmission, cancellation và sliding window.
- TCP framing, parser, authentication, nhiều client và shutdown.
- Sandbox, binary equality, ASCII normalization, append, unique store và SHA-256.
- `LIST`, `NLST`, `STAT`, `STOU`, `APPE`, `HELP` và session isolation.
- Logger chống log forging và progress tracker thread-safe.
- Data channel kiểm tra đủ ma trận Active/Passive × upload/download.
- End-to-end dùng server và client thật qua TCP control + UDP RDT, đối chiếu
  byte nhị phân và SHA-256 hai chiều.

## Giới hạn có chủ đích

- Trong lúc transfer đang chạy, CLI chấp nhận `ABOR`; nhấn Enter để chờ hoặc nhập trước command kế tiếp. Client giữ command kế tiếp đến khi nhận đủ reply kết thúc transfer, nên control channel không bị lệch reply.
- Dữ liệu của một transfer hiện được giữ trong bộ nhớ; phù hợp đồ án và file thử nghiệm, chưa tối ưu cho file rất lớn.
- Giao thức dùng IPv4 và không mã hóa control/data channel. Chỉ nên chạy trong môi trường học tập hoặc mạng tin cậy.
- `MODE S` và `STRU F` được hỗ trợ; các mode/structure FTP khác nằm ngoài phạm vi Hybrid FTP này.
