/* IPv4 DNS fallback for the SP1 host database ABI. No Clib dependency:
 * this is included in the resident POSIX library's scandb.c. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>

#ifndef KTM_RESOLV_CONF
#define KTM_RESOLV_CONF "/helios/etc/resolv.conf"
#endif
#ifndef KTM_DNS_PORT
#define KTM_DNS_PORT 53
#endif
#ifndef KTM_DNS_ALLOC
#define KTM_DNS_ALLOC Malloc
#define KTM_DNS_FREE Free
#endif
#define KTM_DNS_MAX 4096

struct ktm_dns_config {
    unsigned char ns[3][4];
    int count, timeout, attempts, ndots, searches;
    char search[6][256];
};
struct ktm_dns_work {
    struct ktm_dns_config config;
    unsigned char query[512], answer[KTM_DNS_MAX];
    char configtext[2048], name[256], candidate[256], canonical[256];
    unsigned char addresses[32][4];
    int address_count;
};

static unsigned ktm_dns_u16(const unsigned char *p)
{ return ((unsigned)p[0]<<8)|p[1]; }
static void ktm_dns_put16(unsigned char *p, unsigned n)
{ p[0]=n>>8; p[1]=n; }
static int ktm_dns_equal(const char *a,const char *b)
{
    unsigned x,y;
    do { x=(unsigned char)*a++;y=(unsigned char)*b++;
         if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;
         if(x!=y)return 0;
    }while(x);
    return 1;
}
static int ktm_dns_ipv4(const char *s,unsigned char *out)
{
    int i,n,digits;
    for(i=0;i<4;i++) {
        n=digits=0;
        while(*s>='0'&&*s<='9') {n=n*10+*s++-'0';if(++digits>3||n>255)return 0;}
        if(!digits)return 0;out[i]=n;
        if(i<3){if(*s++!='.')return 0;}else if(*s)return 0;
    }
    return 1;
}
static char *ktm_dns_token(char **cursor)
{
    char *s=*cursor,*start;
    while(*s==' '||*s=='\t'||*s=='\r')s++;
    if(!*s||*s=='#'||*s==';'){*cursor=s;return NULL;}
    start=s;while(*s&&*s!=' '&&*s!='\t'&&*s!='\r'&&*s!='#'&&*s!=';')s++;
    if(*s=='#'||*s==';'){*s=0;*cursor=s;}else{if(*s)*s++=0;*cursor=s;}
    return start;
}
static int ktm_dns_number(const char *s,int low,int high)
{
    int n=0;if(!*s)return low;
    while(*s>='0'&&*s<='9'){n=n*10+*s++-'0';if(n>high)return high;}
    return n<low?low:n;
}
static void ktm_dns_configure(struct ktm_dns_work *w)
{
    struct ktm_dns_config *c=&w->config;
    int fd,n,used=0;char *line,*end,*p,*key,*value;
    memset(c,0,sizeof(*c));c->timeout=2;c->attempts=2;c->ndots=1;
    fd=open(KTM_RESOLV_CONF,O_RDONLY);
    if(fd<0)return;
    while(used<(int)sizeof(w->configtext)-1 &&
          (n=read(fd,w->configtext+used,sizeof(w->configtext)-1-used))>0)used+=n;
    close(fd);w->configtext[used]=0;line=w->configtext;
    while(*line) {
        end=line;while(*end&&*end!='\n')end++;
        if(*end)*end++=0;p=line;line=end;
        key=ktm_dns_token(&p);if(!key)continue;
        if(!strcmp(key,"nameserver")) {
            value=ktm_dns_token(&p);
            if(value&&c->count<3&&ktm_dns_ipv4(value,c->ns[c->count]))c->count++;
        }else if(!strcmp(key,"search")||!strcmp(key,"domain")) {
            c->searches=0;
            while((value=ktm_dns_token(&p))!=NULL&&c->searches<6) {
                if(strlen(value)<254)strcpy(c->search[c->searches++],value);
                if(!strcmp(key,"domain"))break;
            }
        }else if(!strcmp(key,"options")) {
            while((value=ktm_dns_token(&p))!=NULL) {
                if(!strncmp(value,"timeout:",8))c->timeout=ktm_dns_number(value+8,1,5);
                if(!strncmp(value,"attempts:",9))c->attempts=ktm_dns_number(value+9,1,3);
                if(!strncmp(value,"ndots:",6))c->ndots=ktm_dns_number(value+6,0,15);
            }
        }
    }
}
/* Decode a compressed name with explicit bounds and a pointer-loop limit.
 * Return the position after the encoded name, not after its pointer target. */
