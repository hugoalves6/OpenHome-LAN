#ifndef MINI_SOCKET_H
#define MINI_SOCKET_H
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#ifdef __NDS__
#include <nds.h>
#include <sys/ioctl.h>
#endif

/* Only for sockets owned by a completed/aborted app request. sgIP keeps
   closesocket() records allocated while TCP closes (up to five minutes).
   At two requests per five seconds its 32 descriptors can run out first.
   Keep ownership while giving FIN a bounded chance, then release the record. */
static inline void mini_socket_close(int sock){
    if(sock<0)return;
#ifdef __NDS__
    int nonblocking=1;
    if(!ioctl(sock,FIONBIO,&nonblocking)){
        unsigned char discard[256];
        shutdown(sock,SHUT_WR);
        for(int frames=0;frames<30;frames++){
            int n=recv(sock,discard,sizeof(discard),0);
            if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINPROGRESS&&errno!=EINTR)break;
            swiWaitForVBlank();
            if(n==0)break;
        }
    }
    /* Do not call closesocket first: a timer could then recycle this handle
       before forceclosesocket, letting us accidentally close another socket. */
    forceclosesocket(sock);
#else
    close(sock);
#endif
}
#endif
