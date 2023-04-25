/*
 * Copyright (C) 2023 Niko Pavlinek
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this software, either in source code form or as a compiled binary, for any
 * purpose, commercial or non-commercial, and by any means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors of this
 * software dedicate any and all copyright interest in the software to the
 * public domain. We make this dedication for the benefit of the public at large
 * and to the detriment of our heirs and successors. We intend this dedication
 * to be an overt act of relinquishment in perpetuity of all present and future
 * rights to this software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/*******************************************************************************
 * STL version
 ******************************************************************************/

static void get_file_list_stl(const std::string &root, std::vector<std::string> &strings) {
    std::string pattern;
    pattern.reserve(MAX_PATH);
    pattern.append(root);
    pattern.append("\\*");

    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileExA(pattern.c_str(), FindExInfoBasic, &find_data, FindExSearchNameMatch, NULL, 0);

    do {
        if (!strcmp(find_data.cFileName, ".")) continue;
        if (!strcmp(find_data.cFileName, "..")) continue;

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

/*******************************************************************************
 * Non-STL version
 ******************************************************************************/

struct PathBuilder {
    char buffer[MAX_PATH];
    size_t used;
};

static void reset_path(PathBuilder *pb) {
    pb->used = 0;
}

static void push_path(PathBuilder *pb, const char *str) {
    size_t length = strlen(str);
    if (length + 1 >= MAX_PATH) {
        fprintf(stderr, "error: no more space left\n");
        exit(EXIT_FAILURE);
    }
    memcpy(pb->buffer + pb->used, str, length);
    pb->buffer[pb->used + length] = '\0';
    pb->used += length;
}

struct FileName {
    size_t length;
    FileName *next;
    char name[1];
};

static void get_file_list_nostl(const char *root, FileName *strings) {
    PathBuilder pattern;
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;

    reset_path(&pattern);
    push_path(&pattern, root);
    push_path(&pattern, "\\*");
    find_handle = FindFirstFileExA(pattern.buffer, FindExInfoBasic, &find_data, FindExSearchNameMatch, NULL, 0);

    do {
        if (!strcmp(find_data.cFileName, ".")) continue;
        if (!strcmp(find_data.cFileName, "..")) continue;

        reset_path(&pattern);
        push_path(&pattern, root);
        push_path(&pattern, "\\");
        push_path(&pattern, find_data.cFileName);

        FileName *file = (FileName *)malloc(sizeof(FileName) + pattern.used * sizeof(char));
        file->length = pattern.used;
        file->next = NULL;
        memcpy(file->name, pattern.buffer, pattern.used + 1);
        for (; strings->next; strings = strings->next) continue;
        strings->next = file;

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            get_file_list_nostl(pattern.buffer, strings);
        }
    } while (FindNextFileA(find_handle, &find_data));
}

/*******************************************************************************
 * Non-STL, custom allocator version
 ******************************************************************************/

#define CLAMP_TOP(val, max) ((val) > (max) ? (max) : (val))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define NEXT_MULTIPLE(num, base) (((num) + ((base) - 1)) & ~((base) - 1))

struct LinearArena {
    uint8_t *base;
    size_t used;
    size_t committed;
    size_t reserved;
};

static void make(LinearArena *arena, size_t reserve_size) {
    arena->base = (uint8_t *)VirtualAlloc(NULL, reserve_size, MEM_RESERVE, PAGE_READWRITE);
    arena->used = 0;
    arena->committed = 0;
    arena->reserved = reserve_size;
}

static void *alloc(LinearArena *arena, size_t size) {
    if (arena->used >= arena->reserved) return NULL;

    size_t aligned_size = NEXT_MULTIPLE(size, 2 * sizeof(void *));
    if (arena->used + aligned_size > arena->committed) {
        /* @note: Assuming page size is 4 KB. */
        size_t page_aligned_size = NEXT_MULTIPLE(aligned_size, 4096);
        /* @note: Committing pages is rather expensive. This is the most
         * important piece of logic, when it comes to performance. This number
         * needs to be set just right for optimal performance, we don't want to
         * commit too often, but also want to minimize the commit size. */
        size_t commit_size = CLAMP_TOP(arena->committed + MAX(page_aligned_size, 100 * 4096), arena->reserved);
        if (!VirtualAlloc(arena->base, commit_size, MEM_COMMIT, PAGE_READWRITE)) return NULL;
        arena->committed = commit_size;
    }

    void *mem = &arena->base[arena->used];
    arena->used += aligned_size;
    return mem;
}

static void get_file_list_custom(const char *root, LinearArena *arena, FileName *strings) {
    PathBuilder pattern = {};
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;

    push_path(&pattern, root);
    push_path(&pattern, "\\*");
    find_handle = FindFirstFileExA(pattern.buffer, FindExInfoBasic, &find_data, FindExSearchNameMatch, NULL, 0);

    do {
        if (!strcmp(find_data.cFileName, ".")) continue;
        if (!strcmp(find_data.cFileName, "..")) continue;

        pattern.used = 0;
        push_path(&pattern, root);
        push_path(&pattern, "\\");
        push_path(&pattern, find_data.cFileName);

        FileName *file = (FileName *)alloc(arena, sizeof(FileName) + pattern.used * sizeof(char));
        file->length = pattern.used;
        file->next = NULL;
        memcpy(file->name, pattern.buffer, pattern.used + 1);
        for (; strings->next; strings = strings->next) continue;
        strings->next = file;

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            get_file_list_custom(pattern.buffer, arena, strings);
        }
    } while (FindNextFileA(find_handle, &find_data));
}

