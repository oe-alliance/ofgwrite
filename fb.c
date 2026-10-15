#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "lvgl.h"
#include "i18n.h"
#include "ofgwrite.h"

#ifndef FBIO_BLIT
#define FBIO_SET_MANUAL_BLIT _IOW('F', 0x21, __u8)
#define FBIO_BLIT 0x22
#endif

LV_FONT_DECLARE(ofg_font_14)
LV_FONT_DECLARE(ofg_font_16)
LV_FONT_DECLARE(ofg_font_20)
LV_FONT_DECLARE(ofg_font_24)
LV_FONT_DECLARE(ofg_digits_44)
LV_FONT_DECLARE(ofg_digits_66)

/* The colors of the Open Recovery Manager. */
#define COLOR_GROUND 0x0b1016
#define COLOR_SURFACE 0x121a23
#define COLOR_SUNKEN 0x070b10
#define COLOR_TRACK 0x1b2533
#define COLOR_TEXT 0xe8edf2
#define COLOR_MUTED 0x7d8fa1
#define COLOR_FOCUS 0x2b7fd9
#define COLOR_FOCUS_LIGHT 0x5b9de6
#define COLOR_WARN 0xf2b84b
#define COLOR_RED 0xef5b5b
#define COLOR_RED_GROUND 0x2a1214
#define COLOR_RED_LINE 0xa33a3a
#define COLOR_OK 0x4cc26a
#define COLOR_OK_GROUND 0x10261a
#define COLOR_OK_LINE 0x2f7a45

#define TRANS 0x00000000u
#define BLACK 0x80000000u  /* Half transparent, behind the window without a background image. */
#define DIM 0x05080cu  /* --dim: the screen around the window, a quarter of it stays. */
#define DIM_KEEP 64
#define WINDOW_OPA 230  /* The picture below shines through the window a little. */

#define DESIGN_HEIGHT 360  /* Sizes are for a window of 480 x 360. */
#define DRAW_ROWS 60
#define REDRAW_MS 100  /* Progress alone redraws at most this often. */
#define ON_TOP_MS 1000  /* Over a running Enigma2 the whole window again at least this often. */

enum card_kind { CARD_NONE, CARD_ERROR, CARD_SUCCESS };

static int fb_fd = -1;
static unsigned char *fb_mem;
static size_t fb_len;
static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static int manual_blit;

static int dim;  /* --dim */
static int checking;  /* ofgwrite -n writes nothing. */
static const char *language;  /* --lang */
static int win_x, win_y, win_w, win_h;
static uint32_t *backing;  /* The pixels below the window, the window is blended over them. */
static uint32_t *screen_copy;  /* The screen before over a running program, to give it back. */
static struct fb_var_screeninfo before;  /* The mode and the page shown before ofgwrite. */
static int alone;  /* Enigma2 is stopped, the screen belongs to ofgwrite. */

static lv_display_t *display;
static uint8_t *draw_buffer;
static lv_obj_t *window;
static lv_obj_t *body;
static lv_obj_t *percent_label;
static lv_obj_t *overall_fill;
static lv_obj_t *step_label;
static lv_obj_t *step_fill;
static lv_obj_t *detail_label;
static lv_obj_t *detail_percent;
static lv_obj_t *info_label;
static lv_obj_t *card;
static lv_obj_t *card_title;
static lv_obj_t *card_text;
static lv_obj_t *card_advice;
static lv_obj_t *footer_left;
static lv_obj_t *footer_right;
static char version_text[32];

static int steps_total = 1;
static int step_now;  /* Of set_step, 1 for the first. */
static int step_percent;
static int overall_percent;
static time_t started;
static uint32_t drawn_at;
static uint32_t full_at;
static enum card_kind card_kind;
static enum fb_after card_after;
static char countdown_text[96];

