/* 
 * TTY testing utility (using tty driver)
 *
 * Copyright (C) 2020 WCH Corporation.
 * Author: TECH39 <zhangj@wch.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * Cross-compile with cross-gcc -I /path/to/cross-kernel/include
 * 
 * Update Log:
 * V1.0 - initial version
 * V1.1 - added hardflow control
 *		- added sendbreak
 *		- added uart to file function
 *		- VTIME and VMIN changed
 * V1.2 - added custom baud rates supports
 * V1.3 - fixed get speed parameter in getopt_long
 *		- added file send operation
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>  
#include <errno.h>   
#include <string.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <signal.h>
#include <getopt.h>
#include <linux/serial.h>
#define termios asmtermios
#include <asm/termios.h>
#undef  termios
#include <termios.h>

extern int ioctl(int d, int request, ...);

static const char *device = "/dev/ttyUSB0";
static int speed = 9600;
static int hardflow = 0;
static int verbose = 0;
static FILE *fp;

static const struct option lopts[] = {
	{ "device", required_argument, 0, 'D' },
	{ "speed", optional_argument, 0, 'S' },
	{ "verbose", optional_argument, 0, 'v' },
	{ "hardflow", required_argument, 0, 'f' },
	{ NULL, 0, 0, 0 },
};

static void print_usage(const char *prog)
{
	printf("Usage: %s [-DSvf]\n", prog);
	puts("  -D --device    tty device to use\n"
		 "  -S --speed     uart speed\n"
		 "  -v --verbose   Verbose (show rx buffer)\n"
		 "  -f --hardflow  open hardware flowcontrol\n");
	exit(1);
}

static void parse_opts(int argc, char *argv[])
{
	int c;
	
	while (1) {
		c = getopt_long(argc, argv, "D:S:vfh", lopts, NULL);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'D':
			if (optarg != NULL)
				device = optarg;
			break;
		case 'S':
			if (optarg != NULL)
				speed = atoi(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'f':
			hardflow = 1;
			break;	
		case 'h':
		default:
			print_usage(argv[0]);
			break;
		}
	}
}

/**
 * libtty_setcustombaudrate - set baud rate of tty device
 * @fd: device handle
 * @speed: baud rate to set
 *
 * The function return 0 if success, or -1 if fail.
 */
static int libtty_setcustombaudrate(int fd, int baudrate)
{
	struct termios2 tio;

	if (ioctl(fd, TCGETS2, &tio)) {
		perror("TCGETS2");
		return -1;
	}

	tio.c_cflag &= ~CBAUD;
	tio.c_cflag |= BOTHER;
	tio.c_ispeed = baudrate;
	tio.c_ospeed = baudrate;

	if (ioctl(fd, TCSETS2, &tio)) {
		perror("TCSETS2");
		return -1;
	}

	if (ioctl(fd, TCGETS2, &tio)) {
		perror("TCGETS2");
		return -1;
	}

	return 0;
}

/**
 * libtty_setopt - config tty device
 * @fd: device handle
 * @speed: baud rate to set
 * @databits: data bits to set
 * @stopbits: stop bits to set
 * @parity: parity to set
 * @hardflow: hardflow to set
 *
 * The function return 0 if success, or -1 if fail.
 */
