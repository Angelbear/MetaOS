#include <telephony/ril.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <alloca.h>
#include "atchannel.h"
#include "at_tok.h"
#include "misc.h"
#include <getopt.h>
#include <sys/socket.h>
#include <cutils/sockets.h>
#include <termios.h>

#define LOG_TAG "RIL"
#include <utils/Log.h>


static pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_state_cond = PTHREAD_COND_INITIALIZER;

static int s_port = -1;
static const char * s_device_path = NULL;
static int          s_device_socket = 0;
static int s_closed = 0;

static void usage(char *s)
{
	fprintf(stderr, "usage: %s [-p <tcp port>] [-d /dev/tty_device]\n", s);
	exit(-1);
}

static void waitForClose()
{
	pthread_mutex_lock(&s_state_mutex);

	while (s_closed == 0) {
		pthread_cond_wait(&s_state_cond, &s_state_mutex);
	}

	pthread_mutex_unlock(&s_state_mutex);
}


static void onATReaderClosed() 
{
	 LOGI("AT channel closed\n"); 
	 at_close();
	 s_closed = 1;	 
}

static void onATTimeout()
{
	 LOGI("AT channel timeout; closing\n");
	 at_close();
	 s_closed = 1;
}

static void * mainLoop(void *param)
{
	int fd;
	int ret;
	LOGI("== entering mainLoop() -1");
	AT_DUMP("== ", "entering mainLoop()", -1 );
	at_set_on_reader_closed(onATReaderClosed);
	at_set_on_timeout(onATTimeout);

	for (;;) {
		fd = -1;
		while  (fd < 0) {
			if (s_port > 0) {
				fd = socket_loopback_client(s_port, SOCK_STREAM);
			} else if (s_device_socket) {
				if (!strcmp(s_device_path, "/dev/socket/qemud")) {
					/* Qemu-specific control socket */
					fd = socket_local_client( "qemud",
							ANDROID_SOCKET_NAMESPACE_RESERVED,
							SOCK_STREAM );
					if (fd >= 0 ) {
						char  answer[2];

						if ( write(fd, "gsm", 3) != 3 ||
								read(fd, answer, 2) != 2 ||
								memcmp(answer, "OK", 2) != 0)
						{
							close(fd);
							fd = -1;
						}
					}
				}
				else
					fd = socket_local_client( s_device_path,
							ANDROID_SOCKET_NAMESPACE_FILESYSTEM,
							SOCK_STREAM );
			} else if (s_device_path != NULL) {
				fd = open (s_device_path, O_RDWR);
				if ( fd >= 0 && !memcmp( s_device_path, "/dev/ttyS", 9 ) ) {
					/* disable echo on serial ports */
					struct termios  ios;
					tcgetattr( fd, &ios );
					ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
					tcsetattr( fd, TCSANOW, &ios );
					LOGI("open device success");
				}else {
					LOGI("open device failed");
				}
			}

			if (fd < 0) {
				perror ("opening AT interface. retrying...");
				sleep(10);
				/* never returns */
			}
		}

		s_closed = 0;
		ret = at_open(fd, NULL);

		if (ret < 0) {
			LOGE ("AT error %d on at_open\n", ret);
			return 0;
		}

		sleep(1);

		waitForClose();
		LOGI("Re-opening after close");
	}
}




int main (int argc, char **argv)
{
	int ret;
	int fd = -1;
	int opt;

	while ( -1 != (opt = getopt(argc, argv, "p:d:"))) {
		switch (opt) {
			case 'p':
				s_port = atoi(optarg);
				if (s_port == 0) {
					usage(argv[0]);
				}
				LOGI("Opening loopback port %d\n", s_port);
				break;

			case 'd':
				s_device_path = optarg;
				LOGI("Opening tty device %s\n", s_device_path);
				break;

			case 's':
				s_device_path   = optarg;
				s_device_socket = 1;
				LOGI("Opening socket %s\n", s_device_path);
				break;

			default:
				usage(argv[0]);
		}
	}

	if (s_port < 0 && s_device_path == NULL) {
		usage(argv[0]);
	}
	mainLoop(NULL);

	return 0;
}


