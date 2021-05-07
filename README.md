# tty_uart  
linux tty uart application  

tty_uart为linux系统下串口通用应用程序 ，各类常用串口功能已封装成API接口函数供开发者使用 

### 功能： 

1. 串口常规参数设定（波特率/数据位/停止位/校验位） 
2. 串口硬件流控  
3. 接收数据实时回显  
4. 串口发送文件或接收串口数据保存至文件  
5. 串口基本读写  
6. MODEM信号读取与设定  
7. 支持获取串口相关计数（modem变化次数/串口帧错误/校验错误/溢出错误）  
8. 支持获取/设定串口serial_struct  
9. 支持同步等待MODEM输入信号变化  

### 用法：

​	使用gcc编译tty_test.c源文件，如：gcc tty_uart.c -o test，生成可执行文件后运行需要root权限操作串口。

### 运行命令选项：

- -D --device  tty device to use（指定操作的串口名，未指定则默认操作：/dev/ttyUSB0）
- -S --speed   uart speed（设定的串口波特率）
- -v --verbose  Verbose (show rx buffer)（是否实时显示接收的串口数据）
- -f --hardflow open hardware flowcontrol（是否打开硬件流控）

源程序串口设置默认为8N1，若需要设定为其他串口格式，可直接修改代码。

串口成功打开后，输入相应字符执行相应操作：

- s - 设置RTS和DTR有效
- z - 设置RTS和DTR无效
- g - 获取MODEM输入引脚状态（CTS、DSR、RING、DCD）
- h - 同步等待MODEM信号变化（相应操作会阻塞执行，直到信号变化或串口异常退出）
- b - 发送break信号
- w - 发送一个字符串
- r - 读取一次数据
- f - 选择通过串口发送文件或者接收串口数据保存至文件

### 举例：

​	sudo ./test -D /dev/ttyS1 -S 57600 -v（表示以57600波特率操作/dev/ttyS1串口，并实时显示接收的数据）
