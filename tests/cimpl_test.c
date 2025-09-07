/*
 * Copyright (C) 2025 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

int superfw_strcmp(const char *a, const char *b);
int superfw_strncmp(const char *a, const char *b, unsigned n);
char *superfw_strchr(const char *s, int c);
char *superfw_strrchr(const char *s, int c);
char *superfw_strcat(char *dest, const char *src);
size_t superfw_strlen(const char *s);

int main() {
  const char *s = "hello world";
  const char *emptystr = "";
  const char *with_null = "abc\0def";

  assert(0 == superfw_strlen(""));
  assert(1 == superfw_strlen("f"));
  assert(2 == superfw_strlen("fo"));
  assert(3 == superfw_strlen("foo"));
  assert(4 == superfw_strlen("fooo"));

  assert(superfw_strcmp("", "") == 0);
  assert(superfw_strcmp("a", "") > 0);
  assert(superfw_strcmp("", "a") < 0);
  assert(superfw_strcmp("foo", "bar") > 0);
  assert(superfw_strcmp("bar", "foo") < 0);
  assert(superfw_strcmp("foo", "foo") == 0);
  assert(superfw_strcmp("f", "foo") < 0);
  assert(superfw_strcmp("foo", "f") > 0);
  assert(superfw_strcmp("abc", "ABC") > 0);
  assert(superfw_strcmp("ABC", "abc") < 0);
  assert(superfw_strcmp("Abc", "Abc") == 0);
  assert(superfw_strcmp("abc", "abC") > 0);
  assert(superfw_strcmp("abC", "abc") < 0);
  assert(superfw_strcmp("abc", "abd") < 0);
  assert(superfw_strcmp("abc", "abb") > 0);

  assert(superfw_strncmp("hello", "world", 0) == 0);
  assert(superfw_strncmp("hello", "hello", 5) == 0);
  assert(superfw_strncmp("hello", "hello", 10) == 0);
  assert(superfw_strncmp("hello", "heLLo", 2) == 0);
  assert(superfw_strncmp("hello", "heLLo", 3) > 0);
  assert(superfw_strncmp("abc", "xyz", 3) < 0);
  assert(superfw_strncmp("xyz", "abc", 3) > 0);
  assert(superfw_strncmp("abc", "abcd", 3) == 0);
  assert(superfw_strncmp("abc", "abcd", 4) < 0);
  assert(superfw_strncmp("abcd", "abc", 4) > 0);
  assert(superfw_strncmp("", "", 1) == 0);
  assert(superfw_strncmp("", "a", 1) < 0);
  assert(superfw_strncmp("a", "", 1) > 0);
  assert(superfw_strncmp("abc\0def", "abc\0xyz", 7) == 0);
  assert(superfw_strncmp("abc\0def", "abc", 4) == 0);
  assert(superfw_strncmp("abc\0def", "abcXdef", 4) < 0);
  assert(superfw_strncmp("abc", "ABC", 3) > 0);
  assert(superfw_strncmp("ABC", "abc", 3) < 0);
  assert(superfw_strncmp("zbc", "abc", 1) > 0);
  assert(superfw_strncmp("abc", "zbc", 1) < 0);
  assert(superfw_strncmp("abcde", "abcdf", 4) == 0);
  assert(superfw_strncmp("abcde", "abcdf", 5) < 0);

  assert(superfw_strchr(s, 'x') == NULL);
  assert(superfw_strchr(s, 'h') == s);
  assert(superfw_strchr(s, 'o') == s + 4);   // First 'o'
  assert(superfw_strchr(s, 'w') == s + 6);
  assert(superfw_strchr(s, 'l') == s + 2);   // multiple "l"s, only first
  assert(superfw_strchr(s, '\0') == s + strlen(s));
  assert(superfw_strchr(emptystr, '\0') == emptystr);
  assert(superfw_strchr(emptystr, 'a') == NULL);
  assert(superfw_strchr(with_null, '\0') == with_null + 3);

  assert(superfw_strrchr(s, 'x') == NULL);
  assert(superfw_strrchr(s, 'h') == s);
  assert(superfw_strrchr(s, 'o') == s + 7);   // Last 'o'
  assert(superfw_strrchr(s, 'l') == s + 9);   // Last 'l'
  assert(superfw_strrchr(s, 'd') == s + 10);
  assert(superfw_strrchr(s, '\0') == s + strlen(s));
  assert(superfw_strrchr(emptystr, '\0') == emptystr);
  assert(superfw_strrchr(emptystr, 'a') == NULL);
}


