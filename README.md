mine/
├── CMakeLists.txt
├── Main.cpp
├── Node.h
└── include/
    ├── config.h        (定义协议类型 PING/PONG)
    ├── icmp_header.h   (标准 ICMP 头，刚才写过)
    ├── ipv4_header.h   (标准 IP 头，刚才写过)
    ├── reader.h        (物理层接收)
    ├── writer.h        (物理层发送)
    ├── utils.h         (帧结构、CRC32声明)
    └── utils.cpp       (CRC32实现)
