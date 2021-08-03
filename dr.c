#include <gphoto2.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/joystick.h>
#include <netinet/tcp.h>
#include <pthread.h>

#define IP "192.168.1.1"
#define PTPPORT "15740"
#define CTRLPORT 8888

#define CTRLDELAY 20000

#define JOYSTICK "/dev/input/js0"
#define MAX_AXIES 16
#define MAX_BUTTONS 20
#define FY_AXIS 1
#define FX_AXIS 0
#define RY_AXIS 4
#define RX_AXIS 3
#define FY_INVERT
//#define FX_INVERT
#define RY_INVERT
//#define RX_INVERT
#define AXIS_CONV(x) (((x)/256)+128)
#define AXIS_CONVI(x) (((-(x))/256)+128)

void ctx_error_func(GPContext *context, const char *str, void *data)
{
	fprintf(stderr, "<CTX ERR> %s\n",str);
	fflush(stderr);
}

void ctx_status_func(GPContext *context, const char *str, void *data)
{
	fprintf(stderr, "%s\n", str);
	fflush(stderr);
}

typedef struct {
	GPContext *context;
	Camera *camera;
} gp_context;

int init_gp(gp_context *ctx)
{
	gp_camera_new(&ctx->camera);

	ctx->context = gp_context_new();

	gp_context_set_error_func(ctx->context, ctx_error_func, NULL);
	gp_context_set_status_func(ctx->context, ctx_status_func, NULL);

	GPPortInfoList *list;

	if (gp_port_info_list_new(&list) < GP_OK) {
		printf("Can't create list of cameras.\n");
		gp_camera_free(ctx->camera);
		return -1;
	}

	if (gp_port_info_list_load(list) < GP_OK) {
		printf("Can't get list of cameras.\n");
		gp_port_info_list_free(list);
		gp_camera_free(ctx->camera);
		return -1;
	}

	if (gp_port_info_list_count(list) < GP_OK) {
		printf("No cameras.\n");
		gp_port_info_list_free(list);
		gp_camera_free(ctx->camera);
		return -1;
	}

	int port;

	if ((port = gp_port_info_list_lookup_path(list, "ptpip:"IP":"PTPPORT)) == GP_ERROR_UNKNOWN_PORT) {
		printf("Can't find camera.\n");
		gp_port_info_list_free(list);
		gp_camera_free(ctx->camera);
		return -1;
	}

	GPPortInfo info;

	if (gp_port_info_list_get_info(list, port, &info) < GP_OK) {
		printf("Can't port.\n");
		gp_port_info_list_free(list);
		gp_camera_free(ctx->camera);
		return -1;
	}

	if (gp_camera_set_port_info(ctx->camera, info) < GP_OK) {
		printf("Can't set camera port.\n");
		gp_port_info_list_free(list);
		gp_camera_free(ctx->camera);
		return -1;
	}

	gp_port_info_list_free(list);

	if (gp_camera_init(ctx->camera, ctx->context) < GP_OK) {
		printf("Can't connect to camera.\n");
		gp_camera_free(ctx->camera);
		return -1;
	}
	return 0;
}

void free_gp(gp_context *ctx)
{
	gp_camera_exit(ctx->camera, ctx->context);
	gp_camera_free(ctx->camera);
}

typedef struct {
	int fd;
	short buttons[MAX_BUTTONS];
	short axies[MAX_AXIES];
} js_context;

int init_joystick(js_context *ctx, const char *dev)
{
	memset(ctx,0,sizeof(js_context));

	ctx->fd = open(dev, O_RDONLY|O_NONBLOCK);

	if (ctx->fd == -1) {
		printf("Couldn't open joystick\n");
		return -1;
	}

	return 0;
}

int update_joystick(js_context *ctx)
{
	int nbev = 0;
	struct js_event e;
	while (read (ctx->fd, &e, sizeof(e)) > 0) {
		switch (e.type)
		{
			case JS_EVENT_BUTTON:
				if (e.number >= MAX_BUTTONS || !e.value)
					break;
				ctx->buttons[e.number] += 1;
				break;
			case JS_EVENT_AXIS:
				if (e.number >= MAX_AXIES)
					break;
				ctx->axies[e.number] = e.value;
				break;
			default:
				break;
		}
		nbev++;
	}
	return nbev;
}

void free_joystick(js_context *ctx)
{
	close(ctx->fd);
}

/*
      Packet example:
      CC80808080000033

      Structure:
      CC Fixed
      80 this.c Fly FB
      80 this.b Fly LR
      80 this.d UD
      80 this.e Turn LR
      00 this.h Flags
      00 a() Checksum of 4 above bytes
      33 Fixed

      Mov values:
      128 -> 0

      Flags bits: 
      0 a(); -> Toggle Takeoff ?
      1 b(); -> Toggle Landing ?
      2 c(); -> Toggle Headless ?
      3 d(); -> Toggle Obstacle Avoidance ?
      4 e(); -> Take 360 picture or smth ? idk
      5 f(); -> Restart AP ? idk after pwd change


      Calibration:
      hold for 3.5s
      c = 255;
      b = 244;
      d = 0;
      e = 244;

*/