static uint32_t milliseconds(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static int px(int value)
{
	return value * win_h / DESIGN_HEIGHT;
}

/* The fonts by the height of the window: 1 from a screen of 1080 lines on. */
static int large(void)
{
	return win_h >= 500;
}

/* Framebuffer */

static uint32_t *pixel(int x, int y)
{
	size_t offset = (size_t)(y + (int)var.yoffset) * fix.line_length + (size_t)(x + (int)var.xoffset) * 4;
	return offset + 4 <= fb_len ? (uint32_t *)(fb_mem + offset) : NULL;
}

static void blit(void)
{
	if (manual_blit && ioctl(fb_fd, FBIO_BLIT) < 0)
		perror("FBIO_BLIT");
}

static void set_manual_blit(unsigned char on)
{
	if (ioctl(fb_fd, FBIO_SET_MANUAL_BLIT, &on) < 0)
		perror("FBIO_SET_MANUAL_BLIT");
	else
		manual_blit = on;
}

static int pages(void)
{
	unsigned int virtual_lines = var.yres_virtual;
	if (!fb_len || !fix.line_length || !var.yres)
		return 1;
	if (virtual_lines > fb_len / fix.line_length)
		virtual_lines = (unsigned int)(fb_len / fix.line_length);
	return virtual_lines / var.yres > 0 ? (int)(virtual_lines / var.yres) : 1;
}

static void paint(int x1, int y1, int x2, int y2, uint32_t color)
{
	for (int y = y1; y < y2; ++y)
		for (int x = x1; x < x2; ++x) {
			uint32_t *p = pixel(x, y);
			if (p)
				*p = color;
		}
}

static void clear_all_pages(uint32_t color)
{
	unsigned int yoffset = var.yoffset;
	for (int page = 0; page < pages(); ++page) {
		var.yoffset = (unsigned int)page * var.yres;
		paint(0, 0, (int)var.xres, (int)var.yres, color);
	}
	var.yoffset = yoffset;
}

/* Needed by some boxes to show the window while Enigma2 is running: triple, double or single buffering.
 * When the driver refuses all, the current mode stays. */
static int set_screeninfo(void)
{
	static const unsigned int factors[] = {3, 2, 1};
	struct fb_var_screeninfo original = var;
	if (!original.yres)
		return 1;
	for (int i = 0; i < 3; ++i) {
		var = original;
		var.yres_virtual = original.yres * factors[i];
		var.xoffset = 0;
		var.yoffset = 0;
		var.activate = FB_ACTIVATE_ALL;
		if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &var) == 0)
			break;
	}
	return ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) == 0 && ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) == 0;
}

static int map_framebuffer(void)
{
	size_t length = fix.smem_len;  /* Some drivers report 0. */
	if (!length)
		length = (size_t)fix.line_length * var.yres_virtual;
	if (!length)
		length = (size_t)var.xres * var.yres_virtual * (var.bits_per_pixel / 8);
	if (!length) {
		my_printf("Error: framebuffer mapping size is 0\n");
		return 0;
	}
	fb_mem = mmap(NULL, length, PROT_WRITE | PROT_READ, MAP_SHARED, fb_fd, 0);
	if (fb_mem == MAP_FAILED) {
		fb_mem = NULL;
		perror("mmap");
		return 0;
	}
	fb_len = length;
	if (var.yoffset > (unsigned int)(pages() - 1) * var.yres)
		var.yoffset = 0;
	return 1;
}

/* LVGL */

/* The window over the pixels below it, so its round corners and the area outside stay. */
static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels)
{
	const uint32_t *source = (const uint32_t *)pixels;
	for (int32_t y = area->y1; y <= area->y2; ++y)
		for (int32_t x = area->x1; x <= area->x2; ++x, ++source) {
			uint32_t *target = pixel(win_x + x, win_y + y);
			uint32_t below = backing[y * win_w + x];
			uint32_t alpha = *source >> 24;
			uint32_t mixed = 0;
			if (!target)
				continue;
			if (alpha == 255 || !below)
				mixed = alpha ? *source : below;
			else if (!alpha)
				mixed = below;
			else {
				uint32_t below_alpha = below >> 24;
				for (int shift = 0; shift < 24; shift += 8)
					mixed |= ((((*source >> shift) & 0xff) * alpha + ((below >> shift) & 0xff) * (255 - alpha)) / 255)
						<< shift;
				mixed |= (alpha + below_alpha * (255 - alpha) / 255) << 24;
			}
			*target = mixed;
		}
	if (lv_display_flush_is_last(disp))
		blit();
	lv_display_flush_ready(disp);
}

