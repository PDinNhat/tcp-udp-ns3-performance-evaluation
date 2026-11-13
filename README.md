# Đánh giá hiệu năng TCP và UDP bằng mô phỏng NS-3

> **English summary:** performance comparison of TCP and UDP-based transport (including HTTP/2 vs HTTP/3-like traffic) in three network scenarios, simulated with **ns-3** and measured with **FlowMonitor**. Results are exported to CSV and visualized in a small static web dashboard.

Bài tập lớn học phần **Đánh giá hiệu năng mạng** – Trường Đại học Quy Nhơn. Dự án so sánh hiệu năng giao thức tầng vận chuyển TCP và các luồng dựa trên UDP trong ba kịch bản mạng, dùng **ns-3** để mô phỏng và **FlowMonitor** để thu số liệu (throughput, độ trễ, jitter, tỷ lệ mất gói).

## Cấu trúc thư mục

```
.
├── scratch/        # Mã nguồn C++ của 3 kịch bản (chép vào thư mục scratch của ns-3)
│   ├── case1.cc
│   ├── case2.cc
│   └── case3.cc
├── results/        # Kết quả mô phỏng xuất ra CSV
│   ├── case1.csv
│   ├── case2.csv
│   └── case3.csv
└── web/            # Dashboard HTML trực quan hóa kết quả
    ├── dashboard.html
    └── case1.html, case2.html, case3.html
```

## Ba kịch bản

### Case 1 – Video call (UDP) và streaming theo yêu cầu (TCP) khi mất gói 2%
- Topology: 2 client – 1 router – 1 server. Link client–router 100 Mbps / 2 ms; link router–server (nút thắt cổ chai) 10 Mbps / 20 ms, mất gói ngẫu nhiên 2%.
- Luồng UDP: mỗi 20 ms gửi một gói 1000 byte (mô phỏng video conference). Luồng TCP: OnOff 5 Mbps, gói 1400 byte (mô phỏng streaming).
- Thời gian mô phỏng: 10 giây.

| Dịch vụ | Giao thức | Mất gói (%) | Throughput (Kbps) | Trễ TB (ms) | Jitter TB (ms) |
|---|---|---|---|---|---|
| On-Demand Streaming | TCP | 2,62 | 850,96 | 22,85 | 0,428 |
| Video Conference | UDP | 1,50 | 405,03 | 22,93 | 0,054 |

### Case 2 – Truyền tệp trên mạng vệ tinh băng thông lớn, trễ cao (LFN)
- Topology: 3 node nối bằng 2 link 1 Gbps, trễ 300 ms mỗi link, mất gói ngẫu nhiên 0,5% (nhiễu, không phải tắc nghẽn).
- TCP NewReno với buffer 128 MB (FTP truyền liên tục) so với luồng UDP 600 Mbps (mô phỏng cách tăng tốc kiểu Aspera/UDT).
- Thời gian mô phỏng: 15 giây.

| Dịch vụ | Giao thức | Mất gói (%) | Throughput (Mbps) | Trễ TB (ms) |
|---|---|---|---|---|
| Traditional FTP | TCP NewReno | 5,73 | 0,406 | 600,14 |
| LFN Optimized Transfer | UDP (mô hình Aspera/UDT) | 1,27 | 603,905 | 600,02 |

### Case 3 – HTTP/2 (TCP) và HTTP/3 (QUIC/UDP)
- Topology: client – router – server; link truy cập 50 Mbps / 15 ms; link backbone 20 Mbps / 35 ms, mất gói 1,5%.
- TCP dùng CUBIC, tải 12 Mbps. Luồng UDP 12 Mbps đại diện cho QUIC, bắt đầu sớm hơn để phản ánh việc bắt tay nhanh hơn.
- Thời gian mô phỏng: 12 giây.

| Dịch vụ | Giao thức | Mất gói (%) | Throughput (Mbps) | Trễ TB (ms) | Jitter TB (ms) |
|---|---|---|---|---|---|
| HTTP/3 (QUIC/UDP) | UDP | 1,52 | 12,613 | 50,93 | 0,071 |
| HTTP/2 (TCP) | TCP | 2,61 | 1,161 | 51,64 | 0,579 |

## Cách chạy lại

Cần cài sẵn [ns-3](https://www.nsnam.org/) (có các module `flow-monitor` và `netanim`).

1. Chép `scratch/case1.cc`, `case2.cc`, `case3.cc` vào thư mục `scratch/` của ns-3.
2. Chạy từng kịch bản (tùy phiên bản ns-3):
   ```bash
   ./ns3 run scratch/case1
   # hoặc với bản cũ: ./waf --run scratch/case1
   ```
3. Kết quả CSV được ghi vào thư mục `results/` (và file XML cho NetAnim, FlowMonitor ở thư mục đang chạy).

Xem dashboard bằng một web server tĩnh:
```bash
cd web
python3 -m http.server 8000
# mở http://localhost:8000/dashboard.html
```
Dashboard dùng thư viện Chart.js tải qua CDN nên cần Internet.

## Công cụ
ns-3 (C++), FlowMonitor, NetAnim, Wireshark (phân tích gói tin), HTML/JavaScript và Chart.js.

## Hạn chế
- QUIC và Aspera/UDT được **mô hình hóa bằng luồng UDP** (OnOff) chứ không cài đặt đầy đủ giao thức, nên kết quả chỉ phản ánh xu hướng.
- Mỗi kịch bản chạy một lần với tham số cố định, chưa lặp nhiều lần để lấy trung bình.
