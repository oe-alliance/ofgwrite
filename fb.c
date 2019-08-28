#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <memory.h>
#include <string.h>
#include <unistd.h>
#include <linux/kd.h>
#include <sys/ioctl.h>

#include "font.h"

#define TRANS "\x00\x00\x00\x00"
#define BLACK "\x00\x00\x00\x80"
#define WHITE "\xFF\xFF\xFF\xFF"
#define RED   "\x00\x00\xFF\xFF"
#define GREEN "\x00\xFF\x00\xFF"
#define BLUE  "\xFF\x00\x00\xFF"

#define FB_WIDTH 1280
#define FB_HEIGHT 720
#define FB_BPP 32

#ifndef FBIO_BLIT
#define FBIO_SET_MANUAL_BLIT _IOW('F', 0x21, __u8)
#define FBIO_BLIT 0x22
#endif

int g_fbFd = -1;
unsigned char *g_lfb = NULL;
char g_fbDevice[] = "/dev/fb0";
int g_manual_blit = 0;
struct fb_var_screeninfo g_screeninfo_var;
struct fb_fix_screeninfo g_screeninfo_fix;
int g_step = 1;

// box
struct window_t
{
	int x1;		// left upper corner
	int y1;		// left upper corner
	int x2;		// right lower corner
	int y2;		// right lower corner
	int width;
	int height;
} g_window;

// progressbar
struct progressbar
{
	int x1;		// left upper corner (outer dimension)
	int y1;		// left upper corner (outer dimension)
	int x2;		// right lower corner (outer dimension)
	int y2;		// right lower corner (outer dimension)
	int outer_border_width;
	int inner_border_width;
	int width; // inner dimension
	int height; // inner dimension
	int steps;
};

struct progressbar g_pb_overall;
struct progressbar g_pb_step;


void blit()
{
	if (g_manual_blit == 1) {
		if (ioctl(g_fbFd, FBIO_BLIT) < 0)
			perror("FBIO_BLIT");
	}
}

void enableManualBlit()
{
	unsigned char tmp = 1;
	if (ioctl(g_fbFd, FBIO_SET_MANUAL_BLIT, &tmp)<0)
		perror("FBIO_SET_MANUAL_BLIT");
	else
		g_manual_blit = 1;
}

void disableManualBlit()
{
	unsigned char tmp = 0;
	if (ioctl(g_fbFd, FBIO_SET_MANUAL_BLIT, &tmp)<0)
		perror("FBIO_SET_MANUAL_BLIT");
	else
		g_manual_blit = 0;
}

void set_window_dimension()
{
	g_window.width = 640;
	g_window.height = 480;
	g_window.x1 = g_screeninfo_var.xres / 2 - g_window.width / 2;
	g_window.y1 = g_screeninfo_var.yres / 2 - g_window.height / 2;
	g_window.x2 = g_screeninfo_var.xres / 2 + g_window.width / 2;
	g_window.y2 = g_screeninfo_var.yres / 2 + g_window.height / 2;
}

void paint_box(int x1, int y1, int x2, int y2, char* color)
{
	int x,y;
	for (y = y1; y < y2; y++)
		for (x = x1; x < x2; x++)
			memcpy(&g_lfb[(x + g_screeninfo_var.xoffset) * 4 + (y + g_screeninfo_var.yoffset) * g_screeninfo_fix.line_length], color, 4);
}

