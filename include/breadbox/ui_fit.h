#ifndef UI_FIT_H
#define UI_FIT_H
/* One fit policy for every line of text on the bottom screen (titles, values, helper lines):
 * draw at the wanted size; if too wide, shrink down to the readable floor `min`; if still too wide,
 * cut between whole UTF-8 code points and end with "...". Never smaller than `min`, never a broken
 * character, never wider than `width` (when even "..." does not fit, the result is empty).
 * The full text stays where the item is focused (the top screen's tag or title).
 * Pure C (no citro2d): the caller passes its measure, so the host tests run the production code. */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef float (*ui_fit_measure)(const char *s, float size, void *ud);

#define UI_FIT_ELLIPSIS "..."
#define UI_FIT_MAX 480   /* longer text is cut here first (a title, a path, a help sentence are shorter) */

/* Byte length of the longest prefix of s (at most n bytes) that ends on a code-point boundary. */
static inline size_t ui_fit_utf8_floor(const char *s, size_t n)
{
	while (n && ((unsigned char)s[n] & 0xc0) == 0x80) n--;
	return n;
}

/* Fits s into width. Writes the text to draw into out (cap bytes, always terminated) and returns the
 * size to draw it at (size, or smaller down to min). *cut (optional) tells whether text was dropped. */
static inline float ui_fit_bounded(const char *s, float size, float min, float width,
	ui_fit_measure measure, void *ud, char *out, size_t cap, bool *cut)
{
	if (cut) *cut = false;
	if (!cap) return size;
	out[0] = 0;
	if (!s) return size;
	size_t len = strlen(s);
	if (min > size) min = size;
	float w = measure(s, size, ud);
	if (w <= width && len < cap) { memcpy(out, s, len + 1); return size; }
	float fitted = w > 0 ? size * width / w : size;
	if (fitted >= min && len < cap) {
		/* proportional shrink can land a hair over: step down until it measures inside */
		for (int i = 0; i < 8 && measure(s, fitted, ud) > width; i++) fitted *= 0.98f;
		if (fitted >= min && measure(s, fitted, ud) <= width) { memcpy(out, s, len + 1); return fitted; }
	}
	/* at the floor: the longest whole-character prefix (trailing spaces dropped) + "..." that fits */
	if (cut) *cut = true;
	char tmp[UI_FIT_MAX + sizeof(UI_FIT_ELLIPSIS)];
	unsigned short at[UI_FIT_MAX + 1];
	size_t nb = 0;
	for (size_t i = 0; i <= len && i <= UI_FIT_MAX; i++)
		if (i == len || ((unsigned char)s[i] & 0xc0) != 0x80) at[nb++] = (unsigned short)i;   /* code-point starts, and the end */
	size_t lo = 0, hi = nb ? nb - 1 : 0, best = 0;   /* at[0] = 0: the empty prefix */
	while (lo <= hi) {
		size_t mid = (lo + hi) / 2, n = at[mid];
		while (n && s[n - 1] == ' ') n--;
		bool ok = n + sizeof(UI_FIT_ELLIPSIS) <= cap;
		if (ok) { memcpy(tmp, s, n); memcpy(tmp + n, UI_FIT_ELLIPSIS, sizeof(UI_FIT_ELLIPSIS)); ok = measure(tmp, min, ud) <= width; }
		if (ok) { best = mid; lo = mid + 1; }
		else { if (!mid) break; hi = mid - 1; }
	}
	size_t n = at[best];
	while (n && s[n - 1] == ' ') n--;
	if (n + sizeof(UI_FIT_ELLIPSIS) <= cap) {
		memcpy(out, s, n); memcpy(out + n, UI_FIT_ELLIPSIS, sizeof(UI_FIT_ELLIPSIS));
		if (measure(out, min, ud) > width) out[0] = 0;   /* not even "..." fits */
	}
	return min;
}

/* Copies text into out (cap bytes) without cutting a UTF-8 character; *cut tells whether it was shortened. */
static inline size_t ui_fit_copy(char *out, size_t cap, const char *text, size_t max_bytes)
{
	if (!cap) return 0;
	size_t n = strlen(text);
	if (n > max_bytes) n = max_bytes;
	if (n >= cap) n = cap - 1;
	n = ui_fit_utf8_floor(text, n);
	memcpy(out, text, n);
	out[n] = 0;
	return n;
}
/* ---- Several lines: a name on a card's ruled lines (the Continue postcard).
 * The rule: one line at size; else wrapped at size into at most max lines; else wrapped at the floor min; else
 * the last of max lines at the floor cut with "...". Wrapped lines are balanced: the narrowest width that keeps
 * the same number of lines, so no lone last word ("Kirby - Nightmare / in Dream Land", not "... Dream / Land").
 * A line breaks after a space when one fits, else between whole code points. Returns the number of lines
 * (0: empty text); *out_size the size to draw them at. */
