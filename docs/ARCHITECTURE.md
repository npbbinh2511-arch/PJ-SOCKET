# Kiến trúc Hybrid FTP

## Tổng quan

Project dùng hai kênh độc lập:

- TCP control giữ kết nối lâu dài, truyền command/reply theo CRLF và quản lý session.
- UDP data được tạo theo từng transfer, truyền dữ liệu bằng Custom RDT Go-Back-N.

Mỗi client TCP được server phục vụ bởi một worker riêng. `Session` không dùng chung giữa các client. Một transfer chạy trong worker riêng để control channel vẫn xử lý được `ABOR` và shutdown.

## Chiều phụ thuộc

```text
apps
 ├─ server -> server -> control -> protocol + session
 │                                -> filesystem
 │                                -> transfer -> rdt -> network
 └─ client -> client CLI -> TCP client + data channel -> transfer + rdt

common/result và network/socket là hạ tầng dùng chung.
```

`CommandDispatcher` chỉ kiểm tra trạng thái và điều phối. Thuật toán filesystem nằm trong `Std_FileRepository`; chuyển đổi ASCII/Binary và hash nằm tại transfer/filesystem/integrity; state machine ACK/timeout/retransmission nằm trong RDT.

## Luồng transfer

1. Client đăng nhập, chọn `TYPE` và Active hoặc Passive.
2. Server cấp/ghi nhận UDP endpoint qua `PASV` hoặc `PORT`.
3. Command transfer trả `150` kèm `id=<transfer_id>` và, với download, `bytes=<size>`.
4. HELLO/probe gắn transfer ID xác lập UDP peer cho các chiều không biết trước source port.
5. RDT truyền DATA theo cửa sổ, cumulative ACK, timeout và retransmission; CRC32 bảo vệ header lẫn payload.
6. FIN/FIN-ACK kết thúc data channel; server trả `226` kèm byte count và SHA-256.
7. Session và socket lease được reset kể cả khi lỗi, hủy hoặc client ngắt kết nối.

Socket UDP đã bind được giữ bằng `shared_ptr` trong `TransferContext`. Lease này loại bỏ race giữa lúc control channel công bố cổng và lúc data worker bắt đầu nhận gói.

## Biên an toàn

- Packet wire serialize từng trường theo network byte order; không gửi trực tiếp layout của C++ struct.
- RDT bỏ ACK sai transfer ID, sai peer, stale/future; receiver khóa đúng peer sau packet hợp lệ.
- Filesystem chuẩn hóa virtual path và chặn absolute path, `..` escape và symlink/junction thoát FTP root.
- Ghi đè file dùng temporary file rồi replace; transfer lỗi không để lại file đích dở dang.
- Reply, command framing và logger chặn CR/LF injection.
- `PORT` chỉ chấp nhận địa chỉ trùng peer TCP để giảm FTP bounce.

## Khả năng kiểm thử

Các module có test độc lập cho packet, RDT, session, dispatcher, filesystem và transfer. Test `hybrid_end_to_end` khởi động server thật, đăng nhập bằng client thật và đối chiếu byte cho đủ bốn trường hợp Active/Passive × upload/download.

Các giới hạn có chủ đích được ghi trong `README.md`; chúng không phải phần implementation còn bỏ trống.
