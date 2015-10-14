#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main (int argc, char* argv[])
{
    if (argc < 3 || !strcmp(argv[1], "--help")) {
        fprintf(stderr, "buffered_pipeline - like shell's pipeline, but with AF_UNIX sockets with adjustable buffer size\n");
        fprintf(stderr, "Usage:\n buffered_pipeline delimiter1 argv1_0 argv1_1 ... argv1_N delimiter1 buffer_size1 delimiter2 argv2_0 ... argv2_M delimiter2 buffer_size_2 ... delimiterK\n");
        fprintf(stderr, "Example:\n buffered_pipeline ! ffmpeg -i rtsp://... -c copy -f nut - ! 1000000  ! ffmpeg -f nut -i - -c:v rawvideo -f nut - ! 50000000 ! mplayer -cache 1024 - !\n");
        return 1;
    }
    
    int buffer_size;
    char** oargv = &argv[2];
    const char* delimiter = argv[1];
    
    int i;
    
    for (i=2; i<argc; ++i) {
        if (!strcmp(argv[i], delimiter)) {
            argv[i]=NULL;
            break;
        }
    }
    
    if (i == 2) {
        fprintf(stderr, "Empty command line occured\n");
        return 127;
    }
    
    if (i == argc) {
        fprintf(stderr, "Missing trailing delmiter %s\n", delimiter);
        return 127;
    }
    
    if (i == argc-1) {
        // last chunk - no need for sockets and friends
        execvp(oargv[0], oargv);
        return 127;
    }
    
    buffer_size = atoi(argv[i+1]);
    
    int sv[2] = {-1, -1};
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    if (sv[0] == -1 || sv[1] == -1) {
        perror("socketpair");
        return 127;
    }
    
    if(-1 == setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size))) {
        perror("setsockopt");
    }
    
    
    int childpid = fork();
    
    if (childpid == -1) {
        perror("fork");
        return 127;
    }
    
    if (childpid == 0) {
        if(sv[0] != 1) {
            dup2(sv[0], 1);
            close(sv[0]);
        }
        close(sv[1]);
        execvp(oargv[0], oargv);
        return 127;
    } else {
        if (sv[1] != 0) {
            dup2(sv[1], 0);
            close(sv[1]);
        }
        close(sv[0]);
        argv[i+1]=argv[0];
        execvp(argv[i+1], argv+i+1);
        return 127;
    }
}
