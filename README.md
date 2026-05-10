# Qmini
一个极致轻量的开黑语音软件
Qmini 语音开黑软件 v1.0
======================

D:\Qmini_Dist\
  ├── README.txt                                  (使用说明)
  ├── client\
  │   ├── Qmini_Tray.exe                          (托盘版 458KB)
  │   └── Qmini_Panel.exe                         (面板版 459KB)    
  └── server\
      ├── windows\
      │   └── signaling_server.exe                (Win版 163KB)
      └── linux\
          └── signaling_server_linux.c            (Linux版


=== 客户端 ===
client/Qmini_Tray.exe   - 系统托盘版（后台运行，右键菜单操作）
client/Qmini_Panel.exe  - 控制面板版（窗口界面操作）

客户端配置文件: %APPDATA%\Qmini\config.ini
首次运行自动生成，可手动编辑服务器地址。

=== 服务器 ===
Windows: server/windows/signaling_server.exe
  直接运行，监听端口 9088

Linux:   server/linux/signaling_server_linux.c
  编译: gcc -O2 signaling_server_linux.c -o signaling_server -lpthread
  运行: ./signaling_server
  端口: 默认 9088，可通过参数指定: ./signaling_server 9800

=== 使用流程 ===
1. 服务器端启动 signaling_server
2. 客户端启动 qmini，点击加入房间
3. 输入服务器地址（如 192.144.144.168:9088）
4. 同一房间内的用户自动 P2P 互通

=== 快捷键 ===
PTT（按键说话）: 鼠标侧键 (XButton1)
静音切换: Ctrl+F13
 
