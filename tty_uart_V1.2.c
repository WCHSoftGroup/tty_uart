/* 
 * TTY testing utility (using tty driver) - Copyright (C) 2020 WCH Corporation.
 * Author: TECH39 <zhangj@wch.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * Cross-compile with cross-gcc -I /path/to/cross-kernel/include
 *
 * Version: V1.1
 * 
 * Update Log:
 * V1.0 - initial version
 * V1.1 - add hardflow control
 *		- add sendbreak
 *		- add uart to file function
 *		- VTIME and VMIN changed
 * V1.2 - add custom baud rates supports
 *
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
static int savefile = 0;
static FILE *fp;

static const struct option lopts[] = {
	{ "device", required_argument, 0, 'D' },
	{ "speed", optional_argument, 0, 'S' },
	{ "verbose", optional_argument, 0, 'v' },
	{ "hardflow", required_argument, 0, 'f' },
	{ "savefile", required_argument, 0, 's' },
	{ NULL, 0, 0, 0 },
};

static void print_usage(const char *prog)
{
	printf("Usage: %s [-DSvfs]\n", prog);
	puts("  -D --device    tty device to use\n"
		 "  -S --speed     uart speed\n"
		 "  -v --verbose   Verbose (show rx buffer)\n"
		 "  -f --hardflow  open hardware flowcontrol\n"
		 "  -s --savefile  save rx data to file\n");
	exit(1);
}

static void parse_opts(int argc, char *argv[])
{
	int c;
	
	while (1) {
		c = getopt_long(argc, argv, "D:S::vfsh", lopts, NULL);
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
		case 's':
			savefile = 1;
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
int libtty_setcustombaudrate(int fd, int baudrate)
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
int libtty_setopt(int fd, int speed, int databits, int stopbits, char parity, char hardflow)
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
 
	newtio.c_cc[VTIME] = 20;	/* Time-out value (tenths of a second) [!ICANON]. */
	newtio.c_cc[VMIN] = 128;	/* Minimum number of bytes read at once [!ICANON]. */
	
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
int libtty_open(const char *devname)
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
int libtty_close(int fd)
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
int libtty_tiocmset(int fd, char bDTR, char bRTS)
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
 *
 * The function return 0 if success, others if fail.
 */
int libtty_tiocmget(int fd)
{
	unsigned long modembits = 0;
	int ret;

	ret = ioctl(fd, TIOCMGET, &modembits);
	if (modembits & TIOCM_DSR)
		printf("DSR Active!\n");
	if (modembits & TIOCM_CTS)
		printf("CTS Active!\n");
	if (modembits & TIOCM_CD)
		printf("DCD Active!\n");
	if (modembits & TIOCM_RI)
		printf("RI Active!\n");
	
	if (ret)
		return ret;
	else
		return modembits;
}

/**
 * libtty_write - write 100 bytes at one time
 * @fd: file descriptor of tty device
 *
 */
void libtty_write(int fd)
{
	int nwrite;
	char buf[256];
	int i;
	
	for (i = 0; i < 256; i++)
		buf[i] = i;
	
	nwrite = write(fd, buf, sizeof(buf));
	printf("wrote %d bytes already.\n", nwrite);
}

/**
 * libtty_read - reading data cyclically
 * @fd: file descriptor of tty device
 *
 */
void libtty_read(int fd)
{
	int nwrite, nread;
	char buf[4096];
	int i;
	int total = 0;
	
	if (savefile) {
		fp = fopen("./fileoutput", "w+");
		if (fp == NULL) {
			printf("create file failed.\n");
			return;
		}
	}
	
	while (1) {
		nread = read(fd, buf, sizeof(buf));
		if (nread >= 0) {
			total += nread;
			printf("read total %d bytes, %d this time.\n", total, nread);
		} else
			printf("read error!\n");

		if (verbose) {
			printf("*************************\n");
			for (i = 0; i < nread; i++)
				printf(" 0x%.2x", (uint8_t)buf[i]);
			printf("\n*************************\n");		
		}
		if (savefile) {
			fwrite(buf, sizeof(char), nread, fp);
		}
	}
}

void sig_handler(int signo)
{
    printf("capture sign no:%d\n",signo);
	if (savefile) {
		fflush(fp);
		fsync(fileno(fp));
		fclose(fp);
	}
	exit(0);
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
int libtty_sendbreak(int fd)
{
	return tcsendbreak(fd, 0);
}

int main(int argc, char *argv[])
{
	int fd;
	int ret;
	char c;

	parse_opts(argc, argv);
	
	signal(SIGINT, sig_handler); 
	
	fd = libtty_open(device);
	if (fd < 0) {
		printf("libtty_open error.\n");
		exit(0);
	}
	
	ret = libtty_setopt(fd, speed, 8, 1, 'n', hardflow);
	if (ret != 0) {
		printf("libtty_setopt error.\n");
		exit(0);
	}

	while (1) {
		if (c != '\n')
			printf("press s to set modem, z to clear modem, g to get modem, "
					"b to send break, w to write, r to read, "
					"G to test gpio, q for quit.\n");
		scanf("%c", &c);
		if (c == 'q')
			break;
		switch (c) {
		case 's':
			libtty_tiocmset(fd, 1, 1);
			break;
		case 'z':
			libtty_tiocmset(fd, 0, 0);
			break;
		case 'g':
			libtty_tiocmget(fd);
			break;
		case 'b':
			libtty_sendbreak(fd);
			break;
		case 'w':
			libtty_write(fd);
			break;
		case 'r':
			libtty_read(fd);
			break;
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

