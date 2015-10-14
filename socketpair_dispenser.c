#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <sys/socket.h>
#include <sys/un.h>


#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 104
#endif 

// http://www.thomasstover.com/uds.html

 int send_fd(int socket, int fd_to_send)
 {
  if(fd_to_send==-1) {
    return send(socket, "X", 1, 0);
  }
  struct msghdr socket_message;
  struct iovec io_vector[1];
  struct cmsghdr *control_message = NULL;
  char message_buffer[1];
  /* storage space needed for an ancillary element with a paylod of length is CMSG_SPACE(sizeof(length)) */
  char ancillary_element_buffer[CMSG_SPACE(sizeof(int))];
  int available_ancillary_element_buffer_space;

  /* at least one vector of one byte must be sent */
  message_buffer[0] = 'F';
  io_vector[0].iov_base = message_buffer;
  io_vector[0].iov_len = 1;

  /* initialize socket message */
  memset(&socket_message, 0, sizeof(struct msghdr));
  socket_message.msg_iov = io_vector;
  socket_message.msg_iovlen = 1;

  /* provide space for the ancillary data */
  available_ancillary_element_buffer_space = CMSG_SPACE(sizeof(int));
  memset(ancillary_element_buffer, 0, available_ancillary_element_buffer_space);
  socket_message.msg_control = ancillary_element_buffer;
  socket_message.msg_controllen = available_ancillary_element_buffer_space;

  /* initialize a single ancillary data element for fd passing */
  control_message = CMSG_FIRSTHDR(&socket_message);
  control_message->cmsg_level = SOL_SOCKET;
  control_message->cmsg_type = SCM_RIGHTS;
  control_message->cmsg_len = CMSG_LEN(sizeof(int));
  *((int *) CMSG_DATA(control_message)) = fd_to_send;

  return sendmsg(socket, &socket_message, 0);
 }


int main(int argc, char* argv[]) {
    struct sockaddr_storage ss;
    socklen_t len;
    
    struct sockaddr* s = (struct sockaddr*) &ss;
    struct sockaddr_un *sa = (struct sockaddr_un*) s;
    
    if (argc != 3 || !strcmp(argv[1], "--help")) {
        fprintf(stderr, "Usage: socketpair_dispenser buffer_size path_to_socket\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, " socketpair_dispenser 1000000 /tmp/somefile.sock&\n");
        fprintf(stderr, " LD_PRELOAD=libmapopentounixsocket.so MAPOPENTOUNIXSOCKET=/tmp/*.sock someprogram --output=/tmp/somefile.sock\n");
        fprintf(stderr, " LD_PRELOAD=libmapopentounixsocket.so MAPOPENTOUNIXSOCKET=/tmp/*.sock otherprogram /tmp/somefile.sock\n");
        fprintf(stderr, "Mind /proc/sys/net/core/wmem_max\n");
        fprintf(stderr, "socketpair_dispenser is oneshot. You need to restart it for each connection.\n");
        return 1;
    }
    
    const char* path = argv[2];
    
    len = sizeof(*sa);
    memset(sa, 0, len);
    sa->sun_family = AF_UNIX;
        
    snprintf(sa->sun_path, UNIX_PATH_MAX, "%s", path);
    
    int ret;
    int sv[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    if (ret == -1) { return 2; }
    
    { 
        int b = atoi(argv[1]);
        setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &b, sizeof(b));
        setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &b, sizeof(b));
    }
    
    int so = socket(AF_UNIX, SOCK_STREAM, 0);
        
    ret = bind(so, s, len);
    
    if (ret == -1 && errno == EADDRINUSE) {
        unlink (path);
        ret = bind(so, s, len);
    }
    
    if (ret==-1) { close (so); return 3; }
    
    
    ret = listen(so, 1);
    if (ret==-1) { close (so); return 4; }
    
    int so2 = accept(so, s, &len);
    if (so2==-1) { return 5; }
    
    send_fd(so2, sv[0]);
    close(sv[0]);
    close(so2);
    
    so2 = accept(so, s, &len);
    if (so2==-1) { return 6; }
    send_fd(so2, sv[1]);
    close(sv[1]);
    close(so2);
    close(so);
    
    return 0;
}
