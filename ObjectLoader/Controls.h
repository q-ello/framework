#pragma once
#include <windows.h>
#include <memory>

// Custom deleter for HWND
struct HWNDDeleter {
    void operator()(HWND hwnd) const {
        if (hwnd) DestroyWindow(hwnd);
    }
};


// Struct to hold control information
struct Control {
    std::shared_ptr<void> hwnd;
    int id;
    const std::wstring name;

    Control(HWND handle, int controlId, const std::wstring& controlName)
        : hwnd(handle, HWNDDeleter()), id(controlId), name(controlName) {
    }
};