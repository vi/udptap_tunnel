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


// Created by Vitaly "_Vi" Shukela; 2015; License=MIT

/* I want only O_CREAT|O_WRONLY|O_TRUNC, not queer open's or creat's signatures */
//#include <fcntl.h>
#define _FCNTL_H
#include <bits/fcntl.h>


static int absolutize_path(char *outpath, int outpath_s, const char* pathname, int dirfd) {
    if(pathname[0]!='/') {
        int l;
        // relative path
        if (dirfd == AT_FDCWD) {
            char* ret = getcwd(outpath, outpath_s);
            if (!ret) {
                return -1;
            }
            l = strlen(outpath);
            if (l>=outpath_s) {
                errno=ERANGE;
                return -1;
            }
        } else {
            char proce[128];
            sprintf(proce, "/proc/self/fd/%d", dirfd);
            
            ssize_t ret = readlink(proce, outpath, outpath_s);
            
            if( ret==-1) {
                /* Let's just assume dirfd is for "/" (as in /bin/rm) */
                fprintf(stderr, "Warning: can't readlink %s, continuing\n", proce);
                l=0;
            } else {
                l=ret;
            }

        }
        outpath[l]='/';
        strncpy(outpath+l+1, pathname, outpath_s-l-1);
    } else {
        // absolute path
        snprintf(outpath, outpath_s, "%s", pathname);
    }
    return 0;
}


static int remote_openat(int dirfd, const char *pathname, int flags, mode_t mode);

static int remote_open(const char *pathname, int flags, mode_t mode) {
    return remote_openat(AT_FDCWD, pathname, flags, mode);
}

static int remote_open64(const char *pathname, int flags, mode_t mode) {
    return remote_open(pathname, flags, mode); }
static int remote_openat64(int dirfd, const char *pathname, int flags, mode_t mode) {
    return remote_openat(dirfd, pathname, flags, mode); }
static int remote_creat(const char *pathname, mode_t mode) { 
    return remote_open(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode); }
static int remote_creat64(const char *pathname, mode_t mode) { 
    return remote_open(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode); }


/* Taken from musl-0.9.7 */
static int __fmodeflags(const char *mode)
{
        int flags;
        if (strchr(mode, '+')) flags = O_RDWR;
        else if (*mode == 'r') flags = O_RDONLY;
        else flags = O_WRONLY;
        if (strchr(mode, 'x')) flags |= O_EXCL;
        if (strchr(mode, 'e')) flags |= O_CLOEXEC;
        if (*mode != 'r') flags |= O_CREAT;
        if (*mode == 'w') flags |= O_TRUNC;
        if (*mode == 'a') flags |= O_APPEND;
        return flags;
}



static FILE* remote_fopen(const char *path, const char *mode) {
    int flags = __fmodeflags(mode);
    int ret = remote_open(path, flags|O_LARGEFILE, 0666);
    if (ret==-1) return NULL;
    return fdopen(ret, mode);
}

static FILE* remote_fopen64(const char *path, const char *mode) {
    return remote_fopen(path, mode);
}


#define OVERIDE_TEMPLATE_I(name, rettype, succcheck, signature, sigargs) \
    /* static rettype (*orig_##name) signature = NULL; */ \
    rettype name signature { \
        return remote_##name sigargs; \
    }

#define OVERIDE_TEMPLATE(name, signature, sigargs) \
    OVERIDE_TEMPLATE_I(name, int, ret!=-1, signature, sigargs)


OVERIDE_TEMPLATE(open, (const char *pathname, int flags, mode_t mode), (pathname, flags, mode))
OVERIDE_TEMPLATE(open64, (const char *pathname, int flags, mode_t mode), (pathname, flags, mode))
OVERIDE_TEMPLATE(openat, (int dirfd, const char *pathname, int flags, mode_t mode), (dirfd, pathname, flags, mode))
OVERIDE_TEMPLATE(openat64, (int dirfd, const char *pathname, int flags, mode_t mode), (dirfd, pathname, flags, mode))
OVERIDE_TEMPLATE(creat, (const char *pathname,  mode_t mode), (pathname, mode))
OVERIDE_TEMPLATE(creat64, (const char *pathname, mode_t mode), (pathname, mode))

OVERIDE_TEMPLATE_I(fopen, FILE*, ret != NULL, (const char *path, const char *mode), (path, mode))
OVERIDE_TEMPLATE_I(fopen64, FILE*, ret != NULL, (const char *path, const char *mode), (path, mode))


