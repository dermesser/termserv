# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <libsocket/libinetsocket.h>
# include <sys/select.h>
# include <string.h>
# include <termios.h>
# include <sys/ioctl.h>

struct termios original_state;

void set_tty_raw(void);
void reset_tty(void);

int main(void)
{
	int clfd = 0;
	fd_set fds;
	ssize_t read_bytes = 1;
	char buffer[32];
	struct winsize ws;

	clfd = create_inet_stream_socket("::1","12345",LIBSOCKET_IPv6,0);

	// Get all characters from calling tty (client tty)
	set_tty_raw();

	ioctl(0,TIOCGWINSZ,&ws);

	write(clfd,&ws,sizeof(struct winsize));

	// Route data between user and server
	while ( read_bytes > 0 )
	{
		FD_ZERO(&fds);
		FD_SET(clfd,&fds);
		FD_SET(0,&fds);

		memset(buffer,0,32);

		select(clfd+1,&fds,NULL,NULL,NULL);

		if ( FD_ISSET(clfd,&fds) )
		{
			read_bytes = read(clfd,buffer,31);
			write(1,buffer,read_bytes);
		}

		if ( read_bytes <= 0 )
			break;

		if ( FD_ISSET(0,&fds) )
		{
			read_bytes = read(0,buffer,31);
			write(clfd,buffer,read_bytes);
		}
	}

	reset_tty();

	close(clfd);

	return 0;
}

// Black terminal magic...

void set_tty_raw(void)
{
	struct termios attrs;

	tcgetattr(0,&original_state);
	tcgetattr(0,&attrs);

	attrs.c_lflag &= ~(ICANON|ISIG|IEXTEN|ECHO);

	attrs.c_iflag &= ~(BRKINT|ICRNL|IGNBRK|IGNCR|INLCR|INPCK|ISTRIP|IXON|PARMRK);

	attrs.c_oflag &= ~OPOST;

	attrs.c_cc[VMIN] = 1;
	attrs.c_cc[VTIME] = 0;

	tcsetattr(0,TCSAFLUSH,&attrs);
}

void reset_tty(void)
{
	tcsetattr(0,TCSANOW,&original_state);
}
