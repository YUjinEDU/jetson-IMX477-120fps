#!/usr/bin/env python3
"""Sequential RAW120 / Argus ISP60 snapshots and local ISP live viewer."""
import argparse,json,signal,subprocess,time
from pathlib import Path
import cv2,numpy as np
import gi
gi.require_version('Gst','1.0');gi.require_version('GstVideo','1.0')
from gi.repository import Gst,GstVideo
ROOT=Path(__file__).resolve().parent
Gst.init(None)

class Argus:
    def __init__(self,manual=False):
        controls=' exposuretimerange="4000000 4000000" gainrange="2 2" ispdigitalgainrange="1 1"' if manual else ''
        self.description=('nvarguscamerasrc sensor-id=0 sensor-mode=1'+controls+' ! video/x-raw(memory:NVMM),width=1920,height=1080,framerate=60/1,format=NV12 ! nvvidconv ! video/x-raw,format=BGRx ! videoconvert ! video/x-raw,format=BGR ! appsink name=frames max-buffers=1 drop=true sync=false')
        self.pipeline=Gst.parse_launch(self.description);self.sink=self.pipeline.get_by_name('frames');self.bus=self.pipeline.get_bus()
        if self.pipeline.set_state(Gst.State.PLAYING)==Gst.StateChangeReturn.FAILURE:raise RuntimeError('Argus failed to start')
    def frame(self):
        sample=self.sink.emit('try-pull-sample',2*Gst.SECOND)
        if sample is None:
            msg=self.bus.pop_filtered(Gst.MessageType.ERROR)
            if msg:
                error,debug=msg.parse_error();raise RuntimeError(f'{error}: {debug}')
            raise RuntimeError('Argus frame timeout')
        info=GstVideo.VideoInfo.new_from_caps(sample.get_caps());buf=sample.get_buffer();ok,mapped=buf.map(Gst.MapFlags.READ)
        if not ok:raise RuntimeError('Argus map failed')
        try:
            image=np.ndarray((info.height,info.width,3),np.uint8,buffer=mapped.data,offset=info.offset[0],strides=(info.stride[0],3,1)).copy()
        finally:buf.unmap(mapped)
        return image,buf.pts
    def close(self):self.pipeline.set_state(Gst.State.NULL);self.pipeline.get_state(5*Gst.SECOND)

def snapshot(manual,path):
    camera=Argus(manual);started=time.monotonic();count=0;first_pts=None
    try:
        while time.monotonic()-started<12:
            frame,pts=camera.frame();count+=1
            if first_pts is None:first_pts=pts
            if count>=120:
                cv2.imwrite(str(path),frame)
                return {'frames_received':count,'shape':list(frame.shape),'pts_fps':(count-1)*Gst.SECOND/(pts-first_pts),'mean_bgr':frame.mean(axis=(0,1)).tolist(),'pipeline':camera.description,'controls':'requested exposure4ms, analog2x, digital1x, auto WB' if manual else 'Argus auto exposure/gain/WB defaults'}
        raise RuntimeError('snapshot warmup deadline')
    finally:camera.close()

