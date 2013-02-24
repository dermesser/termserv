# define _GNU_SOURCE
# include <stdlib.h>
# include <stdio.h>
# include <unistd.h>
# include <fcntl.h>
# include <termios.h>
# include <string.h>
# include <sys/ioctl.h>
# include <libsocket/libinetsocket.h>
# include <termios.h>
# include <sys/wait.h>
# include <signal.h>

struct termios original_state;

pid_t cpid;

void fork_child_shell(const char*, struct winsize);

// Util
void process_child(int sig)
{
	wait3(0,WNOHANG,0);
}

int main(void)
{
	int masterfd = 0, selret = 0, retbuf = 0, server = 0, accepted = 0;
	pid_t handle_pid = 0;
	ssize_t read_bytes = 0;
	char* slave_pts;
	char buffer[32];
	struct winsize ws;

	fd_set fds;

	signal(SIGCHLD,*(process_child));

	// Get TCP connection

	server = create_inet_server_socket("::","12345",LIBSOCKET_TCP,LIBSOCKET_IPv6,0);

	if ( server < 0 )
	{
		perror("create\n");
		exit(1);
	}

	while (	1 )
	{
		accepted = accept_inet_stream_socket(server,NULL,0,NULL,0,0,0);

		if ( accepted < 0 )
		{
			perror("accept\n");
			exit(1);
		}
		// Fork child for concurrent operation
		handle_pid = fork();

		if ( handle_pid > 0 ) // We're parent
		{
			// Child process holds a reference on the socket.
			// If it terminates, it should be the only reference to this socket
			// so the client may close.
			close(accepted);
			continue;
		} else if ( handle_pid == 0 ) // We're in the server child
		{
			read(accepted,&ws,sizeof(struct winsize));

			// Allocate new pseudo terminal
			masterfd = posix_openpt(O_RDWR|O_NOCTTY);

			if ( masterfd < 0 )
			{
				perror("posix_openpt\n");
				exit(1);
			}

			// Change permissions of terminal
			retbuf = grantpt(masterfd);

			if ( retbuf < 0 )
			{
				perror("grantpt\n");
				exit(1);
			}

			// Get name of terminal (/dev/pts/...)
			slave_pts = ptsname(masterfd);

			if ( slave_pts == NULL )
			{
				perror("ptsname\n");
				exit(1);
			}

			// Unlock terminal so the child can read/write from/to it
			retbuf = unlockpt(masterfd);

			if ( retbuf < 0 )
			{
				perror("unlockpt\n");
				exit(1);
			}

			// Fork child
			fork_child_shell(slave_pts,ws);

			// Set raw mode for current tty (noecho, no special character interpretation...)
			//set_tty_raw();

			read_bytes = 1;

			// Get input from user/output from terminal
			while ( read_bytes > 0 )
			{
				FD_ZERO(&fds);
				FD_SET(accepted,&fds);
				FD_SET(masterfd,&fds);

				selret = select(accepted > masterfd ? accepted+1 : masterfd+1,&fds,NULL,NULL,NULL);

				if ( selret == -1 )
					perror("select\n");

				if ( FD_ISSET(masterfd,&fds) ) // The child process wrote something to the terminal
				{
					memset(buffer,0,32);

					read_bytes = read(masterfd,buffer,31);
					write(accepted,buffer,read_bytes);
				}

				if ( read_bytes <= 0 )
					break;

				if ( FD_ISSET(accepted,&fds) ) // We may read data from the network connection
				{
					memset(buffer,0,32);

					read_bytes = read(accepted,buffer,31);
					write(masterfd,buffer,read_bytes);
				}
			}

			// Wait for the shell
			waitpid(cpid,0,0);

			close(accepted);
			close(masterfd);

			exit(0);
		}
	}

	return 0;
}

void fork_child_shell(const char* slavepts, struct winsize ws)
{
	int slavefd;
	char* shell = getenv("SHELL");

	// Gain window size
	//ioctl(0,TIOCGWINSZ,&ws);

	// Which shell do we execute?
	if ( shell == NULL )
		shell = "/bin/bash";

	cpid = fork();

	if ( cpid < 0 )
	{
		perror("fork\n");
		exit(1);
	}

	// Parent...
	if ( cpid != 0 )
		return;

	// Child...
	if ( cpid == 0 ) // We're in the child process, it's later the shell
	{
		// Get out of the server's session
		setsid();

		// Open the slave part of the pseudo terminal
		slavefd = open(slavepts,O_RDWR);

		// Set windows size obtained before
		ioctl(slavefd,TIOCSWINSZ,&ws);

		// Close original stdin, stdout, stderr
		close(0);
		close(1);
		close(2);

		// Duplicate the pseudo terminal to all file descriptors
		dup2(slavefd,0);
		dup2(slavefd,1);
		dup2(slavefd,2);

		// Finally, execute shell
		execl(shell,shell,(char*)NULL);

		perror("Couldn't exec\n");

		exit(1);
	}
}