static int ktm_dns_name(const unsigned char *p,int size,int pos,char *out)
{
    int end=-1,used=0,steps=0,n;
    while(pos<size&&++steps<=size) {
        n=p[pos++];
        if(!n){if(end<0)end=pos;out[used]=0;return end;}
        if((n&192)==192){if(pos>=size)return -1;if(end<0)end=pos+1;
            pos=((n&63)<<8)|p[pos];continue;}
        if((n&192)||pos+n>size||used+n+(used!=0)>253)return -1;
        if(used)out[used++]='.';
        while(n--){if(!p[pos]||p[pos]=='.')return -1;out[used++]=p[pos++];}
    }
    return -1;
}
static int ktm_dns_question(unsigned char *p,const char *name,int type,unsigned id)
{
    const char *s=name,*e;int pos=12,n;
    memset(p,0,12);ktm_dns_put16(p,id);p[2]=1;p[5]=1;
    while(*s) {
        e=s;while(*e&&*e!='.')e++;n=e-s;
        if(n<1||n>63||pos+n+6>512)return -1;
        p[pos++]=n;memcpy(p+pos,s,n);pos+=n;s=*e?e+1:e;
    }
    p[pos++]=0;ktm_dns_put16(p+pos,type);pos+=2;ktm_dns_put16(p+pos,1);
    return pos+2;
}
static unsigned long ktm_dns_millis(void)
{
    struct timeval tv;gettimeofday(&tv,NULL);
    return tv.tv_sec*1000+tv.tv_usec/1000;
}
static int ktm_dns_wait(int fd,int writing,unsigned long deadline)
{
    fd_set set;struct timeval tv;long remaining;int n;
    if(fd<0 || fd>=FD_SETSIZE){errno=EMFILE;return 0;}
    for(;;) {
        remaining=(long)(deadline-ktm_dns_millis());if(remaining<=0)return 0;
        FD_ZERO(&set);FD_SET(fd,&set);tv.tv_sec=remaining/1000;tv.tv_usec=(remaining%1000)*1000;
        n=select(fd+1,writing?NULL:&set,writing?&set:NULL,NULL,&tv);
        if(n<0&&errno==EINTR)continue;return n>0;
    }
}
static int ktm_dns_transfer(int fd,unsigned char *p,int length,int writing,unsigned long deadline)
{
    int done=0,n;
    while(done<length) {
        if(!ktm_dns_wait(fd,writing,deadline))return -1;
        n=writing?send(fd,p+done,length-done,0):recv(fd,p+done,length-done,0);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
        if(n<=0)return -1;done+=n;
    }
    return done;
}
static int ktm_dns_tcp(struct ktm_dns_work *w,struct sockaddr_in *server,int qlen,unsigned long deadline)
{
    unsigned char prefix[2];int fd,n=-1,err,len;
    fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
    if(fcntl(fd,F_SETFL,O_NONBLOCK)<0)goto done;
    if(connect(fd,(struct sockaddr *)server,sizeof(*server))<0) {
        if(errno!=EINPROGRESS&&errno!=EWOULDBLOCK)goto done;
        if(!ktm_dns_wait(fd,1,deadline))goto done;
        len=sizeof(err);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&len)<0||err)goto done;
    }
    ktm_dns_put16(prefix,qlen);
    if(ktm_dns_transfer(fd,prefix,2,1,deadline)<0||
       ktm_dns_transfer(fd,w->query,qlen,1,deadline)<0||
       ktm_dns_transfer(fd,prefix,2,0,deadline)<0)goto done;
    len=ktm_dns_u16(prefix);if(len<12||len>KTM_DNS_MAX)goto done;
    n=ktm_dns_transfer(fd,w->answer,len,0,deadline);
done:close(fd);return n;
}
/* 0: answer, 1: name/type absent, 2: temporary/malformed response,
 * 3: CNAME target requires another question. */
