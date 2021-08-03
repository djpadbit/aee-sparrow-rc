# AEE Sparrow 360 RC

It's a small C program to be able to control the drone on a pc with a controller.

## Usage

It requires `libgphoto2` (but for now the ptp functionality is disabled because it doesn't work)

You build it with a simple
```
make
```
And run it with
```
./dr
```

The program has my axis bindings for my controller, you may have to tweak the axis defines in `dr.c`

To takeoff you use the first button of your controller and to land the second, the next 4 buttons are also mapped so don't touch all of the buttons on the controller. To see what buttons and axies are what i recommand you use `jstest-gtk`

## Video feed

To see the video feed, you can use mpv like so
```
mpv "rtsp://192.168.1.1/H264?W=1920&H=1080&BR=1200000&FPS=15&"
```
You can reduce the latency by usind this command
```
mpv --no-cache --untimed --no-demuxer-thread --vd-lavc-threads=1 "rtsp://192.168.1.1/H264?W=1920&H=1080&BR=1200000&FPS=15&"
```