CFLAGS := -g $(shell pkg-config --cflags libgphoto2)
LDFLAGS := -pthread $(shell pkg-config --libs libgphoto2)

all: dr

dr:
	gcc dr.c $(CFLAGS) $(LDFLAGS) -o dr

clean:
	rm dr