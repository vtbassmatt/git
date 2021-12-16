#include "../git-compat-util.h"

size_t gitstrlcpy(char *dest, const char *src, size_t size)
{
	/*
	 * NOTE: original strlcpy returns full length of src, but this is
	 * unsafe. This implementation returns `size` if src is too long.
	 * This behaviour is faster and still allows to detect an issue.
	 */
	size_t ret = strnlen(src, size);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}
