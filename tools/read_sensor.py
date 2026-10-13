import ctypes
import fcntl
import os
import sys
import re
import time
from pathlib import Path

class Message(ctypes.Structure):
    _fields_ = [('addr', ctypes.c_uint16), ('flags', ctypes.c_uint16),
                ('length', ctypes.c_uint16), ('buffer', ctypes.c_void_p)]

class Transfer(ctypes.Structure):
    _fields_ = [('messages', ctypes.POINTER(Message)), ('count', ctypes.c_uint32)]

fd = os.open('/dev/i2c-9', os.O_RDWR)
def write(register, value):
    payload = ctypes.create_string_buffer(register.to_bytes(2, 'big') + bytes([value]))
    messages = (Message * 1)(Message(0x1a, 0, 3, ctypes.addressof(payload)))
    fcntl.ioctl(fd, 0x0707, bytes(Transfer(messages, 1)))

if '--standby' in sys.argv:
    write(0x0100, 0)
if '--apply120' in sys.argv:
    # Temporary sensor-only experiment, applied while V4L2 has powered the sensor.
    # Streaming off is mandatory before changing geometry and PLL registers.
    write(0x0100, 0)
    time.sleep(0.05)
    repo_root = Path(__file__).resolve().parent.parent
    table_path = repo_root / 'driver' / 'src' / 'imx477_mode_tbls.h'
    source = table_path.read_text()
    table = source.split('static const imx477_reg imx477_mode_1332x990_120fps[] = {')[1].split('};')[0]
    for address, value in re.findall(r'\{(0x[\da-fA-F]+),\s*(0x[\da-fA-F]+)\}', table):
        write(int(address, 16), int(value, 16))
    # Existing 2-lane 1080p clock configuration uses an 840 MHz timing clock.
    for address, value in [(0x0342,0x1a),(0x0343,0x0b),(0x0340,0x04),(0x0341,0x1a),
                           (0x0202,0x01),(0x0203,0xf8),(0x0100,1)]:
        write(address, value)
def read(register, length=1):
    address = ctypes.create_string_buffer(register.to_bytes(2, 'big'))
    result = ctypes.create_string_buffer(length)
    messages = (Message * 2)(Message(0x1a, 0, 2, ctypes.addressof(address)),
                             Message(0x1a, 1, length, ctypes.addressof(result)))
    transfer = Transfer(messages, 2)
    fcntl.ioctl(fd, 0x0707, bytes(transfer))
    return int.from_bytes(result.raw, 'big')

for register, length in [(0x0016,2),(0x0100,1),(0x0112,2),(0x0340,2),
                         (0x0342,2),(0x034c,2),(0x034e,2),(0x040c,2),
                         (0x040e,2),(0x0301,1),(0x0303,1),(0x0305,1),
                         (0x0306,2),(0x0309,1),(0x030b,1),(0x030d,1),
                         (0x030e,2),(0x0820,4),(0x0202,2),(0x0900,1)]:
    print(f'{register:04x}: {read(register,length):0{length*2}x}', flush=True)
os.close(fd)