void init_progressbars(int steps)
{
	// overall progressbar
	g_pb_overall.width = g_window.width * 0.8;
	g_pb_overall.height = g_window.height * 0.1;
	g_pb_overall.outer_border_width = 10;
	g_pb_overall.inner_border_width = 5;
	g_pb_overall.x1 = g_window.x1 + (g_window.width * 0.2 / 2 - g_pb_overall.outer_border_width - g_pb_overall.inner_border_width);
	g_pb_overall.y1 = g_window.y1 + g_window.height * 0.4;
	g_pb_overall.x2 = g_window.x2 - (g_window.width * 0.2 / 2 - g_pb_overall.outer_border_width - g_pb_overall.inner_border_width);
	g_pb_overall.y2 = g_pb_overall.y1 + g_pb_overall.height + 2 * g_pb_overall.outer_border_width + 2* g_pb_overall.inner_border_width;
	g_pb_overall.steps = steps;

	g_pb_step.width = g_window.width * 0.8;
	g_pb_step.height = g_window.height * 0.1;
	g_pb_step.outer_border_width = 10;
	g_pb_step.inner_border_width = 5;
	g_pb_step.x1 = g_window.x1 + (g_window.width * 0.2 / 2 - g_pb_step.outer_border_width - g_pb_step.inner_border_width);
	g_pb_step.y1 = g_window.y1 + g_window.height * 0.65;
	g_pb_step.x2 = g_window.x2 - (g_window.width * 0.2 / 2 - g_pb_step.outer_border_width - g_pb_step.inner_border_width);
	g_pb_step.y2 = g_pb_step.y1 + g_pb_step.height + 2 * g_pb_step.outer_border_width + 2 * g_pb_step.inner_border_width;
}

void paint_progressbars()
{
	// paint white border around overall progressbar
	paint_box(g_pb_overall.x1, g_pb_overall.y1, g_pb_overall.x2, g_pb_overall.y2, WHITE);

	// paint black inner box in overall progressbar
	paint_box(g_pb_overall.x1 + g_pb_overall.outer_border_width
			, g_pb_overall.y1 + g_pb_overall.outer_border_width
			, g_pb_overall.x2 - g_pb_overall.outer_border_width
			, g_pb_overall.y2 - g_pb_overall.outer_border_width
			, BLACK);

	// paint white border around step progressbar
	paint_box(g_pb_step.x1, g_pb_step.y1, g_pb_step.x2, g_pb_step.y2, WHITE);

	// paint black inner box in overall progressbar
	paint_box(g_pb_step.x1 + g_pb_step.outer_border_width
			, g_pb_step.y1 + g_pb_step.outer_border_width
			, g_pb_step.x2 - g_pb_step.outer_border_width
			, g_pb_step.y2 - g_pb_step.outer_border_width
			, BLACK);
}

void close_framebuffer()
{
	// hide all old osd content
	paint_box(0, 0, g_screeninfo_var.xres, g_screeninfo_var.yres, TRANS);

	if (g_lfb)
	{
		msync(g_lfb, g_screeninfo_fix.smem_len, MS_SYNC);
		munmap(g_lfb, g_screeninfo_fix.smem_len);
	}

	if (g_fbFd >= 0)
	{
		disableManualBlit();
		close(g_fbFd);
		g_fbFd = -1;
	}
}

int get_screeninfo()
{
	if (ioctl(g_fbFd, FBIOGET_VSCREENINFO, &g_screeninfo_var) < 0)
	{
		perror("FBIOGET_VSCREENINFO");
		return 0;
	}

	if (ioctl(g_fbFd, FBIOGET_FSCREENINFO, &g_screeninfo_fix) < 0)
	{
		perror("FBIOGET_FSCREENINFO");
		return 0;
	}

	return 1;
}

// Needed by hisilicon boxes to show gui while e2 is running. screeninfo_var.yoffset is not 0 on these boxes
int set_screeninfo()
{
	g_screeninfo_var.yres_virtual = g_screeninfo_var.yres * 2;
	g_screeninfo_var.xoffset = g_screeninfo_var.yoffset = 0;

	if (ioctl(g_fbFd, FBIOPUT_VSCREENINFO, &g_screeninfo_var) < 0)
	{
		perror("Cannot set variable information");
		return 0;
	}

	return 1;
}

