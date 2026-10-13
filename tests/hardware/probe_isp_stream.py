#!/usr/bin/env python3
"""Bounded ISP NVMM stream probe, no GUI/model or CPU pixel conversion."""
import argparse,json,time
from pathlib import Path
import gi
gi.require_version('Gst','1.0')
from gi.repository import Gst
Gst.init(None)
p=argparse.ArgumentParser();p.add_argument('--mode',type=int,default=2);p.add_argument('--frames',type=int,default=1200);p.add_argument('--output',required=True);p.add_argument('--exposure-max-us',type=int,default=0);p.add_argument('--fixed-controls',action='store_true');a=p.parse_args()
width,height,fps={0:(3840,2160,30),1:(1920,1080,60),2:(1332,990,120)}[a.mode]
controls=f' exposuretimerange="13000 {a.exposure_max_us*1000}"' if a.exposure_max_us else ''
if a.fixed_controls:controls=' exposuretimerange="4000000 4000000" gainrange="2 2" ispdigitalgainrange="1 1" aelock=true awblock=true'
desc=f'nvarguscamerasrc sensor-id=0 sensor-mode={a.mode}{controls} ! video/x-raw(memory:NVMM),format=NV12,width={width},height={height},framerate={fps}/1 ! appsink name=frames max-buffers=8 drop=false sync=false'
pipe=Gst.parse_launch(desc);sink=pipe.get_by_name('frames');bus=pipe.get_bus();rows=[];deadline=time.monotonic()+a.frames/fps*3+20
try:
    if pipe.set_state(Gst.State.PLAYING)==Gst.StateChangeReturn.FAILURE:raise RuntimeError('Start failed')
    for i in range(a.frames+60):
        if time.monotonic()>deadline:raise RuntimeError('Overall deadline exceeded')
        sample=sink.emit('try-pull-sample',3*Gst.SECOND)
        if sample is None:
            msg=bus.pop_filtered(Gst.MessageType.ERROR)
            if msg:
                error,debug=msg.parse_error();raise RuntimeError(f'{error}: {debug}')
            raise RuntimeError('Sample timeout')
        buf=sample.get_buffer()
        if i>=60:rows.append({'pts_ns':int(buf.pts),'host_ns':time.monotonic_ns(),'flags':int(buf.get_flags())})
    pts_span=(rows[-1]['pts_ns']-rows[0]['pts_ns'])/1e9;wall_span=(rows[-1]['host_ns']-rows[0]['host_ns'])/1e9
    gaps=[(b['pts_ns']-x['pts_ns'])/1e6 for x,b in zip(rows,rows[1:])]
    result={'frames':len(rows),'mode':a.mode,'width':width,'height':height,'requested_fps':fps,'pts_fps':(len(rows)-1)/pts_span,'host_delivery_fps':(len(rows)-1)/wall_span,'pts_span_seconds':pts_span,'min_interval_ms':min(gaps),'max_interval_ms':max(gaps),'pipeline':desc,'rows':rows}
    result['passed']=len(rows)==a.frames and .98*fps<result['pts_fps']<1.02*fps and all(0<g<1.5*1000/fps for g in gaps)
    Path(a.output).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='rows'},indent=2),flush=True)
    if not result['passed']:raise RuntimeError('FPS/interval validation failed')
finally:pipe.set_state(Gst.State.NULL);pipe.get_state(5*Gst.SECOND)
