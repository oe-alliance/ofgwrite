#ifndef OFGWRITE_I18N_H
#define OFGWRITE_I18N_H

/* Marks a text of the window for tools/mkstrings.py, translated where it is shown. */
#define N_(text) text

struct translation
{
	const char *text;
	const char *translated[3];  /* In the order of i18n_languages. */
};

extern const char *const i18n_languages[3];
extern const struct translation translations[];
extern const int translation_count;

/* The language of Enigma2, or language when set; English without a translation. Before pivot_root. */
void i18n_init(const char *language);
/* text in the chosen language, else text itself. */
const char *tr(const char *text);
/* 1 for a language written from the right. */
int i18n_rtl(void);

#endif