static lv_obj_t *box(lv_obj_t *parent)
{
	lv_obj_t *obj = lv_obj_create(parent);
	lv_obj_remove_style_all(obj);
	lv_obj_set_scrollable(obj, false);
	return obj;
}

static void fill(lv_obj_t *obj, uint32_t color)
{
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
}

static void glass(lv_obj_t *obj, uint32_t color)
{
	lv_obj_set_style_bg_opa(obj, WINDOW_OPA, 0);
	lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
	lv_obj_t *obj = lv_label_create(parent);
	lv_label_set_text(obj, "");
	lv_obj_set_style_text_font(obj, font, 0);
	lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
	return obj;
}

/* Digits, the version and the time stay as they are in a language from the right. */
static lv_obj_t *ltr(lv_obj_t *obj)
{
	lv_obj_set_style_base_dir(obj, LV_BASE_DIR_LTR, 0);
	return obj;
}

static lv_obj_t *wrapped(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
	lv_obj_t *obj = label(parent, font, color);
	lv_obj_set_width(obj, LV_PCT(100));
	lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_WRAP);
	return obj;
}

static lv_obj_t *bar(lv_obj_t *parent, int width_pct, int height, uint32_t color)
{
	lv_obj_t *track = box(parent);
	lv_obj_t *part = box(track);
	lv_obj_set_size(track, LV_PCT(width_pct), height);
	fill(track, COLOR_TRACK);
	lv_obj_set_style_radius(track, height / 2, 0);
	lv_obj_set_style_clip_corner(track, true, 0);
	lv_obj_set_flex_flow(track, LV_FLEX_FLOW_ROW);  /* The part fills from the start, from the right in Arabic. */
	lv_obj_set_size(part, 0, LV_PCT(100));
	fill(part, color);
	lv_obj_set_style_radius(part, height / 2, 0);
	return part;
}

static lv_obj_t *row(lv_obj_t *parent, int height, uint32_t color)
{
	lv_obj_t *obj = box(parent);
	lv_obj_set_size(obj, LV_PCT(100), height);
	glass(obj, color);
	lv_obj_set_style_pad_hor(obj, px(22), 0);
	lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	return obj;
}

