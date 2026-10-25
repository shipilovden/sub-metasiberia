#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <string.h>

// no unicode!
inline bool get_dll_path (char *res)
{
    char path[MAX_PATH];
    HMODULE hm;
    if (GetModuleHandleExW (
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPWSTR)&get_dll_path, &hm))
    {
        const DWORD length = GetModuleFileNameA (hm, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return false;
        char *last_slash = strrchr (path, '\\');
        if (!last_slash)
            return false;
        last_slash[1] = '\0';
        strcpy_s (res, MAX_PATH, path);
        return true;
    }
    return false;
}

#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <string.h>
#include <string>


inline bool get_dll_path (char *res)
{
    Dl_info dl_info;
    if (dladdr ((void *)get_dll_path, &dl_info))
    {
        std::string full_path = dl_info.dli_fname;
        std::string dir_path = full_path.substr (0, full_path.find_last_of ("/") + 1);
        strcpy (res, dir_path.c_str ());
        return true;
    }
    return false;
}

#endif
