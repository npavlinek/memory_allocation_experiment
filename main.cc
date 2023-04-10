// Copyright (C) 2023 Niko Pavlinek
//
// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either
// in source code form or as a compiled binary, for any purpose, commercial or non-commercial, and
// by any means.
//
// In jurisdictions that recognize copyright laws, the author or authors of this software dedicate
// any and all copyright interest in the software to the public domain. We make this dedication for
// the benefit of the public at large and to the detriment of our heirs and successors. We intend
// this dedication to be an overt act of relinquishment in perpetuity of all present and future
// rights to this software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
// NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <http://unlicense.org/>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MAX_STRING_LENGTH 260

//
// STL version
//

static void get_file_list_stl(const std::string &root, std::vector<std::string> &strings)
{
  std::string pattern;
  pattern.reserve(MAX_STRING_LENGTH);
  pattern.append(root);
  pattern.append("\\*");

  WIN32_FIND_DATAA find_data;
  HANDLE find_handle = FindFirstFileExA(pattern.c_str(), FindExInfoBasic, &find_data, FindExSearchNameMatch, NULL, 0);

  do {
    if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
      continue;
    }

    pattern.clear();
    pattern.append(root);
    pattern.append("\\");
    pattern.append(find_data.cFileName);
    strings.push_back(pattern);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      get_file_list_stl(pattern, strings);
    }
  } while (FindNextFileA(find_handle, &find_data));
}

//
// No STL version
//

struct StringBuilder {
  char buffer[MAX_STRING_LENGTH];
  size_t used;
};

static void string_builder_push(StringBuilder *builder, const char *string)
{
  const size_t string_length = strlen(string);
  if ((string_length + 1) >= MAX_STRING_LENGTH) {
    fprintf(stderr, "error: no more space left\n");
    exit(EXIT_FAILURE);
  }
  memcpy(builder->buffer + builder->used, string, string_length);
  builder->buffer[builder->used + string_length] = '\0';
  builder->used += string_length;
}

struct FileName {
  size_t length;
  FileName *next;
  char name[1]; // C++ does not support flexible array members without compiler extensions. Yet another reason why C is better. :)
};

static void get_file_list_nostl(const char *root, FileName *strings)
{
  StringBuilder pattern = {};
  string_builder_push(&pattern, root);
  string_builder_push(&pattern, "\\*");

  WIN32_FIND_DATAA find_data;
  HANDLE find_handle = FindFirstFileExA(pattern.buffer, FindExInfoBasic, &find_data, FindExSearchNameMatch, NULL, 0);

  do {
    if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
      continue;
    }

    pattern.used = 0;
    string_builder_push(&pattern, root);
    string_builder_push(&pattern, "\\");
    string_builder_push(&pattern, find_data.cFileName);

    auto *file = (FileName *)malloc(sizeof(FileName) + (pattern.used) * sizeof(char));
    file->length = pattern.used;
    file->next = NULL;
    memcpy(file->name, pattern.buffer, pattern.used + 1);
    while (strings->next != NULL) {
      strings = strings->next;
    }
    strings->next = file;

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      get_file_list_nostl(pattern.buffer, strings);
    }
  } while (FindNextFileA(find_handle, &find_data));
}

//
// No STL, custom allocator version
//

#define CLAMP_TOP(val, max) ((val) > (max) ? (max) : (val))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define NEXT_MULTIPLE(num, base) (((num) + ((base) - 1)) & ~((base) - 1))

struct LinearArena {
  uint8_t *base;
  size_t used;
  size_t committed;
  size_t reserved;
};

static void linear_arena_create(LinearArena *arena, size_t reserve_size)
{
  arena->base = (uint8_t *)VirtualAlloc(NULL, reserve_size, MEM_RESERVE, PAGE_READWRITE);
  arena->used = 0;
  arena->committed = 0;
  arena->reserved = reserve_size;
}

static void *linear_arena_push_size(LinearArena *arena, size_t size)
{
  if (arena->used >= arena->reserved) {
    return NULL;
  }

  const size_t aligned_size = NEXT_MULTIPLE(size, sizeof(void *));
  if ((arena->used + aligned_size) > arena->committed) {
    // @note: Assuming page size is 4 KB.
    const size_t page_aligned_size = NEXT_MULTIPLE(aligned_size, 4096);
    // @note: Committing pages is rather expensive. This is the most important piece of logic, when
    // it comes to performance. This number needs to be set just right for optimal performance, we
    // don't want to commit too often, but also want to minimize the commit size.
    const size_t commit_size = CLAMP_TOP(arena->committed + MAX(page_aligned_size, 100 * 4096), arena->reserved);
    if (VirtualAlloc(arena->base, commit_size, MEM_COMMIT, PAGE_READWRITE) == NULL) {
      return NULL;
    }
    arena->committed = commit_size;
  }

  void *mem = &arena->base[arena->used];
  arena->used += aligned_size;
  return mem;
}

