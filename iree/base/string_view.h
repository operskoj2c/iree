// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef IREE_BASE_STRING_VIEW_H_
#define IREE_BASE_STRING_VIEW_H_

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "iree/base/attributes.h"
#include "iree/base/config.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

#define IREE_STRING_VIEW_NPOS SIZE_MAX

// A string view (ala std::string_view) into a non-NUL-terminated string.
typedef struct {
  const char* data;
  iree_host_size_t size;
} iree_string_view_t;

// Returns an empty string view ("").
static inline iree_string_view_t iree_string_view_empty(void) {
  iree_string_view_t v = {0, 0};
  return v;
}

// Returns true if the given string view is the empty string.
#define iree_string_view_is_empty(sv) (((sv).data == NULL) || ((sv).size == 0))

static inline iree_string_view_t iree_make_string_view(
    const char* str, iree_host_size_t str_length) {
  iree_string_view_t v = {str, str_length};
  return v;
}

// Returns a string view initialized with a reference to the given
// NUL-terminated string literal.
static inline iree_string_view_t iree_make_cstring_view(const char* str) {
  iree_string_view_t v = {str, strlen(str)};
  return v;
}

#define iree_string_view_literal(str) \
  { .data = (str), .size = IREE_ARRAYSIZE(str) - 1 }

// Returns a string view initialized with the given cstring.
#define IREE_SV(cstr) iree_make_cstring_view(cstr)

// Returns true if the two strings are equal (compare == 0).
IREE_API_EXPORT bool iree_string_view_equal(iree_string_view_t lhs,
                                            iree_string_view_t rhs);

// Like std::string::compare but with iree_string_view_t values.
IREE_API_EXPORT int iree_string_view_compare(iree_string_view_t lhs,
                                             iree_string_view_t rhs);

// Finds the first occurrence of |c| in |value| starting at |pos|.
// Returns the found character position or IREE_STRING_VIEW_NPOS if not found.
IREE_API_EXPORT iree_host_size_t iree_string_view_find_char(
    iree_string_view_t value, char c, iree_host_size_t pos);

// Returns the index of the first occurrence of one of the characters in |s| or
// -1 if none of the characters were found.
IREE_API_EXPORT iree_host_size_t iree_string_view_find_first_of(
    iree_string_view_t value, iree_string_view_t s, iree_host_size_t pos);

// Returns the index of the last occurrence of one of the characters in |s| or
// -1 if none of the characters were found.
IREE_API_EXPORT iree_host_size_t iree_string_view_find_last_of(
    iree_string_view_t value, iree_string_view_t s, iree_host_size_t pos);

// Returns true if the string starts with the given prefix.
IREE_API_EXPORT bool iree_string_view_starts_with(iree_string_view_t value,
                                                  iree_string_view_t prefix);

// Returns true if the string starts with the given suffix.
IREE_API_EXPORT bool iree_string_view_ends_with(iree_string_view_t value,
                                                iree_string_view_t suffix);

// Removes the first |n| characters from the string view (not the data).
IREE_API_EXPORT iree_string_view_t
iree_string_view_remove_prefix(iree_string_view_t value, iree_host_size_t n);

// Removes the last |n| characters from the string view (not the data).
IREE_API_EXPORT iree_string_view_t
iree_string_view_remove_suffix(iree_string_view_t value, iree_host_size_t n);

// Removes the given substring prefix from the string view if present.
IREE_API_EXPORT iree_string_view_t iree_string_view_strip_prefix(
    iree_string_view_t value, iree_string_view_t prefix);

// Removes the given substring suffix from the string view if present.
IREE_API_EXPORT iree_string_view_t iree_string_view_strip_suffix(
    iree_string_view_t value, iree_string_view_t suffix);

// Removes the given substring prefix from the string view if present in-place.
// Returns true if the strip succeeded.
IREE_API_EXPORT bool iree_string_view_consume_prefix(iree_string_view_t* value,
                                                     iree_string_view_t prefix);

// Removes the given substring suffix from the string view if present in-place.
// Returns true if the strip succeeded.
IREE_API_EXPORT bool iree_string_view_consume_suffix(iree_string_view_t* value,
                                                     iree_string_view_t suffix);

// Removes leading and trailing whitespace.
IREE_API_EXPORT iree_string_view_t
iree_string_view_trim(iree_string_view_t value);

// Returns a substring of the string view at offset |pos| and length |n|.
// Use |n| == INTPTR_MAX to take the remainder of the string after |pos|.
// Returns empty string on failure.
IREE_API_EXPORT iree_string_view_t iree_string_view_substr(
    iree_string_view_t value, iree_host_size_t pos, iree_host_size_t n);

// Splits |value| into two parts based on the first occurrence of |split_char|.
// Returns the index of the |split_char| in the original |value| or -1 if not
// found.
IREE_API_EXPORT intptr_t iree_string_view_split(iree_string_view_t value,
                                                char split_char,
                                                iree_string_view_t* out_lhs,
                                                iree_string_view_t* out_rhs);

// Replaces all occurrences of |old_char| with |new_char|.
IREE_API_EXPORT void iree_string_view_replace_char(iree_string_view_t value,
                                                   char old_char,
                                                   char new_char);

// Returns true if the given |value| matches |pattern| (normal * and ? rules).
// This accepts wildcards in the form of '*' and '?' for any delimited value.
// '*' will match zero or more of any character and '?' will match exactly one
// of any character.
//
// For example,
// 'foo-*-bar' matches: 'foo-123-bar', 'foo-456-789-bar'
// 'foo-10?' matches: 'foo-101', 'foo-102'
IREE_API_EXPORT bool iree_string_view_match_pattern(iree_string_view_t value,
                                                    iree_string_view_t pattern);

// Copies the string bytes into the target buffer and returns the number of
// characters copied. Does not include a NUL terminator.
IREE_API_EXPORT iree_host_size_t iree_string_view_append_to_buffer(
    iree_string_view_t source_value, iree_string_view_t* target_value,
    char* buffer);

IREE_API_EXPORT bool iree_string_view_atoi_int32(iree_string_view_t value,
                                                 int32_t* out_value);
IREE_API_EXPORT bool iree_string_view_atoi_uint32(iree_string_view_t value,
                                                  uint32_t* out_value);
IREE_API_EXPORT bool iree_string_view_atoi_int64(iree_string_view_t value,
                                                 int64_t* out_value);
IREE_API_EXPORT bool iree_string_view_atoi_uint64(iree_string_view_t value,
                                                  uint64_t* out_value);
IREE_API_EXPORT bool iree_string_view_atof(iree_string_view_t value,
                                           float* out_value);
IREE_API_EXPORT bool iree_string_view_atod(iree_string_view_t value,
                                           double* out_value);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_BASE_STRING_VIEW_H_