static int ktm_dns_parse(struct ktm_dns_work *w,int size,int type,unsigned char *address)
{
    unsigned char *p=w->answer;char owner[256],target[256];
    int pos,count,i,pass,t,n,data,found;
    w->address_count=0;
    if(size<12||memcmp(p,w->query,2)||!(p[2]&128)||(p[2]&122)||ktm_dns_u16(p+4)!=1)return 2;
    pos=ktm_dns_name(p,size,12,owner);
    if(pos<0||pos+4>size||!ktm_dns_equal(owner,w->candidate)||
       ktm_dns_u16(p+pos)!=type||ktm_dns_u16(p+pos+2)!=1)return 2;
    if((p[3]&15)==3)return 1;if(p[3]&15)return 2;
    pos+=4;data=pos;count=ktm_dns_u16(p+6);strcpy(w->canonical,w->candidate);
    for(pass=0;pass<9;pass++) {
        pos=data;found=0;
        for(i=0;i<count;i++) {
            pos=ktm_dns_name(p,size,pos,owner);if(pos<0||pos+10>size)return 2;
            t=ktm_dns_u16(p+pos);n=ktm_dns_u16(p+pos+8);pos+=10;
            if(pos+n>size)return 2;
            if(ktm_dns_u16(p+pos-8)==1&&ktm_dns_equal(owner,w->canonical)) {
                if(t==type&&type==1&&n==4) {
                    int j;
                    for(j=0;j<w->address_count;j++)if(!memcmp(w->addresses[j],p+pos,4))break;
                    if(j==w->address_count&&j<32)memcpy(w->addresses[w->address_count++],p+pos,4);
                }
                if(t==type&&type==12) {
                    int end=ktm_dns_name(p,size,pos,target);
                    if(end<0||end!=pos+n)return 2;strcpy(w->canonical,target);return 0;
                }
                if(t==5) {
                    int end=ktm_dns_name(p,size,pos,target);
                    if(end<0||end!=pos+n||!target[0])return 2;
                    strcpy(w->canonical,target);found=1;break;
                }
            }
            pos+=n;
        }
        if(w->address_count){memcpy(address,w->addresses[0],4);return 0;}
        if(!found)return pass?3:1;
    }
    return 2;
}
static int ktm_dns_query(struct ktm_dns_work *w,int type,unsigned char *address)
{
    struct sockaddr_in server;struct timeval now;
    unsigned long deadline;int attempt,ns,fd,n,qlen,status=2,depth;
    unsigned id;
    for(depth=0;depth<9;depth++) {
        gettimeofday(&now,NULL);id=(now.tv_sec^now.tv_usec^getpid()^(depth*31))&65535;
        qlen=ktm_dns_question(w->query,w->candidate,type,id);if(qlen<0)return 1;
        for(attempt=0;attempt<w->config.attempts;attempt++)for(ns=0;ns<w->config.count;ns++) {
            memset(&server,0,sizeof(server));server.sin_family=AF_INET;
            ktm_dns_put16((unsigned char *)&server.sin_port,KTM_DNS_PORT);
            memcpy(&server.sin_addr,w->config.ns[ns],4);
            deadline=ktm_dns_millis()+w->config.timeout*1000;
            fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return 2;
            n=-1;
            if(connect(fd,(struct sockaddr *)&server,sizeof(server))==0&&send(fd,w->query,qlen,0)==qlen) {
                while(ktm_dns_wait(fd,0,deadline)) {
                    n=recv(fd,w->answer,KTM_DNS_MAX,0);
                    if(n>=12&&!memcmp(w->answer,w->query,2))break;
                    n=-1;
                }
            }
            close(fd);
            if(n>=12&&(w->answer[2]&2))n=ktm_dns_tcp(w,&server,qlen,deadline);
            status=n<0?2:ktm_dns_parse(w,n,type,address);
            if(status==0||status==1)return status;
            if(status==3)goto alias;
        }
        return 2;
alias:  strcpy(w->candidate,w->canonical);
    }
    return 2;
}
static int ktm_dns_lookup_all(const char *name,int reverse,unsigned char *address,char *canonical,
                              unsigned char (*addresses)[4],int *count)
{
    struct ktm_dns_work *w;int status=2,i,dots=0,absolute,len,n;char *p;
    if(count)*count=0;
    if(!name||strlen(name)>253||!*name)return 1;
    if(!reverse&&ktm_dns_ipv4(name,address)){
        strcpy(canonical,name);if(addresses)memcpy(addresses[0],address,4);if(count)*count=1;return 0;
    }
    w=KTM_DNS_ALLOC(sizeof(*w));if(!w)return 2;
    ktm_dns_configure(w);strcpy(w->name,name);len=strlen(name);
    absolute=w->name[len-1]=='.';if(absolute)w->name[--len]=0;
    for(i=0;i<len;i++)if(w->name[i]=='.')dots++;
    if(reverse) {
        p=w->name;
        for(i=3;i>=0;i--) {
            n=address[i];if(n>=100)*p++='0'+n/100;
            if(n>=10)*p++='0'+(n/10)%10;*p++='0'+n%10;*p++='.';
        }
        strcpy(p,"in-addr.arpa");
    }
    if(reverse||absolute||dots>=w->config.ndots) {
        strcpy(w->candidate,w->name);status=ktm_dns_query(w,reverse?12:1,address);
        if(status==0||reverse||absolute)goto done;
    }
    for(i=0;i<w->config.searches;i++) {
        n=strlen(w->config.search[i]);if(len+n+1>253)continue;
        memcpy(w->candidate,w->name,len);w->candidate[len]='.';
        strcpy(w->candidate+len+1,w->config.search[i]);
        status=ktm_dns_query(w,1,address);if(status==0)goto done;
    }
    if(dots<w->config.ndots) {strcpy(w->candidate,w->name);status=ktm_dns_query(w,1,address);}
done:
    if(!status){
        strcpy(canonical,w->canonical);
        if(addresses)memcpy(addresses,w->addresses,w->address_count*4);
        if(count)*count=w->address_count;
    }
    KTM_DNS_FREE(w);return status;
}
static int ktm_dns_lookup(const char *name,int reverse,unsigned char *address,char *canonical)
{return ktm_dns_lookup_all(name,reverse,address,canonical,NULL,NULL);}