static void get_file_list_custom(const char *root, LinearArena *arena, FileName *strings)
{
  StringBuilder pattern = {};
  string_builder_push(&pattern, root);
  string_builder_push(&pattern, "\\*");

  WIN32_FIND_DATAA find_data;
  HANDLE find_handle = FindFirstFileExA(pattern.buffer, FindExInfoBasic, &find_data, FindExSearchNameMatch, NULL, 0);

  do {
    if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
      continue;
    }

    pattern.used = 0;
    string_builder_push(&pattern, root);
    string_builder_push(&pattern, "\\");
    string_builder_push(&pattern, find_data.cFileName);

    auto *file = (FileName *)linear_arena_push_size(arena, sizeof(FileName) + (pattern.used) * sizeof(char));
    file->length = pattern.used;
    file->next = NULL;
    memcpy(file->name, pattern.buffer, pattern.used + 1);
    while (strings->next != NULL) {
      strings = strings->next;
    }
    strings->next = file;

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      get_file_list_custom(pattern.buffer, arena, strings);
    }
  } while (FindNextFileA(find_handle, &find_data));
}

int main()
{
  LARGE_INTEGER frequency, begin, end;
  QueryPerformanceFrequency(&frequency);

  {
    std::vector<std::string> strings;

    QueryPerformanceCounter(&begin);
    get_file_list_stl(".", strings);
    QueryPerformanceCounter(&end);

    size_t file_count = 0;
    for (size_t i = 0; i < strings.size(); ++i) {
      ++file_count;
    }

    printf("STL version took ");
    const double elapsed = (double)(end.QuadPart - begin.QuadPart) / (double)frequency.QuadPart;
    if (elapsed >= 1.0) {
      printf("%.2f s ", elapsed);
    } else if ((elapsed * 1000.0) >= 1.0) {
      printf("%.2f ms ", elapsed * 1000.0);
    } else if ((elapsed * 1000.0 * 1000.0) >= 0) {
      printf("%.2f us ", elapsed * 1000.0 * 1000.0);
    } else if ((elapsed * 1000.0 * 1000.0 * 1000.0) >= 0) {
      printf("%.2f ns ", elapsed * 1000.0 * 1000.0 * 1000.0);
    }
    printf("and found %lld items\n", file_count);
  }

  {
    auto *first = (FileName *)malloc(sizeof(FileName) + sizeof(char));
    first->length = 1;
    first->next = NULL;
    memcpy(first->name, ".", 2 * sizeof(char));

    QueryPerformanceCounter(&begin);
    get_file_list_nostl(".", first);
    QueryPerformanceCounter(&end);

    size_t file_count = 0;
    for (first = first->next; first != NULL; first = first->next) {
      ++file_count;
    }

    printf("No STL version took ");
    const double elapsed = (double)(end.QuadPart - begin.QuadPart) / (double)frequency.QuadPart;
    if (elapsed >= 1.0) {
      printf("%.2f s ", elapsed);
    } else if ((elapsed * 1000.0) >= 1.0) {
      printf("%.2f ms ", elapsed * 1000.0);
    } else if ((elapsed * 1000.0 * 1000.0) >= 0) {
      printf("%.2f us ", elapsed * 1000.0 * 1000.0);
    } else if ((elapsed * 1000.0 * 1000.0 * 1000.0) >= 0) {
      printf("%.2f ns ", elapsed * 1000.0 * 1000.0 * 1000.0);
    }
    printf("and found %lld items\n", file_count);
  }

  {
    LinearArena arena;
    linear_arena_create(&arena, 1024 * 1024 * 1024);

    auto *first = (FileName *)linear_arena_push_size(&arena, sizeof(FileName) + sizeof(char));
    first->length = 1;
    first->next = NULL;
    memcpy(first->name, ".", 2 * sizeof(char));

    QueryPerformanceCounter(&begin);
    get_file_list_custom(".", &arena, first);
    QueryPerformanceCounter(&end);

    size_t file_count = 0;
    for (first = first->next; first != NULL; first = first->next) {
      ++file_count;
    }

    printf("Custom allocator version took ");
    const double elapsed = (double)(end.QuadPart - begin.QuadPart) / (double)frequency.QuadPart;
    if (elapsed >= 1.0) {
      printf("%.2f s ", elapsed);
    } else if ((elapsed * 1000.0) >= 1.0) {
      printf("%.2f ms ", elapsed * 1000.0);
    } else if ((elapsed * 1000.0 * 1000.0) >= 0) {
      printf("%.2f us ", elapsed * 1000.0 * 1000.0);
    } else if ((elapsed * 1000.0 * 1000.0 * 1000.0) >= 0) {
      printf("%.2f ns ", elapsed * 1000.0 * 1000.0 * 1000.0);
    }
    printf("and found %lld items\n", file_count);
  }
}