static int libtty_setopt(int fd, int speed, int databits, int stopbits, char parity, char hardflow)
{
	struct termios newtio;
	struct termios oldtio;
	int i;
	
	bzero(&newtio, sizeof(newtio));
	bzero(&oldtio, sizeof(oldtio));
	
	if (tcgetattr(fd, &oldtio) != 0) {
		perror("tcgetattr");    
		return -1; 
	}
	newtio.c_cflag |= CLOCAL | CREAD;
	newtio.c_cflag &= ~CSIZE;
 
	/* set data bits */
	switch (databits) {
	case 5:                
		newtio.c_cflag |= CS5;
		break;
	case 6:                
		newtio.c_cflag |= CS6;
		break;
	case 7:                
		newtio.c_cflag |= CS7;
		break;
	case 8:    
		newtio.c_cflag |= CS8;
		break;  
	default:   
		fprintf(stderr, "unsupported data size\n");
		return -1; 
	}
	
	/* set parity */
	switch (parity) {  
	case 'n':
	case 'N':
		newtio.c_cflag &= ~PARENB;    /* Clear parity enable */
		newtio.c_iflag &= ~INPCK;     /* Disable input parity check */
		break; 
	case 'o':  
	case 'O':    
		newtio.c_cflag |= (PARODD | PARENB); /* Odd parity instead of even */
		newtio.c_iflag |= INPCK;     /* Enable input parity check */
		break; 
	case 'e': 
	case 'E':  
		newtio.c_cflag |= PARENB;    /* Enable parity */   
		newtio.c_cflag &= ~PARODD;   /* Even parity instead of odd */  
		newtio.c_iflag |= INPCK;     /* Enable input parity check */
		break;
	default:  
		fprintf(stderr, "unsupported parity\n");
		return -1; 
	} 
	
	/* set stop bits */ 
	switch (stopbits) {  
	case 1:   
		newtio.c_cflag &= ~CSTOPB; 
		break;
	case 2:   
		newtio.c_cflag |= CSTOPB; 
		break;
	default:   
		perror("unsupported stop bits\n"); 
		return -1;
	}
 
	if (hardflow)
		newtio.c_cflag |= CRTSCTS;
	else
		newtio.c_cflag &= ~CRTSCTS;
 
	newtio.c_cc[VTIME] = 10;	/* Time-out value (tenths of a second) [!ICANON]. */
	newtio.c_cc[VMIN] = 0;	/* Minimum number of bytes read at once [!ICANON]. */
	
	tcflush(fd, TCIOFLUSH);  
	
	if (tcsetattr(fd, TCSANOW, &newtio) != 0) {
		perror("tcsetattr");
		return -1;
	}

	/* set tty speed */
	if (libtty_setcustombaudrate(fd, speed) != 0) {
		perror("setbaudrate");
		return -1;
	}

	return 0;
}
 
/**
 * libtty_open - open tty device
 * @devname: the device name to open
 *
 * In this demo device is opened blocked, you could modify it at will.
 */
static int libtty_open(const char *devname)
{
	int fd = open(devname, O_RDWR | O_NOCTTY | O_NDELAY); 
	int flags = 0;
	
	if (fd < 0) {                        
		perror("open device failed");
		return -1;            
	}
	
	flags = fcntl(fd, F_GETFL, 0);
	flags &= ~O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) {
		printf("fcntl failed.\n");
		return -1;
	}
		
	if (isatty(fd) == 0) {
		printf("not tty device.\n");
		return -1;
	}
	else
		printf("tty device test ok.\n");
	
	return fd;
}
 
/**
 * libtty_close - close tty device
 * @fd: the device handle
 *
 * The function return 0 if success, others if fail.
 */
static int libtty_close(int fd)
{
	return close(fd);
}
 
/**
 * libtty_tiocmset - modem set
 * @fd: file descriptor of tty device
 * @bDTR: 0 on inactive, other on DTR active
 * @bRTS: 0 on inactive, other on RTS active
 *
 * The function return 0 if success, others if fail.
 */
static int libtty_tiocmset(int fd, char bDTR, char bRTS)
{
	unsigned long controlbits = 0;
	
	if (bDTR)
		controlbits |= TIOCM_DTR;
	if (bRTS)
		controlbits |= TIOCM_RTS;
	
	return ioctl(fd, TIOCMSET, &controlbits);
}

/**
 * libtty_tiocmget - modem get
 * @fd: file descriptor of tty device
 * @modembits: pointer to modem status
 *
 * The function return 0 if success, others if fail.
 */