static void build(void)
{
	const lv_font_t *text = large() ? &ofg_font_24 : &ofg_font_16;
	const lv_font_t *small = large() ? &ofg_font_20 : &ofg_font_14;
	lv_obj_t *screen = lv_display_get_screen_active(display);
	lv_obj_t *header;
	lv_obj_t *footer;

	lv_obj_clean(screen);
	lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
	window = box(screen);
	lv_obj_set_size(window, win_w, win_h);  /* Its parts are translucent each, so they do not add up. */
	lv_obj_set_style_radius(window, px(14), 0);
	lv_obj_set_style_clip_corner(window, true, 0);
	lv_obj_set_flex_flow(window, LV_FLEX_FLOW_COLUMN);
	if (i18n_rtl())
		lv_obj_set_style_base_dir(window, LV_BASE_DIR_RTL, 0);

	header = row(window, px(44), COLOR_SURFACE);
	lv_label_set_text(label(header, text, COLOR_TEXT), "ofgwrite - written by Betacentauri");
	lv_label_set_text(ltr(label(header, small, COLOR_MUTED)), version_text);

	body = box(window);
	lv_obj_set_width(body, LV_PCT(100));
	lv_obj_set_flex_grow(body, 1);
	glass(body, COLOR_GROUND);
	lv_obj_set_style_pad_hor(body, px(40), 0);
	lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	percent_label = ltr(label(body, large() ? &ofg_digits_66 : &ofg_digits_44, COLOR_TEXT));
	overall_fill = bar(body, 100, px(14), COLOR_FOCUS);
	lv_obj_set_style_margin_top(lv_obj_get_parent(overall_fill), px(14), 0);
	lv_obj_set_style_margin_bottom(lv_obj_get_parent(overall_fill), px(22), 0);
	step_label = label(body, text, COLOR_TEXT);
	lv_obj_set_width(step_label, LV_PCT(100));
	lv_obj_set_style_text_align(step_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(step_label, LV_LABEL_LONG_MODE_DOTS);
	step_fill = bar(body, 72, px(6), COLOR_FOCUS_LIGHT);
	lv_obj_set_style_margin_ver(lv_obj_get_parent(step_fill), px(10), 0);
	{  /* "Step 4 of 6 · 62 %" in parts, so a language from the right keeps the numbers in place. */
		lv_obj_t *detail = box(body);
		lv_obj_set_size(detail, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
		lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_ROW);
		lv_obj_set_style_pad_column(detail, px(6), 0);
		detail_label = label(detail, small, COLOR_MUTED);
		lv_label_set_text(label(detail, small, COLOR_MUTED), "\xC2\xB7");
		detail_percent = ltr(label(detail, small, COLOR_MUTED));
	}
	info_label = wrapped(body, small, COLOR_WARN);
	lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_margin_top(info_label, px(10), 0);
	lv_obj_set_hidden(info_label, true);

	card = box(window);
	lv_obj_set_width(card, LV_PCT(100));
	lv_obj_set_flex_grow(card, 1);
	glass(card, COLOR_GROUND);
	lv_obj_set_style_pad_all(card, px(16), 0);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
	lv_obj_set_hidden(card, true);
	{
		lv_obj_t *inner = box(card);
		lv_obj_set_size(inner, LV_PCT(100), LV_SIZE_CONTENT);
		lv_obj_set_style_border_width(inner, 1, 0);
		lv_obj_set_style_radius(inner, px(10), 0);
		lv_obj_set_style_pad_all(inner, px(16), 0);
		lv_obj_set_style_pad_row(inner, px(8), 0);
		lv_obj_set_flex_flow(inner, LV_FLEX_FLOW_COLUMN);
		card_title = wrapped(inner, small, COLOR_RED);
		card_text = wrapped(inner, text, COLOR_TEXT);
		card_advice = wrapped(inner, small, COLOR_MUTED);
	}

	footer = row(window, px(40), COLOR_SUNKEN);
	footer_left = label(footer, small, COLOR_WARN);
	footer_right = ltr(label(footer, small, COLOR_MUTED));

	if (dim && !alone) {  /* Like a dialog over the program that started ofgwrite; over header and footer. */
		lv_obj_t *frame = box(screen);
		lv_obj_set_size(frame, win_w, win_h);
		lv_obj_set_style_radius(frame, px(14), 0);
		lv_obj_set_style_border_width(frame, px(2) > 1 ? px(2) : 2, 0);
		lv_obj_set_style_border_color(frame, lv_color_hex(COLOR_FOCUS), 0);
	}
}

static void show_card(void)
{
	lv_obj_t *inner = lv_obj_get_child(card, 0);
	int error = card_kind == CARD_ERROR;
	if (card_kind == CARD_NONE) {
		lv_obj_set_hidden(card, true);
		lv_obj_set_hidden(body, false);
		return;
	}
	lv_obj_set_hidden(body, true);
	lv_obj_set_hidden(card, false);
	fill(inner, error ? COLOR_RED_GROUND : COLOR_OK_GROUND);
	lv_obj_set_style_border_color(inner, lv_color_hex(error ? COLOR_RED_LINE : COLOR_OK_LINE), 0);
	lv_obj_set_style_text_color(card_title, lv_color_hex(error ? COLOR_RED : COLOR_OK), 0);
	lv_label_set_text(card_title, error ? tr(N_("Flashing failed")) : tr(N_("Done")));
	lv_obj_set_style_text_color(footer_left, lv_color_hex(error ? COLOR_RED : COLOR_OK), 0);
}

static void update_labels(void)
{
	char text[128];
	time_t seconds = started ? time(NULL) - started : 0;
	int step = step_now > 0 ? step_now : 1;
	int percent = steps_total > 0 ? ((step - 1) * 100 + step_percent) / steps_total : 0;

	if (percent > 99 && card_kind != CARD_SUCCESS)
		percent = 99;  /* Until it is done; a step more than counted stays there. */
	if (percent > overall_percent || card_kind == CARD_SUCCESS)
		overall_percent = card_kind == CARD_SUCCESS ? 100 : percent;
	snprintf(text, sizeof(text), "%d %%", overall_percent);
	lv_label_set_text(percent_label, text);
	lv_obj_set_width(overall_fill, LV_PCT(overall_percent));
	lv_obj_set_width(step_fill, LV_PCT(step_percent));
	snprintf(text, sizeof(text), tr(N_("Step %d of %d")), step, step > steps_total ? step : steps_total);
	lv_label_set_text(detail_label, text);
	snprintf(text, sizeof(text), "%d %%", step_percent);
	lv_label_set_text(detail_percent, text);
	if (card_kind == CARD_NONE && checking) {
		lv_obj_set_style_text_color(footer_left, lv_color_hex(COLOR_MUTED), 0);
		lv_label_set_text(footer_left, tr(N_("Check only, nothing is written")));
	} else if (card_kind == CARD_NONE) {
		snprintf(text, sizeof(text), "\xE2\x9A\xA0 %s", tr(N_("Do not switch off the receiver")));
		lv_label_set_text(footer_left, text);
	} else
		lv_label_set_text(footer_left, countdown_text);
	snprintf(text, sizeof(text), "%ld:%02ld", (long)(seconds / 60), (long)(seconds % 60));
	lv_label_set_text(footer_right, text);
}

/* Draws the window, progress alone at most every REDRAW_MS. */
static void draw(int force)
{
	uint32_t now = milliseconds();
	if (!display || !window)
		return;
	if (!force && now - drawn_at < REDRAW_MS)
		return;
	update_labels();
	if (!alone && now - full_at >= ON_TOP_MS) {  /* Enigma2 may have drawn over it. */
		lv_obj_invalidate(window);
		full_at = now;
	}
	if (fb_mem)
		lv_refr_now(display);
	drawn_at = now;
}

static int setup_display(void)
{
	static int initialized;
	if (!initialized) {
		lv_init();
		lv_tick_set_cb(milliseconds);
		initialized = 1;
	}
	if (display && lv_display_get_horizontal_resolution(display) == win_w &&
		lv_display_get_vertical_resolution(display) == win_h)
		return 1;
	if (display) {
		lv_display_delete(display);
		free(draw_buffer);
		window = NULL;
	}
	draw_buffer = malloc((size_t)win_w * DRAW_ROWS * 4);
	display = lv_display_create(win_w, win_h);
	if (!draw_buffer || !display)
		return 0;
	lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
	lv_display_set_buffers(display, draw_buffer, NULL, (uint32_t)win_w * DRAW_ROWS * 4, LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(display, flush);
	return 1;
}

/* The middle of the screen, half its height. */
static void place_window(void)
{
	win_h = (int)var.yres / 2;
	win_w = win_h * 4 / 3;
	if (win_w > (int)var.xres) {
		win_w = (int)var.xres;
		win_h = win_w * 3 / 4;
	}
	win_x = ((int)var.xres - win_w) / 2;
	win_y = ((int)var.yres - win_h) / 2;
}

/* API of ofgwrite */

void set_dim(int on)
{
	dim = on;
}

void set_checking(int on)
{
	checking = on;
}

/* Over a running program: keeps the screen to give it back, with --dim darkens it around the window,
 * else clears it. */
static void save_screen(void)
{
	size_t row = (size_t)var.xres * 4;
	uint32_t *dimmed = malloc(row);
	uint8_t darker[3][256];  /* Per channel, so a row is quick and the screen darkens at once. */
	free(screen_copy);
	screen_copy = malloc(row * var.yres);
	if (!screen_copy || !dimmed) {
		free(dimmed);
		return;
	}
	for (int channel = 0; channel < 3; ++channel)
		for (int value = 0; value < 256; ++value)
			darker[channel][value] = (uint8_t)((value * DIM_KEEP + ((DIM >> (channel * 8)) & 0xff) * (255 - DIM_KEEP)) / 255);
	for (int y = 0; y < (int)var.yres; ++y) {  /* Whole rows, the framebuffer is slow to read pixel by pixel. */
		uint32_t *copy = screen_copy + (size_t)y * var.xres;
		size_t shown = (size_t)(y + (int)before.yoffset) * fix.line_length + (size_t)var.xoffset * 4;
		uint32_t *target = pixel(0, y);
		if (shown + row <= fb_len)
			memcpy(copy, fb_mem + shown, row);
		else
			memset(copy, 0, row);
		if (!dim || !target || (uint8_t *)target + row > fb_mem + fb_len)
			continue;
		for (int x = 0; x < (int)var.xres; ++x) {
			uint32_t color = copy[x];
			dimmed[x] = (color & 0xff000000u) | (uint32_t)darker[2][(color >> 16) & 0xff] << 16 |
				(uint32_t)darker[1][(color >> 8) & 0xff] << 8 | darker[0][color & 0xff];
		}
		memcpy(target, dimmed, row);
	}
	free(dimmed);
	if (!dim)
		clear_all_pages(TRANS);
}

void set_language(const char *lang)
{
	language = lang;
}

int init_framebuffer(int steps)
{
	static int translated;
	if (!translated) {  /* Still with the settings of Enigma2, before pivot_root. */
		i18n_init(language);
		translated = 1;
	}
	if (!started)
		started = time(NULL);
	steps_total = steps > 0 ? steps : 1;
	if (fb_fd >= 0)
		return 1;
	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd < 0) {
		perror("/dev/fb0");
		my_printf("framebuffer not available.\n");
		return 0;
	}
	set_manual_blit(1);
	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0 || ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		my_printf("Error: Cannot get screen info\n");
		close_framebuffer();
		return 0;
	}
	before = var;  /* The program below keeps its page, set_screeninfo() shows the first one. */
	if (!set_screeninfo()) {
		my_printf("Error: Cannot set screen info\n");
		close_framebuffer();
		return 0;
	}
	if (var.bits_per_pixel != 32) {
		my_printf("Error: Only 32 bit per pixel supported. Framebuffer uses %d\n", var.bits_per_pixel);
		close_framebuffer();
		return 0;
	}
	if (!map_framebuffer()) {
		close_framebuffer();
		return 0;
	}
	return 1;
}

