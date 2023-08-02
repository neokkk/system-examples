BUILDROOT_DIR=/home/nk/Workspaces/buildroot
TOOLCHAIN_DIR=${BUILDROOT_DIR}/output/host/bin

CC=${TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu-gcc
CXX=${TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu-g++

# CC = gcc
# CXX = g++
CXXFLAGS = -g -O0 -std=c++20
CXXLIBS = -lpthread -lm -lrt

INCLUDES = -I./ -I./common -I./input -I./system -I./web -I./hal

OBJS = main.o system_process.o web_process.o input_process.o common_timer.o common_mq.o hardware.o
SHARED_LIBS = libcamera.oema.so libcamera.oemb.so
TARGET = nsystem

$(TARGET): $(OBJS) $(SHARED_LIBS)
	$(CXX) -o $(TARGET) $(OBJS) $(CXXLIBS)
	$(MAKE) modules

main.o: main.c
	$(CC) -g -c $< $(INCLUDES)

system_process.o: ./system/system_process.c
	$(CC) -g -c $< $(INCLUDES)

web_process.o: ./web/web_process.c
	$(CC) -g -c $< $(INCLUDES)

input_process.o: ./input/input_process.c
	$(CC) -g -c $< $(INCLUDES)

common_timer.o: ./common/common_timer.c
	$(CC) -g -c $< $(INCLUDES)

common_mq.o: ./common/common_mq.c
	$(CC) -g -c $< $(INCLUDES)

hardware.o: ./hal/hardware.c
	$(CC) -g -c $< $(INCLUDES)

# %.o: %.c
# 	$(CC) -g -c $< -o $@ $(INCLUDES)

.PHONY: libcamera.oema.so
libcamera.oema.so:
	$(CXX) -o libcamera.oema.so -shared -fPIC $(CXXFLAGS) ./hal/oem_a/camera_HAL_oem.cpp ./hal/oem_a/ControlThread.cpp $(INCLUDES)

.PHONY: libcamera.oemb.so
libcamera.oemb.so:
	$(CXX) -o libcamera.oemb.so -shared -fPIC $(CXXFLAGS) ./hal/oem_b/camera_HAL_oem.cpp ./hal/oem_b/ControlThread.cpp $(INCLUDES)

.PHONY: modules
modules:
	$(MAKE) -C ./drivers

.PHONY: nfs
nfs:
	cp $(TARGET) ~/Shared
	$(MAKE) -C ./drivers nfs

.PHONY: clean
clean:
	rm -rf $(TARGET) *.o **/*.o
	$(MAKE) -C ./drivers clean