typedef struct {
	pthread_t thread;
	int sock;
	uint8_t alive;
	pthread_mutex_t lock;
	uint8_t fy; // Fly FB
	uint8_t fx; // Fly LR
	uint8_t ry; // Up Down
	uint8_t rx; // Turn LR
	uint8_t flags;
} ctrl_context;

void ctrl_thread(ctrl_context *ctx)
{
	uint8_t data[8];
	data[0] = 0xCC;
	data[7] = 0x33;
	while (ctx->alive) {
		pthread_mutex_lock(&ctx->lock);
		data[1] = ctx->fy;
		data[2] = ctx->fx;
		data[3] = ctx->ry;
		data[4] = ctx->rx;
		data[5] = ctx->flags;
		pthread_mutex_unlock(&ctx->lock);
		int chksum = data[1];
		for (int i=2;i<6;i++)
			chksum ^= data[i];
		data[6] = chksum;
		send(ctx->sock, &data, 8, 0);
		usleep(CTRLDELAY);
	}
}

int init_ctrl(ctrl_context *ctx)
{
	if ((ctx->sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("Can't create socket\n");
		return -1;
	}

	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(CTRLPORT);

	if (inet_pton(AF_INET, IP, &addr.sin_addr) <= 0) {
		printf("Invalid address\n");
		return -1;
	}

	if (connect(ctx->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("Can't connect to ip\n");
		return -1;
	}

	int flag = 1;

	if (setsockopt(ctx->sock, IPPROTO_TCP, TCP_NODELAY, (void*)&flag, sizeof(int)) < 0) {
		printf("Can't set TCP_NODELAY\n");
		close(ctx->sock);
		return -1;	
	}


	if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
		close(ctx->sock);
		printf("Can't create mutex\n");
		return -1;
	}

	ctx->alive = 1;
	ctx->fy = 0x80;
	ctx->fx = 0x80;
	ctx->ry = 0x80;
	ctx->rx = 0x80;
	ctx->flags = 0;

	if (pthread_create(&ctx->thread, NULL, (void *(*) (void *))ctrl_thread, ctx) != 0) {
		pthread_mutex_destroy(&ctx->lock);
		close(ctx->sock);
		printf("Can't create thread\n");
		return -1;	
	}

	return 0;
}

void free_ctrl(ctrl_context *ctx)
{
	ctx->alive = 0;
	pthread_join(ctx->thread, NULL);
	close(ctx->sock);
}

void calibrate_ctrl(ctrl_context *ctx)
{
	printf("Starting calibration...\n");
	pthread_mutex_lock(&ctx->lock);
	ctx->fy = 0xFF;
	ctx->fx = 0xF4;
	ctx->ry = 0;
	ctx->rx = 0xF4;
	ctx->flags = 0;
	pthread_mutex_unlock(&ctx->lock);
	usleep(3500000);
	pthread_mutex_lock(&ctx->lock);
	ctx->fy = 0x80;
	ctx->fx = 0x80;
	ctx->ry = 0x80;
	ctx->rx = 0x80;
	pthread_mutex_unlock(&ctx->lock);
	printf("Press enter when the led stops blinking\n");
	getchar();
}

int main(int argc, char const *argv[])
{
	gp_context gp;

	/*if (init_gp(&gp) < 0) {
		printf("Couldn't set up GP\n");
		return -1;
	}*/

	js_context js;

	if (init_joystick(&js,JOYSTICK) < 0) {
		printf("Couldn't setup joystick\n");
		//free_gp(&gp);
		return -1;
	}

	ctrl_context ctrl;

	if (init_ctrl(&ctrl) < 0) {
		printf("Couldn't setup control port\n");
		free_joystick(&js);
		//free_gp(&gp);
		return -1;
	}

	calibrate_ctrl(&ctrl);

	while (1) {
		if (update_joystick(&js)) {
			pthread_mutex_lock(&ctrl.lock);
#ifdef FY_INVERT
			ctrl.fy = AXIS_CONVI(js.axies[FY_AXIS]);
#else
			ctrl.fy = AXIS_CONV(js.axies[FY_AXIS]);
#endif
#ifdef FX_INVERT
			ctrl.fx = AXIS_CONVI(js.axies[FX_AXIS]);
#else
			ctrl.fx = AXIS_CONV(js.axies[FX_AXIS]);
#endif
#ifdef RY_INVERT
			ctrl.ry = AXIS_CONVI(js.axies[RY_AXIS]);
#else
			ctrl.ry = AXIS_CONV(js.axies[RY_AXIS]);
#endif
#ifdef RX_INVERT
			ctrl.rx = AXIS_CONVI(js.axies[RX_AXIS]);
#else
			ctrl.rx = AXIS_CONV(js.axies[RX_AXIS]);
#endif
			for (int i=0;i<6;i++) {
				if (js.buttons[i]) {
					ctrl.flags ^= 1UL << i;
					js.buttons[i] = 0;
				}
			}
			pthread_mutex_unlock(&ctrl.lock);
			printf("fy(FB):%x fx(LF):%x ry(UD):%x rx(LR):%x f:%x\n", ctrl.fy,ctrl.fx,ctrl.ry,ctrl.rx,ctrl.flags);
		}
		usleep(CTRLDELAY);
	}

	free_ctrl(&ctrl);

	free_joystick(&js);

	//free_gp(&gp);

	return 0;
}