int loadBackgroundImage()
{
	static const char *const images[] = {"/etc/enigma2/bootlogo.mvi", "/etc/enigma2/backdrop.mvi",
		"/usr/share/bootlogo.mvi", "/usr/share/backdrop.mvi"};
	char command[64];
	for (int i = 0; i < 4; ++i)
		if (access(images[i], R_OK) == 0) {
			snprintf(command, sizeof(command), "/usr/bin/showiframe %s", images[i]);
			return system(command) == 0;
		}
	return 0;
}

/* alone: Enigma2 is stopped, the screen is cleared and gets the boot logo. Else the window lies over the
 * screen of the running Enigma2 or of the program that started ofgwrite. */
int show_main_window(int alone_now, const char *version)
{
	if (fb_fd < 0 || !fb_mem)
		return 0;
	alone = alone_now;
	snprintf(version_text, sizeof(version_text), "v%s", version);
	place_window();
	free(backing);
	backing = calloc((size_t)win_w * win_h, 4);
	if (!backing || !setup_display())
		return 0;
	if (alone) {
		clear_all_pages(TRANS);
		if (!loadBackgroundImage()) {
			my_printf("Error: Found no background image, or image is unusable\n");
			paint(0, 0, (int)var.xres, (int)var.yres, BLACK);
		}
	} else if (!screen_copy)
		save_screen();
	for (int y = 0; y < win_h; ++y)
		for (int x = 0; x < win_w; ++x) {
			uint32_t *p = pixel(win_x + x, win_y + y);
			backing[y * win_w + x] = p ? *p : 0;
		}
	build();
	show_card();
	lv_obj_invalidate(window);
	draw(1);
	return 1;
}