#define MAXPATH 4096

static int is_initialized = 0;
static const char* filesmask;
const char do_debug = 0;
const char do_absolutize = 1;


static void initialize() {
    const char* e;
    e = getenv("MAPOPENTOUNIXSOCKET");
    if(!e) {
        fprintf(stderr, "Usage: LD_PRELOAD=libfopen_override.so MAPOPENTOUNIXSOCKET=somefilepathglob program [argguments...]\n");
        fprintf(stderr, "See also help message of socketpair_dispenser program.\n");
        return;
    }
    filesmask = e;

    is_initialized = 1;
}

int recv_fd(int socket)
 {
  int sent_fd;
  struct msghdr socket_message;
  struct iovec io_vector[1];
  struct cmsghdr *control_message = NULL;
  char message_buffer[1];
  char ancillary_element_buffer[CMSG_SPACE(sizeof(int))];

  /* start clean */
  memset(&socket_message, 0, sizeof(struct msghdr));
  memset(ancillary_element_buffer, 0, CMSG_SPACE(sizeof(int)));

  /* setup a place to fill in message contents */
  io_vector[0].iov_base = message_buffer;
  io_vector[0].iov_len = 1;
  socket_message.msg_iov = io_vector;
  socket_message.msg_iovlen = 1;

  /* provide space for the ancillary data */
  socket_message.msg_control = ancillary_element_buffer;
  socket_message.msg_controllen = CMSG_SPACE(sizeof(int));

  if(recvmsg(socket, &socket_message, MSG_CMSG_CLOEXEC) < 0)
   return -1;

  if(message_buffer[0] != 'F')
  {
   /* this did not originate from the above function */
   return -1;
  }

  if((socket_message.msg_flags & MSG_CTRUNC) == MSG_CTRUNC)
  {
   /* we did not provide enough space for the ancillary element array */
   return -1;
  }

  /* iterate ancillary elements */
   for(control_message = CMSG_FIRSTHDR(&socket_message);
       control_message != NULL;
       control_message = CMSG_NXTHDR(&socket_message, control_message))
  {
   if( (control_message->cmsg_level == SOL_SOCKET) &&
       (control_message->cmsg_type == SCM_RIGHTS) )
   {
    sent_fd = *((int *) CMSG_DATA(control_message));
    return sent_fd;
   }
  }

  return -1;
 }


int (*orig_openat) (int dirfd, const char *pathname, int flags, mode_t mode) = NULL;
static int remote_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (!is_initialized) initialize();
    if (!is_initialized) return -1;

    const char* pn = pathname;
    if (do_debug) fprintf(stderr, "mapopentounixsocket file=%s dirfd=%d flags=%x mode=%o orinsym=%p\n", pn, dirfd, flags, mode, orig_openat);

    char pathbuf[MAXPATH];
    if (do_absolutize) {
        
        if (absolutize_path(pathbuf, sizeof pathbuf, pathname, dirfd) == -1) return -1;

        pn = pathbuf;

        if (do_debug) fprintf(stderr, "mapopentounixsocket abs=%s\n", pn);
    } 
    
    if (fnmatch(filesmask, pn, FNM_PATHNAME|FNM_PERIOD) == 0) {        
        struct sockaddr_storage ss;
        socklen_t len;
        
        struct sockaddr* s = (struct sockaddr*) &ss;
        struct sockaddr_un *sa = (struct sockaddr_un*) s;
        
        len = sizeof(*sa);
        memset(sa, 0, len);
        sa->sun_family = AF_UNIX;
            
        snprintf(sa->sun_path, UNIX_PATH_MAX, "%s", pn);
        
        int so = socket(AF_UNIX, SOCK_STREAM, 0);
        
        if (do_debug) fprintf(stderr, "mapopentounixsocket socket->%d\n", so);
        
        if (so == -1) return -1;
        
        int ret = connect(so, s, len);
        if (do_debug) fprintf(stderr, "mapopentounixsocket connect->%d\n", ret);
        
        if (ret == -1) { close(so); return -1; }
        
        int so2 = recv_fd(so);
        if (do_debug) fprintf(stderr, "mapopentounixsocket recv_fd->%d\n", so2);
        
        close(so);
        
        if (so2 == -1) {  return -1; }
        
        return so2;
    }

    if(!orig_openat) {
        orig_openat = dlsym(RTLD_NEXT, "openat");
    }
    
    return (*orig_openat)(dirfd, pathname, flags, mode);
}