int open_framebuffer()
{
	g_fbFd = open(g_fbDevice, O_RDWR);
	if (g_fbFd < 0)
	{
		perror(g_fbDevice);
		goto nolfb;
	}

	enableManualBlit();

	/*my_printf("FB: line_length %d\n", g_screeninfo_fix.line_length);
	my_printf("FB: screeninfo_var.xres %d\n", g_screeninfo_var.xres);
	my_printf("FB: screeninfo_var.yres %d\n", g_screeninfo_var.yres);
	my_printf("FB: screeninfo_var.xres_virt %d\n", g_screeninfo_var.xres_virtual);
	my_printf("FB: screeninfo_var.yres_virt %d\n", g_screeninfo_var.yres_virtual);
	my_printf("FB: screeninfo_var.xoffset %d\n", g_screeninfo_var.xoffset);
	my_printf("FB: screeninfo_var.yoffset %d\n", g_screeninfo_var.yoffset);
	my_printf("FB: screeninfo_var.bits_per_pixel %d\n", g_screeninfo_var.bits_per_pixel);
	my_printf("FB: screeninfo_var.grayscale %d\n", g_screeninfo_var.grayscale);*/

	return 1;

nolfb:
	if (g_fbFd >= 0)
	{
		close(g_fbFd);
		g_fbFd = -1;
	}
	my_printf("framebuffer not available.\n");
	return 0;
}

int mmap_fb()
{
	g_lfb = (unsigned char*)mmap(0, g_screeninfo_fix.smem_len, PROT_WRITE|PROT_READ, MAP_SHARED, g_fbFd, 0);
	if (!g_lfb)
	{
		perror("mmap");
		return 0;
	}
	return 1;
}

int set_fb_resolution()
{
	g_screeninfo_var.xres_virtual = g_screeninfo_var.xres = FB_WIDTH;
	g_screeninfo_var.yres_virtual = g_screeninfo_var.yres = FB_HEIGHT;
	g_screeninfo_var.bits_per_pixel = FB_BPP;
	g_screeninfo_var.xoffset = g_screeninfo_var.yoffset = 0;
	g_screeninfo_var.height = 0;
	g_screeninfo_var.width = 0;

	if (ioctl(g_fbFd, FBIOPUT_VSCREENINFO, &g_screeninfo_var) < 0)
	{
		my_printf("Error: Cannot set variable information");
		return 0;
	}

	if (!get_screeninfo())
	{
		return 0;
	}

	if (g_screeninfo_var.xres != FB_WIDTH || g_screeninfo_var.yres != FB_HEIGHT)
	{
		my_printf("Warning: Cannot change resolution: using %dx%dx%d", g_screeninfo_var.xres, g_screeninfo_var.yres, g_screeninfo_var.bits_per_pixel);
	}

	if (g_screeninfo_var.bits_per_pixel != FB_BPP)
	{
		my_printf("Error: Only 32 bit per pixel supported. Framebuffer currently use %d\n", g_screeninfo_var.bits_per_pixel);
		return 0;
	}

	return 1;
}

void set_step_progress(int percent)
{
	if (g_fbFd == -1)
		return;

	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	int x = g_pb_step.x1 + g_pb_step.outer_border_width + g_pb_step.inner_border_width;
	int y = g_pb_step.y1 + g_pb_step.outer_border_width + g_pb_step.inner_border_width;

	paint_box(x
			, y
			, (int)(x + g_pb_step.width / 100.0 * percent)
			, y + g_pb_step.height
			, WHITE);
	blit();
}

void set_overall_progress(int step)
{
	int percent = (step - 1) * 100 / g_pb_overall.steps;
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	int x = g_pb_overall.x1 + g_pb_overall.outer_border_width + g_pb_overall.inner_border_width;
	int y = g_pb_overall.y1 + g_pb_overall.outer_border_width + g_pb_overall.inner_border_width;

	// paint overall bar
	paint_box(x
			, y
			, (int)(x + g_pb_overall.width / 100.0 * percent)
			, y + g_pb_overall.height
			, WHITE);

	if (percent >= 99)
		return;
	// reset step progressbar
	x = g_pb_step.x1 + g_pb_step.outer_border_width + g_pb_step.inner_border_width;
	y = g_pb_step.y1 + g_pb_step.outer_border_width + g_pb_step.inner_border_width;
	paint_box(x
			, y
			, x + g_pb_step.width
			, y + g_pb_step.height
			, BLACK);
}

