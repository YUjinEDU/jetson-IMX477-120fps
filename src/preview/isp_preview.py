#!/usr/bin/env python3
"""Bounded, standalone stable ISP1080p60 AUTO monitor preview (no YOLO)."""
import signal,time
import cv2
from camera_quality_compare import Argus

stopped=False
def stop(*_):
    global stopped
    stopped=True
signal.signal(signal.SIGTERM,stop)
signal.signal(signal.SIGINT,stop)
camera=Argus(False)
window='ISP AUTO | 1080p60 | Q / ESC to quit'
cv2.namedWindow(window,cv2.WINDOW_NORMAL)
cv2.resizeWindow(window,1280,720)
started=time.monotonic();last_display=0
try:
    print('ISP_PREVIEW_READY',flush=True)
    while not stopped and time.monotonic()-started<1800:
        frame,pts=camera.frame();now=time.monotonic()
        if now-last_display>=1/30:
            cv2.imshow(window,frame);last_display=now
        if cv2.waitKey(1)&255 in (27,ord('q')) or cv2.getWindowProperty(window,cv2.WND_PROP_VISIBLE)<1:break
finally:
    camera.close();cv2.destroyAllWindows()