void close_framebuffer()
{
	if (fb_mem) {
		if (alone || !backing)
			clear_all_pages(TRANS);
		else if (screen_copy) {  /* Gives the screen back as it was: its mode, its page and in whole rows. */
			size_t row = (size_t)var.xres * 4;
			struct fb_var_screeninfo back = before;
			back.activate = FB_ACTIVATE_ALL;
			if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &back) < 0 || ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0)
				perror("FBIOPUT_VSCREENINFO");
			for (int y = 0; y < (int)var.yres; ++y) {
				uint8_t *p = (uint8_t *)pixel(0, y);
				if (p && p + row <= fb_mem + fb_len)
					memcpy(p, screen_copy + (size_t)y * var.xres, row);
			}
			free(screen_copy);
			screen_copy = NULL;
		} else {
			for (int y = 0; y < win_h; ++y)
				for (int x = 0; x < win_w; ++x) {
					uint32_t *p = pixel(win_x + x, win_y + y);
					if (p)
						*p = backing[y * win_w + x];
				}
		}
		blit();
		msync(fb_mem, fb_len, MS_SYNC);
		munmap(fb_mem, fb_len);
		fb_mem = NULL;
		fb_len = 0;
	}
	if (fb_fd >= 0) {
		set_manual_blit(0);
		close(fb_fd);
		fb_fd = -1;
	}
}

