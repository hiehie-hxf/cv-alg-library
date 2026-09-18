"""稳定 C ABI 的 Python ctypes 调用示例。

Usage: python3 ctypes_leak.py /path/to/libcv_sdk.so models/leak_seg_1280 tests/data/leak/leak01.jpg
"""
import ctypes as ct
import sys

import cv2


class LeakItem(ct.Structure):
    _fields_ = [("x", ct.c_float), ("y", ct.c_float), ("width", ct.c_float),
                ("height", ct.c_float), ("score", ct.c_float), ("centroid_x", ct.c_float),
                ("centroid_y", ct.c_float), ("area", ct.c_uint32)]


class LeakItemList(ct.Structure):
    _fields_ = [("struct_size", ct.c_uint32), ("items", ct.POINTER(LeakItem)),
                ("capacity", ct.c_uint32), ("count", ct.c_uint32)]


class LeakAlertState(ct.Structure):
    _fields_ = [("struct_size", ct.c_uint32), ("level", ct.c_int),
                ("leak_count", ct.c_uint32), ("hits", ct.c_uint32),
                ("window_filled", ct.c_uint32), ("total_area", ct.c_uint32),
                ("centroid_y", ct.c_float), ("max_confidence", ct.c_float),
                ("area_growing", ct.c_uint32), ("centroid_down", ct.c_uint32),
                ("reason", ct.c_char * 96)]


class Image(ct.Structure):
    _fields_ = [("struct_size", ct.c_uint32), ("data", ct.POINTER(ct.c_uint8)),
                ("width", ct.c_uint32), ("height", ct.c_uint32),
                ("stride_bytes", ct.c_uint32), ("pixel_format", ct.c_int)]


lib = ct.CDLL(sys.argv[1])
handle = ct.c_void_p()
assert lib.CVSDK_LeakProcessorCreate(sys.argv[2].encode(), None, ct.byref(handle)) == 0

frame = cv2.imread(sys.argv[3])
assert frame is not None, "cannot read image"
# 1 = CVSDK_PIXEL_FORMAT_BGR8；stride 直接取自 numpy 的行跨度。
image = Image(ct.sizeof(Image), frame.ctypes.data_as(ct.POINTER(ct.c_uint8)), frame.shape[1],
              frame.shape[0], frame.strides[0], 1)

items = (LeakItem * 16)()
result = LeakItemList(ct.sizeof(LeakItemList), items, 16, 0)
state = LeakAlertState(ct.sizeof(LeakAlertState))
assert lib.CVSDK_LeakProcessorProcess(handle, ct.byref(image), ct.byref(result),
                                      ct.byref(state)) == 0
print("leaks=%d hits=%d level=%d total_area=%d reason=%s" %
      (result.count, state.hits, state.level, state.total_area, state.reason.decode()))
for index in range(result.count):
    print("  [%d] score=%.4f box=[%.1f,%.1f,%.1f,%.1f] area=%d centroid_y=%.1f" %
          (index, items[index].score, items[index].x, items[index].y, items[index].width,
           items[index].height, items[index].area, items[index].centroid_y))
lib.CVSDK_LeakProcessorDestroy(handle)
