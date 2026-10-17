/* Shows the flash window in one of its states without flashing, e.g. to check a translation:
 *
 *   ofgwrite_fbdemo [--lang=xx] [--dim] progress|check|error|success [seconds]
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../i18n.h"
#include "../ofgwrite.h"

void my_printf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

int main(int argc, char *argv[])
{
	const char *state = "progress";
	int seconds = 10;
	for (int i = 1; i < argc; ++i)
		if (!strncmp(argv[i], "--lang=", 7))
			set_language(argv[i] + 7);
		else if (!strcmp(argv[i], "--dim"))
			set_dim(1);
		else if (atoi(argv[i]) > 0)
			seconds = atoi(argv[i]);
		else
			state = argv[i];
	set_checking(!strcmp(state, "check"));
	if (!init_framebuffer(6) || !show_main_window(0, "5.0.2"))
		return EXIT_FAILURE;
	set_step(N_("Killing processes"));
	set_step(N_("Syncing filesystem"));
	set_step(N_("init 2"));
	set_step(N_("Extracting rootfs"));
	set_step_progress(40);
	if (!strcmp(state, "error"))
		show_error(N_("The rootfs could not be unpacked. The flashed image will not start like this."),
			N_("Flash a backup."), FB_REBOOT, seconds);
	else if (!strcmp(state, "success"))
		show_success(N_("The image was flashed."), FB_REBOOT, seconds);
	if (!strcmp(state, "progress") || !strcmp(state, "check"))
		sleep(seconds);
	else
		fb_wait(seconds);
	close_framebuffer();
	return EXIT_SUCCESS;
}