int set_step(char *str)
{
	step_now++;
	step_percent = 0;
	set_step_without_incr(str);
	return step_now;
}

void set_step_without_incr(char *str)
{
	if (step_label)
		lv_label_set_text(step_label, tr(str));
	draw(1);
}

void set_step_progress(int percent)
{
	step_percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
	draw(0);
}

void set_info_text(char *str)
{
	if (info_label) {
		lv_label_set_text(info_label, str);
		lv_obj_set_hidden(info_label, false);
	}
	draw(1);
}

static void set_countdown(int seconds)
{
	const char *format = NULL;
	if (card_after == FB_REBOOT)
		format = tr(N_("Restart in %d s"));
	else if (card_after == FB_CLOSE)
		format = tr(N_("Ends in %d s"));
	if (format)
		snprintf(countdown_text, sizeof(countdown_text), format, seconds);
	else
		countdown_text[0] = '\0';
}

static void show_message(enum card_kind kind, const char *message, const char *advice, enum fb_after after,
	int seconds)
{
	card_kind = kind;
	card_after = after;
	set_countdown(seconds);
	if (card) {
		show_card();
		lv_label_set_text(card_text, tr(message));
		lv_label_set_text(card_advice, advice ? tr(advice) : "");
		if (advice)
			lv_obj_set_hidden(card_advice, false);
		else
			lv_obj_set_hidden(card_advice, true);
	}
	draw(1);
}

void show_error(const char *message, const char *advice, enum fb_after after, int seconds)
{
	show_message(CARD_ERROR, message, advice, after, seconds);
}

void show_success(const char *message, enum fb_after after, int seconds)
{
	show_message(CARD_SUCCESS, message, NULL, after, seconds);
}

void fb_wait(int seconds)
{
	for (; seconds > 0; --seconds) {
		set_countdown(seconds);
		draw(1);
		sleep(1);
	}
}