#define UI_FIT_LINE_MAX 128   /* bytes per line, with its terminator */
typedef char ui_fit_line[UI_FIT_LINE_MAX];

/* s measured as its first n bytes */
static inline float ui_fit_measure_n(const char *s, size_t n, float size, ui_fit_measure measure, void *ud)
{
	char tmp[UI_FIT_MAX + 1];
	if (n > UI_FIT_MAX) n = ui_fit_utf8_floor(s, UI_FIT_MAX);
	memcpy(tmp, s, n);
	tmp[n] = 0;
	return measure(tmp, size, ud);
}

/* Greedy wrap of s at width; lines go to out (up to max, may be NULL), the last allowed one takes the rest.
 * Returns how many lines the whole text needs (can be more than max). */
static inline int ui_fit_wrap(const char *s, float size, float width, int max, ui_fit_measure measure, void *ud, ui_fit_line *out)
{
	int n = 0;
	while (*s == ' ') s++;
	while (*s) {
		size_t len = strlen(s);
		if (len > UI_FIT_MAX) len = ui_fit_utf8_floor(s, UI_FIT_MAX);
		size_t cut = len;
		if (ui_fit_measure_n(s, len, size, measure, ud) > width) {
			/* the longest whole-code-point prefix that fits (at least one code point), then back to its last space */
			unsigned short at[UI_FIT_MAX + 1];
			size_t nb = 0;
			for (size_t i = 1; i <= len; i++)
				if (i == len || ((unsigned char)s[i] & 0xc0) != 0x80) at[nb++] = (unsigned short)i;   /* code-point ends */
			size_t lo = 0, hi = nb - 1, best = 0;
			while (lo < hi) {   /* the last end that fits; the first one always counts */
				size_t mid = (lo + hi + 1) / 2;
				if (ui_fit_measure_n(s, at[mid], size, measure, ud) <= width) lo = mid;
				else hi = mid - 1;
			}
			best = at[lo];
			size_t sp = best;
			if (s[best] != ' ')
				while (sp && s[sp] != ' ') sp--;
			cut = sp ? sp : best;
		}
		if (out && n < max) {
			ui_fit_copy(out[n], UI_FIT_LINE_MAX, s, n == max - 1 ? strlen(s) : cut);   /* the last allowed line takes the rest */
			size_t k = strlen(out[n]);
			while (k && out[n][k - 1] == ' ') out[n][--k] = 0;
		}
		n++;
		s += cut;
		while (*s == ' ') s++;
	}
	return n;
}

static inline int ui_fit_lines_bounded(const char *s, float size, float min, float width, int max,
	ui_fit_measure measure, void *ud, ui_fit_line *out, float *out_size)
{
	if (out_size) *out_size = size;
	if (!s || max < 1) return 0;
	while (*s == ' ') s++;
	if (!*s) return 0;
	if (min > size) min = size;
	float at = min;
	int n = ui_fit_wrap(s, size, width, 0, measure, ud, NULL);
	if (n <= max) at = size;
	else n = ui_fit_wrap(s, min, width, 0, measure, ud, NULL);
	if (out_size) *out_size = at;
	if (n > max) {   /* even at the floor: max lines, the last one cut */
		ui_fit_wrap(s, at, width, max, measure, ud, out);
		char last[UI_FIT_LINE_MAX];
		ui_fit_bounded(out[max - 1], at, at, width, measure, ud, last, sizeof(last), NULL);
		memcpy(out[max - 1], last, sizeof(last));
		return max;
	}
	float w = width;
	if (n > 1) {   /* balanced: the narrowest width (whole pixels, down to half) with the same number of lines */
		int lo = (int)(width / 2), hi = (int)width;
		while (lo < hi) {
			int mid = (lo + hi) / 2;
			if (ui_fit_wrap(s, at, (float)mid, 0, measure, ud, NULL) == n) hi = mid;
			else lo = mid + 1;
		}
		if ((float)lo < w) w = (float)lo;
	}
	return ui_fit_wrap(s, at, w, n, measure, ud, out);
}

#endif