static int libtty_tiocmget(int fd, unsigned long *modembits)
{
	int ret;

	ret = ioctl(fd, TIOCMGET, modembits);
	if (ret == 0) {
		if (*modembits & TIOCM_DSR)
			printf("DSR Active!\n");
		if (*modembits & TIOCM_CTS)
			printf("CTS Active!\n");
		if (*modembits & TIOCM_CD)
			printf("DCD Active!\n");
		if (*modembits & TIOCM_RI)
			printf("RI Active!\n");
	}

	return ret;
}

/**
 * libtty_tiocmwait - wiat for modem signal to changed
 * @fd: file descriptor of tty device
 *
 * The function return 0 if success, others if fail.
 */
static int libtty_tiocmwait(int fd)
{
	unsigned long modembits = TIOCM_DSR | TIOCM_CTS | TIOCM_CD | TIOCM_RI;

	return ioctl(fd, TIOCMIWAIT, modembits);
}

/**
 * libtty_sendbreak - uart send break
 * @fd: file descriptor of tty device
 *
 * Description:
 *  tcsendbreak() transmits a continuous stream of zero-valued bits for a specific duration, if the  termi©\
 *	nal is using asynchronous serial data transmission.  If duration is zero, it transmits zero-valued bits
 *	for at least 0.25 seconds, and not more that 0.5 seconds.  If duration is not zero, it sends  zero-val©\
 *	ued bits for some implementation-defined length of time.
 *
 *  If  the terminal is not using asynchronous serial data transmission, tcsendbreak() returns without tak©\
 *	ing any action.
 */
static int libtty_sendbreak(int fd)
{
	return tcsendbreak(fd, 0);
}

/**
 * libtty_write - write data to uart
 * @fd: file descriptor of tty device
 *
 * The function return the number of bytes written if success, others if fail.
 */
static int libtty_write(int fd)
{
	int nwrite;
	char buf[4096];
	int i;
	
	memset(buf, 0x00, sizeof(buf));
	printf("please input string to send:\n");
	scanf("%s", buf);
	nwrite = write(fd, buf, strlen(buf));
	printf("wrote %d bytes already.\n", nwrite);

	return nwrite;
}

/**
 * libtty_read - read data from uart
 * @fd: file descriptor of tty device
 *
 * The function return the number of bytes read if success, others if fail.
 */
static int libtty_read(int fd)
{
	int nwrite, nread;
	char buf[4096];
	int i;
	
	nread = read(fd, buf, sizeof(buf));
	if (nread >= 0) {
		printf("read nread %d bytes.\n", nread);
	} else {
		printf("read error: %d\n", nread);
		return nread;
	}

	if (verbose) {
		printf("*************************\n");
		for (i = 0; i < nread; i++)
			printf(" 0x%.2x", (uint8_t)buf[i]);
		printf("\n*************************\n");		
	}

	return nread;
}

/**
 * libtty_file_send - send file from uart
 * @fd: file descriptor of tty device
 *
 * The function return 0 if success, or -1 if fail.
 */
static int libtty_file_send(int fd)
{
	int nwrite;
	char buf[4096];
	int total = 0;
	char filename[128];
	int ret;
	
	printf("please input file name to send:\n");
	scanf("%s", filename);

	fp = fopen(filename, "r+");
	if (fp == NULL) {
		printf("file open failed.\n");
		return -1;
	}
	
	while (1) {
		ret = fread(buf, 1, sizeof(buf), fp);
		if (ret <= 0) {
			return ret;
		}
		nwrite = write(fd, buf, ret);
		if (nwrite >= 0) {
			total += nwrite;
			printf("write total %d bytes, %d this time.\n", total, nwrite);
		} else {
			printf("write error: %d\n", nwrite);
			break;
		}
	}
}

/**
 * libtty_file_read - receive uart data and save to file
 * @fd: file descriptor of tty device
 *
 * The function will loop unless read uart fail or write file fail.
 */