def tile(canvas,image,x,y,w,h,title,detail):
    cv2.putText(canvas,title,(x,y-34),cv2.FONT_HERSHEY_SIMPLEX,.58,(230,230,230),1,cv2.LINE_AA)
    cv2.putText(canvas,detail,(x,y-12),cv2.FONT_HERSHEY_SIMPLEX,.4,(170,170,170),1,cv2.LINE_AA)
    scale=min(w/image.shape[1],h/image.shape[0]);ow=round(image.shape[1]*scale);oh=round(image.shape[0]*scale)
    view=cv2.resize(image,(ow,oh));xx=x+(w-ow)//2;yy=y+(h-oh)//2
    canvas[yy:yy+oh,xx:xx+ow]=view

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--seconds',type=int,default=1800);args=parser.parse_args()
    prefix=ROOT/'benchmark'/('quality-compare-'+time.strftime('%Y%m%d-%H%M%S'))
    raw_prefix=str(prefix)+'-raw120'
    print('QUALITY_STAGE raw120 original resolution, no preview correction',flush=True)
    subprocess.run(['timeout','-k','3','20',str(ROOT/'color_pipeline'),'--frames','360','--timeout','15','--output',raw_prefix],check=True)
    print('QUALITY_STAGE ISP60 matched exposure/gain request',flush=True)
    manual=snapshot(True,Path(str(prefix)+'-isp60-manual.jpg'))
    print('QUALITY_STAGE ISP60 auto exposure/gain/WB',flush=True)
    auto=snapshot(False,Path(str(prefix)+'-isp60-auto.jpg'))
    raw=cv2.imread(raw_prefix+'.jpg');man=cv2.imread(str(prefix)+'-isp60-manual.jpg');aut=cv2.imread(str(prefix)+'-isp60-auto.jpg')
    report={'raw120':json.loads(Path(raw_prefix+'.json').read_text()),'isp60_manual':manual,'isp60_auto':auto,'note':'sequential snapshots: different resolution/crop and ISP; optical focus unchanged; not an isolated ISP benchmark. Manual values are requests, not measured sensor metadata.'}
    Path(str(prefix)+'.json').write_text(json.dumps(report,indent=2)+'\n')
    comparison=np.full((640,1680,3),18,np.uint8)
    tile(comparison,raw,20,110,530,450,'RAW 120 / SOFTWARE COLOR','1332x990 original | 4ms / 2x | no ISP')
    tile(comparison,man,575,110,530,450,'ISP 60 / FIXED EXPOSURE','1920x1080 | request 4ms / 2x | auto WB')
    tile(comparison,aut,1130,110,530,450,'ISP 60 / AUTO','1920x1080 | auto exposure / gain / WB')
    cv2.putText(comparison,'Same lens / no focus changes by software. Different sensor modes and fields of view.',(20,35),cv2.FONT_HERSHEY_SIMPLEX,.55,(210,210,210),1,cv2.LINE_AA)
    cv2.putText(comparison,'Snapshots captured sequentially; not simultaneous. Image processing is not lens focusing.',(20,603),cv2.FONT_HERSHEY_SIMPLEX,.48,(160,160,160),1,cv2.LINE_AA)
    cv2.imwrite(str(prefix)+'-comparison.jpg',comparison)
    win_compare='CAMERA QUALITY | RAW120 vs ISP60 snapshots';win_live='ISP LIVE | 1080p60 AUTO | Q / ESC to quit'
    cv2.namedWindow(win_compare,cv2.WINDOW_NORMAL);cv2.resizeWindow(win_compare,1680,640);cv2.moveWindow(win_compare,30,50);cv2.imshow(win_compare,comparison)
    cv2.namedWindow(win_live,cv2.WINDOW_NORMAL);cv2.resizeWindow(win_live,960,540);cv2.moveWindow(win_live,1750,100)
    stopped=[False];signal.signal(signal.SIGTERM,lambda *_:stopped.__setitem__(0,True));signal.signal(signal.SIGINT,lambda *_:stopped.__setitem__(0,True))
    camera=Argus(False);started=time.monotonic();last_display=0.;count=0
    print('QUALITY_PREVIEW_READY '+str(prefix),flush=True)
    try:
        while not stopped[0] and time.monotonic()-started<args.seconds:
            frame,pts=camera.frame();count+=1;now=time.monotonic()
            if now-last_display>.033:
                cv2.imshow(win_live,frame);last_display=now
            key=cv2.waitKey(1)&255
            if key in (27,ord('q')) or cv2.getWindowProperty(win_live,cv2.WND_PROP_VISIBLE)<1:break
    finally:camera.close();cv2.destroyAllWindows();print(f'QUALITY_PREVIEW_STOP frames={count}',flush=True)

if __name__=='__main__':main()
