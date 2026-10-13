/* SPDX-License-Identifier: GPL-2.0-only */
/* Direct RAW capture for IMX477 on Jetson. Sensor programming occurs only
 * while V4L2 owns and powers the sensor. STREAMOFF runs on all exit paths. */
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "sensor_tables.h"
#include "rpi_tables.h"

static volatile sig_atomic_t interrupted;
static void stop(int sig) { (void)sig; interrupted = 1; }
static int xioctl(int fd, unsigned long request, void *arg) {
    int rc; do { rc = ioctl(fd, request, arg); } while (rc < 0 && errno == EINTR && !interrupted);
    return rc;
}
static int control(int fd, uint32_t id, int64_t value, bool wide) {
    struct v4l2_ext_control c = {.id=id};
    if (wide) c.value64=value; else c.value=value;
    struct v4l2_ext_controls cs = {.count=1,.controls=&c};
    return xioctl(fd, VIDIOC_S_EXT_CTRLS, &cs);
}
static int write_reg(int fd, uint16_t reg, uint8_t value) {
    uint8_t data[] = {reg >> 8, reg & 255, value};
    struct i2c_msg msg = {.addr=0x1a,.len=3,.buf=data};
    struct i2c_rdwr_ioctl_data transfer = {.msgs=&msg,.nmsgs=1};
    return xioctl(fd,I2C_RDWR,&transfer) < 0 ? -1 : 0;
}
static int table(int fd, const struct regval *regs) {
    for (; regs->addr != 1; ++regs) {
        if (!regs->addr) usleep(regs->val*1000);
        else if (write_reg(fd,regs->addr,regs->val)) return -1;
    }
    return 0;
}
static int configure(int fd, int pattern) {
    if (write_reg(fd,0x0100,0)) return -1;
    usleep(50000);
    if (write_reg(fd,0x0103,1)) return -1;
    usleep(10000);
    if (table(fd,rpi_mode_common_regs) || table(fd,rpi_mode_1332x990_regs)) return -1;
    /* 840 MHz / 6667 clocks / 1050 lines, exposure about 4 ms, 2x gain. */
    const struct regval timing[] = {{0x0342,0x1a},{0x0343,0x0b},
        {0x0340,0x04},{0x0341,0x1a},{0x0202,0x01},{0x0203,0xf8},
        {0x0204,0x02},{0x0205,0x00},{0x0112,0x0a},{0x0113,0x0a},
        {0x0309,0x0a},{0x3f0d,0},{0x030e,0},{0x030f,0x96},
        {0x0820,0x07},{0x0821,0x08},{0x0822,0},{0x0823,0},
        {0x0b05,0},{0x0b06,0},
        {0x0602,0x0f},{0x0603,0xff},{0x0604,0},{0x0605,0},
        {0x0606,0},{0x0607,0},{0x0608,0},{0x0609,0},
        {0x0600,0},{0x0601,pattern},
        {0x0100,1},{1,0}};
    return table(fd,timing);
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr,"Usage: %s OUTPUT.raw FRAMES [--test-pattern]\n",argv[0]); return 2; }
    int target = atoi(argv[2]);
    if (target < 2 || target > 120000) return 2;
    int pattern = argc > 3 && !strcmp(argv[3],"--test-pattern") ? 2 :
        argc > 3 && !strcmp(argv[3],"--solid-red") ? 1 : 0;
    unsigned stride=2688;
    const char *stride_env=getenv("IMX477_STRIDE");
    if(stride_env) {
        char *end=NULL; unsigned long value=strtoul(stride_env,&end,10);
        if(*end || value<2664 || value>8192 || value%64) {
            fprintf(stderr,"IMX477_STRIDE must be a multiple of 64 between 2688 and 8192\n");
            return 2;
        }
        stride=value;
    }
    signal(SIGINT,stop); signal(SIGTERM,stop);
    int rc=1, fd=-1, i2c=-1; bool streaming=false;
    FILE *out=NULL,*csv=NULL; void *maps[8]={0}; size_t lengths[8]={0};
    char name[4096];
    if (snprintf(name,sizeof(name),"%s.csv",argv[1]) >= (int)sizeof(name)) return 2;
    fd=open("/dev/video0",O_RDWR|O_NONBLOCK); if(fd<0) goto cleanup;
    if (control(fd,0x009a2008,2,true) || control(fd,0x009a2064,0,false) ||
        control(fd,0x009a206e,stride,false)) goto cleanup;
    struct v4l2_format fmt={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE};
    fmt.fmt.pix.width=1332; fmt.fmt.pix.height=990; fmt.fmt.pix.pixelformat=V4L2_PIX_FMT_SRGGB10;
    fmt.fmt.pix.field=V4L2_FIELD_NONE;
    if(xioctl(fd,VIDIOC_S_FMT,&fmt)) goto cleanup;
    fprintf(stderr,"FORMAT width=%u height=%u stride=%u size=%u\n",fmt.fmt.pix.width,
        fmt.fmt.pix.height,fmt.fmt.pix.bytesperline,fmt.fmt.pix.sizeimage);
    if(fmt.fmt.pix.width!=1332 || fmt.fmt.pix.height!=990 || fmt.fmt.pix.bytesperline!=stride) {
        fprintf(stderr,"Unexpected capture format/stride\n"); goto cleanup;
    }
    if(control(fd,0x009a2065,1,false) || control(fd,0x009a200b,120000000,true) ||
       control(fd,0x009a200a,4000,true) || control(fd,0x009a2009,32,true)) goto cleanup;
    struct v4l2_requestbuffers req={.count=8,.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.memory=V4L2_MEMORY_MMAP};
    if(xioctl(fd,VIDIOC_REQBUFS,&req) || req.count>8) goto cleanup;
    for(unsigned i=0;i<req.count;i++) {
        struct v4l2_buffer b={.type=req.type,.memory=req.memory,.index=i};
        if(xioctl(fd,VIDIOC_QUERYBUF,&b)) goto cleanup;
        lengths[i]=b.length;
        maps[i]=mmap(NULL,b.length,PROT_READ|PROT_WRITE,MAP_SHARED,fd,b.m.offset);
        if(maps[i]==MAP_FAILED) { maps[i]=NULL; goto cleanup; }
        if(xioctl(fd,VIDIOC_QBUF,&b)) goto cleanup;
    }
    int type=req.type;
    if(xioctl(fd,VIDIOC_STREAMON,&type)) goto cleanup;
    streaming=true;
    i2c=open("/dev/i2c-9",O_RDWR); if(i2c<0 || configure(i2c,pattern)) goto cleanup;
    out=fopen(argv[1],"wbx"); csv=fopen(name,"wx"); if(!out||!csv) goto cleanup;
    setvbuf(out,NULL,_IOFBF,8*1024*1024);
    fprintf(csv,"frame,sequence,timestamp_us,bytesused\n");
    int saved=0,errors=0,warmup=12,drops=0; unsigned lastseq=0; int64_t first=0,last=0;
    time_t deadline=time(NULL)+target/100+20;
    while(saved<target && !interrupted && time(NULL)<deadline) {
        struct pollfd p={.fd=fd,.events=POLLIN};
        if(poll(&p,1,3000)<=0) { fprintf(stderr,"Frame wait failed\n"); goto cleanup; }
        struct v4l2_buffer b={.type=req.type,.memory=req.memory};
        if(xioctl(fd,VIDIOC_DQBUF,&b)) { if(errno==EAGAIN) continue; goto cleanup; }
        if(b.flags & V4L2_BUF_FLAG_ERROR) errors++;
        else if(warmup>0) warmup--;
        else {
            if(b.bytesused!=fmt.fmt.pix.sizeimage || b.index>=req.count) { fprintf(stderr,"Unexpected buffer size\n"); goto cleanup; }
            int64_t ts=(int64_t)b.timestamp.tv_sec*1000000+b.timestamp.tv_usec;
            if(!saved) first=ts; else if(b.sequence!=lastseq+1) drops+=b.sequence-lastseq-1;
            last=ts; lastseq=b.sequence;
            if(fwrite(maps[b.index],1,b.bytesused,out)!=b.bytesused) goto cleanup;
            fprintf(csv,"%d,%u,%lld,%u\n",saved,b.sequence,(long long)ts,b.bytesused);
            saved++;
            if(saved%120==0) fprintf(stderr,"Captured %d/%d frames\n",saved,target);
        }
        if(xioctl(fd,VIDIOC_QBUF,&b)) goto cleanup;
    }
    fprintf(stderr,"RESULT frames=%d fps=%.6f drops=%d startup_errors=%d test_pattern=%d\n",
        saved,last>first?(saved-1)*1000000.0/(last-first):0.0,drops,errors,pattern);
    rc=(saved==target && !drops)?0:1;
cleanup:
    if(rc && errno) perror("capture120");
    if(i2c>=0) { write_reg(i2c,0x0100,0); close(i2c); }
    if(streaming) { int type=V4L2_BUF_TYPE_VIDEO_CAPTURE; xioctl(fd,VIDIOC_STREAMOFF,&type); }
    for(int i=0;i<8;i++) if(maps[i]) munmap(maps[i],lengths[i]);
    if(fd>=0) close(fd);
    if(out && fclose(out)) rc=1;
    if(csv && fclose(csv)) rc=1;
    return rc;
}
