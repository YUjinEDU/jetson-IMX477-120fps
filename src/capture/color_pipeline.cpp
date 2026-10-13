// SPDX-License-Identifier: GPL-2.0-only
// Native V4L2 RAW10 -> BGR. No direct sensor I2C writes and no Argus ISP.
#include <opencv2/opencv.hpp>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <csignal>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <time.h>
#include <cstdint>

static volatile sig_atomic_t stopped=0;
static void stop(int) { stopped=1; }
static uint64_t mono_ns() { timespec t{}; clock_gettime(CLOCK_MONOTONIC,&t); return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec; }
static double now() { return mono_ns()/1e9; }
static double clock_s(clockid_t id) { timespec t{};clock_gettime(id,&t);return t.tv_sec+t.tv_nsec/1e9; }
struct FrameHeader { char magic[8]={'I','M','X','B','G','R','1',0}; uint64_t sequence, receive_ns, color_done_ns, sensor_ns; uint32_t flags,width,height,bytes; double color_ms; };
static_assert(sizeof(FrameHeader)==64,"protocol size");
static void write_pipe(const void* data,size_t bytes) {
 const char* p=(const char*)data;double deadline=now()+2;
 while(bytes && !stopped) { pollfd f{STDOUT_FILENO,POLLOUT,0};if(now()>deadline || poll(&f,1,100)<0)throw std::runtime_error("Pipe write timeout");
 ssize_t n=write(STDOUT_FILENO,p,bytes);if(n>0){p+=n;bytes-=n;}else if(errno!=EAGAIN && errno!=EINTR)throw std::runtime_error("Pipe write failed"); }
}
static void io(int fd, unsigned long op, void* arg) {
    int r; do { r=ioctl(fd,op,arg); } while(r<0 && errno==EINTR && !stopped);
    if(r<0) throw std::runtime_error(std::string("ioctl: ")+strerror(errno));
}
static void ctrl(int fd, unsigned id, long long val, bool wide=false) {
    v4l2_ext_control c{}; c.id=id; if(wide)c.value64=val;else c.value=val;
    v4l2_ext_controls cs{};cs.count=1;cs.controls=&c;io(fd,VIDIOC_S_EXT_CTRLS,&cs);
}
struct Camera {
    int fd=-1; bool streaming=false; std::vector<void*> maps;std::vector<size_t> lengths;
    v4l2_format fmt{};
    ~Camera() {
        if(streaming){int t=V4L2_BUF_TYPE_VIDEO_CAPTURE;ioctl(fd,VIDIOC_STREAMOFF,&t);}
        for(size_t i=0;i<maps.size();++i)munmap(maps[i],lengths[i]);
        if(fd>=0)close(fd);
    }
    void open_camera() {
        fd=open("/dev/video0",O_RDWR|O_NONBLOCK);if(fd<0)throw std::runtime_error("open video0 failed");
        ctrl(fd,0x009a2008,2,true);ctrl(fd,0x009a2064,0);ctrl(fd,0x009a206e,2688);ctrl(fd,0x009a2065,0);
        fmt.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;fmt.fmt.pix.width=1332;fmt.fmt.pix.height=990;
        fmt.fmt.pix.pixelformat=V4L2_PIX_FMT_SRGGB10;fmt.fmt.pix.field=V4L2_FIELD_NONE;io(fd,VIDIOC_S_FMT,&fmt);
        if(fmt.fmt.pix.width!=1332 || fmt.fmt.pix.height!=990 || fmt.fmt.pix.bytesperline<2664)throw std::runtime_error("Unexpected geometry");
        v4l2_requestbuffers req{};req.count=8;req.type=fmt.type;req.memory=V4L2_MEMORY_MMAP;io(fd,VIDIOC_REQBUFS,&req);
        for(unsigned i=0;i<req.count;++i){v4l2_buffer b{};b.type=req.type;b.memory=req.memory;b.index=i;io(fd,VIDIOC_QUERYBUF,&b);
            void* p=mmap(nullptr,b.length,PROT_READ|PROT_WRITE,MAP_SHARED,fd,b.m.offset);
            if(p==MAP_FAILED)throw std::runtime_error("mmap failed");
            maps.push_back(p);lengths.push_back(b.length);io(fd,VIDIOC_QBUF,&b);}
        int type=fmt.type;io(fd,VIDIOC_STREAMON,&type);streaming=true;
        // STREAMON initializes mode2 control ranges, then request 120fps.
        ctrl(fd,0x009a200b,120000000,true);ctrl(fd,0x009a200a,4000,true);ctrl(fd,0x009a2009,32,true);
    }
    v4l2_buffer dequeue(){pollfd p{fd,POLLIN,0};if(poll(&p,1,3000)<=0)throw std::runtime_error("Capture timeout");
        v4l2_buffer b{};b.type=fmt.type;b.memory=V4L2_MEMORY_MMAP;io(fd,VIDIOC_DQBUF,&b);return b;}
    void queue(v4l2_buffer& b){io(fd,VIDIOC_QBUF,&b);}
};
static double percentile(std::vector<double> v,double p){if(v.empty())return 0;std::sort(v.begin(),v.end());return v[std::min(v.size()-1,size_t(p*(v.size()-1)))];}
int main(int argc,char** argv){
    int frames=3600,pipe_width=0;std::string prefix="color-benchmark";bool display=false,pipe=false;double max_seconds=90;int max_errors=32;
    for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--frames" && i+1<argc)frames=std::stoi(argv[++i]);
        else if(a=="--output" && i+1<argc)prefix=argv[++i];else if(a=="--display")display=true;
        else if(a=="--pipe-width" && i+1<argc)pipe_width=std::stoi(argv[++i]);else if(a=="--pipe")pipe=true;else if(a=="--timeout" && i+1<argc)max_seconds=std::stod(argv[++i]);else if(a=="--max-errors" && i+1<argc)max_errors=std::stoi(argv[++i]);else {std::cerr<<"Usage: color_pipeline [--frames N] [--output PREFIX] [--display] [--pipe] [--pipe-width N] [--timeout seconds] [--max-errors N]\n";return 2;}}
    if(frames<2 || max_seconds<=0 || max_errors<1 || pipe_width<0 || pipe_width>1332)return 2;
    if(pipe)fcntl(STDOUT_FILENO,F_SETFL,fcntl(STDOUT_FILENO,F_GETFL)|O_NONBLOCK);
    for(const auto& suffix:{".json",".csv",".jpg"})if(std::ifstream(prefix+suffix).good()){std::cerr<<"Output exists\n";return 2;}
    signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGPIPE,stop);cv::setNumThreads(2);
    try {
        Camera cam;cam.open_camera();int w=cam.fmt.fmt.pix.width,h=cam.fmt.fmt.pix.height;
        cv::Mat raw8(h,w,CV_8U),bgr,transport;unsigned char lut[1024];
        std::vector<unsigned char> cached(cam.fmt.fmt.pix.sizeimage);
        for(int i=0;i<1024;++i)lut[i]=cv::saturate_cast<unsigned char>(255*pow(std::max(0.,(i-64.)/959.),1/2.2));
        std::ofstream csv(prefix+".csv");csv<<"frame,sequence,sensor_timestamp_s,flags,host_receive_monotonic_ns,host_color_done_monotonic_ns,conversion_ms\n";
        std::vector<double> times;double clock_mono=0,clock_boot=0,clock_real=0,clock_steady=0;unsigned timestamp_flags=0;int count=0,errors=0,missing=0;unsigned prev=0;double first=0,last=0,start=0;cv::Scalar mean;
        double deadline=now()+max_seconds;int warm_errors=0;
        for(int warm=0;warm<16&&!stopped;){auto b=cam.dequeue();if(now()>deadline)throw std::runtime_error("Warmup deadline");if(!(b.flags&V4L2_BUF_FLAG_ERROR))++warm;else if(++warm_errors>=max_errors)throw std::runtime_error("Warmup error limit");cam.queue(b);}
        start=now();
        while(count<frames&&!stopped){auto b=cam.dequeue();
            if(now()>deadline)throw std::runtime_error("Capture deadline");
            if(b.flags&V4L2_BUF_FLAG_ERROR){++errors;cam.queue(b);if(errors>=max_errors)throw std::runtime_error("Frame error limit");continue;}
            if(b.index>=cam.maps.size() || b.bytesused!=cam.fmt.fmt.pix.sizeimage)throw std::runtime_error("Bad capture buffer");
            const unsigned sequence=b.sequence,flags=b.flags;
            const uint64_t receive_ns=mono_ns();double ts=b.timestamp.tv_sec+b.timestamp.tv_usec/1e6;
            if(!count){clock_mono=clock_s(CLOCK_MONOTONIC);clock_boot=clock_s(CLOCK_BOOTTIME);clock_real=clock_s(CLOCK_REALTIME);clock_steady=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();timestamp_flags=flags;}
            if(count){if(b.sequence>prev)missing+=b.sequence-prev-1;else throw std::runtime_error("Nonmonotonic sequence");}else first=ts;
            prev=b.sequence;last=ts;
            // VI mmap memory is not ordinary cached RAM. Bulk-copy before
            // per-pixel CPU reads; otherwise each RAW sample is very expensive.
            memcpy(cached.data(),cam.maps[b.index],cached.size());
            for(int y=0;y<h;++y){auto src=(const uint16_t*)(cached.data()+y*cam.fmt.fmt.pix.bytesperline);auto dst=raw8.ptr(y);
                for(int x=0;x<w;++x)dst[x]=lut[src[x]>>6];}
            cv::cvtColor(raw8,bgr,cv::COLOR_BayerRGGB2BGR);
            if(pipe_width)cv::resize(bgr,transport,cv::Size(pipe_width,int(std::round(double(h)*pipe_width/w))),0,0,cv::INTER_LINEAR);else transport=bgr;
            uint64_t done_ns=mono_ns();double ms=(done_ns-receive_ns)/1e6;times.push_back(ms);
            cam.queue(b);csv<<count<<","<<sequence<<","<<std::fixed<<std::setprecision(6)<<ts<<","<<flags<<","<<receive_ns<<","<<done_ns<<","<<ms<<"\n";
            if(!count){cv::imwrite(prefix+".jpg",bgr);mean=cv::mean(bgr);}
            if(pipe){FrameHeader hdr{};hdr.sequence=sequence;hdr.receive_ns=receive_ns;hdr.color_done_ns=done_ns;hdr.sensor_ns=uint64_t(ts*1e9);hdr.flags=flags;hdr.width=transport.cols;hdr.height=transport.rows;hdr.bytes=transport.total()*3;hdr.color_ms=ms;write_pipe(&hdr,sizeof(hdr));write_pipe(transport.data,hdr.bytes);}
            if(display){cv::imshow("IMX477 native RAW -> BGR",bgr);if((cv::waitKey(1)&255)=='q')stopped=1;}
            ++count;
        }
        double elapsed=now()-start,fps=last>first?(count-1)/(last-first):0;
        std::ofstream out(prefix+".json");
        out<<std::setprecision(12)<<"{\n  \"frames\": "<<count<<",\n  \"width\": "<<w<<",\n  \"height\": "<<h
           <<",\n  \"pipe_width\": "<<pipe_width<<",\n  \"sensor_fps\": "<<fps<<",\n  \"wall_processing_fps\": "<<count/elapsed
           <<",\n  \"warmup_frames\": 16,\n  \"warmup_buffer_errors\": "<<warm_errors
           <<",\n  \"buffer_errors\": "<<errors<<",\n  \"missing_sequences\": "<<missing
           <<",\n  \"conversion_p50_ms\": "<<percentile(times,.5)<<",\n  \"conversion_p95_ms\": "<<percentile(times,.95)
           <<",\n  \"mean_bgr\": ["<<mean[0]<<","<<mean[1]<<","<<mean[2]
           <<"],\n  \"display_requested\": "<<(display?"true":"false")
           <<",\n  \"sensor_to_host_latency_ms\": null"
           <<",\n  \"timestamp_flags\": "<<timestamp_flags<<",\n  \"clock_first_sensor_s\": "<<first<<",\n  \"clock_monotonic_s\": "<<clock_mono<<",\n  \"clock_boottime_s\": "<<clock_boot<<",\n  \"clock_realtime_s\": "<<clock_real<<",\n  \"clock_steady_s\": "<<clock_steady<<",\n  \"latency_note\": \"V4L2 timestamp domain unverified; host receive-to-result uses CLOCK_MONOTONIC only\""
           <<",\n  \"processing\": \"CPU LUT RAW10 upper bits, black=64, gamma=2.2, RGGB bilinear BGR; no calibrated ISP\"\n}\n";
        std::cerr<<"frames="<<count<<" sensor_fps="<<fps<<" processing_fps="<<count/elapsed<<" errors="<<errors<<" missing="<<missing<<" convert_p95_ms="<<percentile(times,.95)<<"\n";
        return count==frames && !errors && !missing && fps>119 && fps<121?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