void render_char(char ch, int x, int y, char* color, int thick)
{
	const unsigned short* bitmap = font[ch-0x20];

	int h, w, line;
	const unsigned int pos = (y + g_screeninfo_var.yoffset) * g_screeninfo_fix.line_length + (x + g_screeninfo_var.xoffset) * 4;
	for (h = 0; h < CHAR_HEIGHT; h++)
	{
		line = bitmap[h] >> 2;  // ignore 2 lsb bits
		for (w = CHAR_WIDTH - 1; w >= 0; w--)
		{
			if ((line & 0x01) == 0x01)
			{
				memcpy(&g_lfb[pos + (thick + 1) * h * g_screeninfo_fix.line_length + (thick + 1) * w * 4], color, 4);
				if (thick)
				{
					memcpy(&g_lfb[pos + 2 * h * g_screeninfo_fix.line_length + 2 * w * 4 + 4], color, 4);
					memcpy(&g_lfb[pos + (2 * h + 1) * g_screeninfo_fix.line_length + 2 * w * 4], color, 4);
					memcpy(&g_lfb[pos + (2 * h + 1) * g_screeninfo_fix.line_length + 2 * w * 4 + 4], color, 4);
				}
			}

			line = line >> 1;
		}
	}
}

void render_string(char* str, int x, int y, char* color, int thick)
{
	int i;
	for (i = 0; i < strlen(str); i++)
		render_char(str[i], x + i * (CHAR_WIDTH + CHAR_WIDTH * thick), y, color, thick);
}

void set_title(char* str)
{
	if (g_fbFd == -1)
		return;

	// hide text
	paint_box(g_window.x1 + 10
			, g_window.y1 + g_window.height * 0.10
			, g_window.x2
			, g_window.y1 + g_window.height * 0.10 + CHAR_HEIGHT * 2
			, BLACK);

	// display text
	render_string(str
				, g_window.x1 + 10
				, g_window.y1 + g_window.height * 0.10
				, WHITE
				, 1);

	blit();
}

void set_sub_title(char* str)
{
	if (g_fbFd == -1)
		return;

	// hide text
	paint_box(g_window.x1 + 10
			, g_window.y1 + g_window.height * 0.2
			, g_window.x2
			, g_window.y1 + g_window.height * 0.2 + CHAR_HEIGHT
			, BLACK);

	// display text
	render_string(str
				, g_window.x1 + 10
				, g_window.y1 + g_window.height * 0.2
				, WHITE
				, 0);

	blit();
}

void set_overall_text(char* str)
{
	if (g_fbFd == -1)
		return;

	// hide text
	paint_box(g_window.x1 + 10
			, g_window.y1 + g_window.height * 0.35
			, g_window.x2
			, g_window.y1 + g_window.height * 0.35 + CHAR_HEIGHT
			, BLACK);

	// display text
	render_string(str
				, g_window.x1 + 10
				, g_window.y1 + g_window.height * 0.35
				, WHITE
				, 0);

	blit();
}

void set_step_text(char* str)
{
	if (g_fbFd == -1)
		return;

	// hide text
	paint_box(g_window.x1 + 10
			, g_window.y1 + g_window.height * 0.6
			, g_window.x2
			, g_window.y1 + g_window.height * 0.6 + CHAR_HEIGHT
			, BLACK);

	// display text
	render_string(str
				, g_window.x1 + 10
				, g_window.y1 + g_window.height * 0.6
				, WHITE
				, 0);

	blit();
}

void set_step(char* str)
{
	if (g_fbFd == -1)
		return;

	set_step_text(str);
	set_overall_progress(g_step);
	g_step++;
	set_step_progress(0);
}

void set_step_without_incr(char* str)
{
	if (g_fbFd == -1)
		return;

	set_step_text(str);
	set_overall_progress(g_step);
	blit();
}

void set_info_text(char* str)
{
	if (g_fbFd == -1)
		return;

	// display text
	render_string(str
				, g_window.x1 + 10
				, g_window.y1 + g_window.height * 0.90
				, WHITE
				, 0);

	blit();
}