static int libtty_file_read(int fd)
{
	int ret;
	int nread;
	char buf[4096];
	int total = 0;
	char filename[128];
	
	printf("please input file name to save:\n");
	scanf("%s", filename);

	fp = fopen(filename, "w+");
	if (fp == NULL) {
		printf("create file failed.\n");
		return -1;
	}
	
	while (1) {
		nread = read(fd, buf, sizeof(buf));
		if (nread >= 0) {
			total += nread;
			printf("read total %d bytes, %d this time.\n", total, nread);
		} else {
			printf("read error: %d\n", nread);
			return nread;
		}
		ret = fwrite(buf, 1, nread, fp);
		if (ret != nread) {
			printf("write file error: %d\n", ret);
			return ret;
		}
	}
}

static int file_operation(int fd)
{
	int ret;
	char c;

	printf("press w to send file from uart, press r to receive uart data and save to file.\n");
	scanf(" %c", &c);
	if (c == 'w') {
		ret = libtty_file_send(fd);
		if (ret) {
			printf("libtty_file_send error: %d\n", ret);
			goto exit;
		}
		printf("file has been sent over.\n");
	} else if (c == 'r') {
		ret = libtty_file_read(fd);
		if (ret) {
			printf("libtty_file_read error: %d\n", ret);
			goto exit;
		}
	} else {
		printf("bad choice.\n");
	}
exit:
	return ret;
}

static void sig_handler(int signo)
{
    printf("capture sign no:%d\n",signo);
	if (fp != NULL) {
		fflush(fp);
		fsync(fileno(fp));
		fclose(fp);
	}
	exit(0);
}

int main(int argc, char *argv[])
{
	int fd;
	int ret;
	char c;
	unsigned long modemstatus;

	parse_opts(argc, argv);
	
	signal(SIGINT, sig_handler); 
	
	fd = libtty_open(device);
	if (fd < 0) {
		printf("libtty_open: %s error.\n", device);
		exit(0);
	}
	
	ret = libtty_setopt(fd, speed, 8, 1, 'n', hardflow);
	if (ret != 0) {
		printf("libtty_setopt error.\n");
		exit(0);
	}

	while (1) {
		if (c != '\n')
			printf("press s to set rts and dtr, z to clear rts and dtr, g to get modem status(cts/dsr/ring/dcd), "
					"h to wait for modem to be change, b to send break, w to send a string, r to read data once, "
					"f to send file or save received data to file, q to quit this app.\n");
		scanf("%c", &c);
		if (c == 'q')
			break;
		switch (c) {
		case 's':
			ret = libtty_tiocmset(fd, 1, 1);
			if (ret)
				printf("libtty_tiocmset error: %d\n", ret);
			break;
		case 'z':
			ret = libtty_tiocmset(fd, 0, 0);
			if (ret)
				printf("libtty_tiocmset error: %d\n", ret);
			break;
		case 'g':
			ret = libtty_tiocmget(fd, &modemstatus);
			if (ret)
				printf("libtty_tiocmget error: %d\n", ret);
			break;
		case 'h':
			ret = libtty_tiocmwait(fd);
			if (ret)
				printf("libtty_tiocmwait error: %d\n", ret);
			break;
		case 'b':
			ret = libtty_sendbreak(fd);
			if (ret)
				printf("libtty_sendbreak error: %d\n", ret);
			break;
		case 'w':
			ret = libtty_write(fd);
			if (ret <= 0)
				printf("libtty_write error: %d\n", ret);
			break;
		case 'r':
			ret = libtty_read(fd);
			if (ret < 0)
				printf("libtty_read error: %d\n", ret);
			break;
		case 'f':
			ret = file_operation(fd);
			if (ret)
				printf("file read/write error: %d\n", ret);
		default:
			break;
		}
	}
 	
	ret = libtty_close(fd);
	if (ret != 0) {
		printf("libtty_close error.\n");
		exit(0);
	}
}