/******************************************************************************/

int main() {
    LARGE_INTEGER freq, begin, end;
    QueryPerformanceFrequency(&freq);

    {
        std::vector<std::string> strings;

        QueryPerformanceCounter(&begin);
        get_file_list_stl(".", strings);
        QueryPerformanceCounter(&end);
        size_t file_count = 0;
        for (size_t i = 0; i < strings.size(); ++i) ++file_count;

        printf("STL version took ");
        double elapsed = (double)(end.QuadPart - begin.QuadPart) * 1000000000.0 / (double)freq.QuadPart;
        if (elapsed >= 1000000000.0) {
            printf("%.2f s ", elapsed / 1000000000.0);
        } else if (elapsed >= 1000000.0) {
            printf("%.2f ms ", elapsed / 1000000.0);
        } else if (elapsed >= 1000.0) {
            printf("%.2f us ", elapsed / 1000.0);
        } else {
            printf("%.2f ns ", elapsed);
        }
        printf("and found %lld items\n", file_count);
    }

    {
        FileName *first = (FileName *)calloc(1, sizeof(FileName) + sizeof(char));
        first->length = 1;
        first->next = NULL;
        first->name[0] = '.';

        QueryPerformanceCounter(&begin);
        get_file_list_nostl(first->name, first);
        QueryPerformanceCounter(&end);

        size_t file_count = 0;
        for (first = first->next; first; first = first->next) ++file_count;

        printf("Non-STL version took ");
        double elapsed = (double)(end.QuadPart - begin.QuadPart) * 1000000000.0 / (double)freq.QuadPart;
        if (elapsed >= 1000000000.0) {
            printf("%.2f s ", elapsed / 1000000000.0);
        } else if (elapsed >= 1000000.0) {
            printf("%.2f ms ", elapsed / 1000000.0);
        } else if (elapsed >= 1000.0) {
            printf("%.2f us ", elapsed / 1000.0);
        } else {
            printf("%.2f ns ", elapsed);
        }
        printf("and found %lld items\n", file_count);
    }

    {
        LinearArena arena;
        make(&arena, 1024 * 1024 * 1024);

        FileName *first = (FileName *)alloc(&arena, sizeof(FileName) + sizeof(char));
        first->length = 1;
        first->next = NULL;
        first->name[0] = '.';
        first->name[1] = '\0';

        QueryPerformanceCounter(&begin);
        get_file_list_custom(".", &arena, first);
        QueryPerformanceCounter(&end);

        size_t file_count = 0;
        for (first = first->next; first; first = first->next) ++file_count;

        printf("Custom allocator version took ");
        double elapsed = (double)(end.QuadPart - begin.QuadPart) * 1'000'000'000.0 / (double)freq.QuadPart;
        if (elapsed >= 1'000'000'000.0) {
            printf("%.2f s ", elapsed / 1'000'000'000.0);
        } else if (elapsed >= 1000000.0) {
            printf("%.2f ms ", elapsed / 1'000'000.0);
        } else if (elapsed >= 1000.0) {
            printf("%.2f us ", elapsed / 1'000.0);
        } else {
            printf("%.2f ns ", elapsed);
        }
        printf("and found %lld items\n", file_count);
    }
}