void set_error_text(char* str)
{
	if (g_fbFd == -1)
		return;

	// hide text
	paint_box(g_window.x1 + 10
			, g_window.y1 + g_window.height * 0.9
			, g_window.x2
			, g_window.y2
			, BLACK);

	// display text
	render_string(str
				, g_window.x1 + 10
				, g_window.y1 + g_window.height * 0.90
				, RED
				, 0);

	blit();
}

void set_error_text1(char* str)
{
	if (g_fbFd == -1)
		return;

	// hide text
	paint_box(g_window.x1 + 10
			, g_window.y1 + g_window.height * 0.85
			, g_window.x2
			, g_window.y2
			, BLACK);

	// display text
	render_string(str
				, g_window.x1 + 10
				, g_window.y1 + g_window.height * 0.85
				, RED
				, 0);

	blit();
}

void set_error_text2(char* str)
{
	if (g_fbFd == -1)
		return;

	// hide text
	paint_box(g_window.x1 + 10
			, g_window.y1 + g_window.height * 0.91
			, g_window.x2
			, g_window.y2
			, BLACK);

	// display text
	render_string(str
				, g_window.x1 + 10
				, g_window.y1 + g_window.height * 0.91
				, RED
				, 0);

	blit();
}

int loadBackgroundImage()
{
	int ret;

	// search for background image
	if (access("/etc/enigma2/bootlogo.mvi", R_OK) != 0)
		if (access("/etc/enigma2/backdrop.mvi", R_OK) != 0)
			if (access("/usr/share/bootlogo.mvi", R_OK) != 0)
				if (access("/usr/share/backdrop.mvi", R_OK) != 0)
					return 0;
				else
					ret = system("/usr/bin/showiframe /usr/share/backdrop.mvi");
			else
				ret = system("/usr/bin/showiframe /usr/share/bootlogo.mvi");
		else
			ret = system("/usr/bin/showiframe /etc/enigma2/backdrop.mvi");
	else
		ret = system("/usr/bin/showiframe /etc/enigma2/bootlogo.mvi");

	if (ret != 0)
		return 0;
	return 1;
}

int init_framebuffer(int steps)
{
	if (g_fbFd == -1)
		if (!open_framebuffer())
		{
			return 0;
		}

	if (!get_screeninfo())
	{
		my_printf("Error: Cannot get screen info\n");
		close_framebuffer();
		return 0;
	}

	/*if (   g_screeninfo_var.xres != FB_WIDTH
		|| g_screeninfo_var.yres != FB_HEIGHT
		|| g_screeninfo_var.bits_per_pixel != FB_BPP)
	{
		my_printf("Setting resolution 1280x720 32bit\n");
		if (!set_fb_resolution())
		{
			close_framebuffer();
			return 0;
		}
	}*/

	if (!set_screeninfo())
	{
		my_printf("Error: Cannot set screen info\n");
		close_framebuffer();
		return 0;
	}

	if (!mmap_fb())
	{
		close_framebuffer();
		return 0;
	}

	set_window_dimension();

	// hide all old osd content
	paint_box(0, 0, g_screeninfo_var.xres, g_screeninfo_var.yres, TRANS);

	init_progressbars(steps);

	return 1;
}

int show_main_window(int show_background_image, const char* version)
{
	// hide all old osd content
	paint_box(0, 0, g_screeninfo_var.xres, g_screeninfo_var.yres, TRANS);

	// set background image
	if (show_background_image && !loadBackgroundImage())
	{ // if image not present paint black background
		my_printf("Error: Found no background image, or image is unusable\n");
		paint_box(0, 0, g_screeninfo_var.xres, g_screeninfo_var.yres, BLACK);
	}

	// paint window
	paint_box(g_window.x1, g_window.y1, g_window.x2, g_window.y2, BLACK);
	paint_progressbars();

	set_title("ofgwrite Flashing Tool");
	char version_string[60];
	strcpy(version_string, "written by Betacentauri  v.");
	strcat(version_string, version);
	set_sub_title(version_string);
	return 1;
}
