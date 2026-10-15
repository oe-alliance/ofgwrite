#include "i18n.h"

#include <stdio.h>
#include <string.h>

#define SETTINGS "/etc/enigma2/settings"
#define BOXINFO "/usr/lib/enigma.info"

static int chosen = -1;  /* Index in i18n_languages, -1 for English. */

/* The value of key in a file of key=value lines, without quotes; 0 when missing. */
static int file_value(const char *path, const char *key, char *value, size_t size)
{
	char line[256];
	size_t length = strlen(key);
	int found = 0;
	FILE *file = fopen(path, "r");
	if (!file)
		return 0;
	while (fgets(line, sizeof(line), file))
		if (!strncmp(line, key, length) && line[length] == '=') {
			char *start = line + length + 1;
			start += strspn(start, "'\"");
			start[strcspn(start, "'\"\r\n")] = '\0';
			snprintf(value, size, "%s", start);
			found = value[0] != '\0';
		}
	fclose(file);
	return found;
}

/* Like Enigma2: config.osd.language, else config.misc.locale, else the default of the brand. */
static void enigma2_language(char *locale, size_t size)
{
	char brand[64] = "";
	if (file_value(SETTINGS, "config.osd.language", locale, size) ||
		file_value(SETTINGS, "config.misc.locale", locale, size))
		return;
	file_value(BOXINFO, "displaybrand", brand, sizeof(brand));
	if (!strcmp(brand, "Atto.TV"))
		snprintf(locale, size, "pt_BR");
	else if (!strcmp(brand, "Zgemma"))
		snprintf(locale, size, "en_US");
	else if (!strcmp(brand, "Beyonwiz"))
		snprintf(locale, size, "en_AU");
	else
		snprintf(locale, size, "de_DE");
}

void i18n_init(const char *language)
{
	char locale[32];
	if (language && *language)
		snprintf(locale, sizeof(locale), "%s", language);
	else
		enigma2_language(locale, sizeof(locale));
	chosen = -1;
	for (int i = 0; i < 3; ++i)
		if (!strncmp(locale, i18n_languages[i], 2))
			chosen = i;
}

const char *tr(const char *text)
{
	if (chosen < 0 || !text)
		return text;
	for (int i = 0; i < translation_count; ++i)
		if (!strcmp(translations[i].text, text))
			return translations[i].translated[chosen] && *translations[i].translated[chosen] ?
				translations[i].translated[chosen] : text;
	return text;
}

int i18n_rtl(void)
{
	return chosen >= 0 && !strcmp(i18n_languages[chosen], "ar");
}
