"""稳定 C ABI 的 Python ctypes 调用示例。

Usage: python3 ctypes_fire_filter.py /path/to/libcv_sdk.dylib ../../models/fire_smoke_640/fire_rules.json
"""
import ctypes as ct
import sys

class Detection(ct.Structure):
    _fields_ = [("x", ct.c_float), ("y", ct.c_float), ("width", ct.c_float),
                ("height", ct.c_float), ("score", ct.c_float), ("class_id", ct.c_int32)]

class AlertState(ct.Structure):
    _fields_ = [("struct_size", ct.c_uint32), ("level", ct.c_int),
                ("max_fire_confidence", ct.c_float), ("max_smoke_confidence", ct.c_float),
                ("fire_hits", ct.c_uint32), ("smoke_hits", ct.c_uint32), ("reason", ct.c_char * 96)]

lib = ct.CDLL(sys.argv[1])
handle = ct.c_void_p()
assert lib.CVSDK_FireFilterCreate(sys.argv[2].encode(), ct.byref(handle)) == 0
fire = Detection(100, 100, 80, 80, .60, 1)  # 1=fire; 0=smoke
state = AlertState(ct.sizeof(AlertState))
for _ in range(3):
    assert lib.CVSDK_FireFilterProcess(handle, 1280, 720, ct.byref(fire), 1, ct.byref(state)) == 0
print(state.level, state.reason.decode())
lib.CVSDK_FireFilterDestroy(handle)
