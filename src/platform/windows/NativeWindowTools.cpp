#include "../NativeWindowTools.h"

#include <windows.h>

namespace sampler::platform
{
juce::Image NativeWindowTools::snapshot(void* nativeWindowHandle)
{
    auto hwnd = static_cast<HWND>(nativeWindowHandle);
    RECT rect {};
    if (hwnd == nullptr || !GetClientRect(hwnd, &rect))
        return {};

    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0)
        return {};

    juce::Image image(juce::Image::ARGB, w, h, true);
    {
        juce::Image::BitmapData bits(image, juce::Image::BitmapData::readWrite);
        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h; // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (bitmap != nullptr && pixels != nullptr)
        {
            auto old = SelectObject(dc, bitmap);
            // PW_CLIENTONLY | PW_RENDERFULLCONTENT: client area, including child windows that draw themselves.
            PrintWindow(hwnd, dc, 0x1 | 0x2);
            SelectObject(dc, old);

            for (int y = 0; y < h; ++y)
            {
                auto* src = static_cast<const juce::uint32*>(pixels) + static_cast<size_t>(y) * static_cast<size_t>(w);
                for (int x = 0; x < w; ++x)
                    bits.setPixelColour(x, y, juce::Colour(src[x] | 0xff000000u));
            }
        }
        if (bitmap != nullptr)
            DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
    }
    return image;
}

bool NativeWindowTools::postClick(void* nativeWindowHandle, juce::Point<int> clientPosition)
{
    auto top = static_cast<HWND>(nativeWindowHandle);
    if (top == nullptr)
        return false;

    // Walk down to the deepest child at that point, converting the coordinates at each level.
    HWND target = top;
    POINT pt { clientPosition.x, clientPosition.y };
    for (;;)
    {
        HWND child = ChildWindowFromPointEx(target, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (child == nullptr || child == target)
            break;
        MapWindowPoints(target, child, &pt, 1);
        target = child;
    }

    const LPARAM lParam = MAKELPARAM(pt.x, pt.y);
    PostMessage(target, WM_MOUSEMOVE, 0, lParam);
    PostMessage(target, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
    PostMessage(target, WM_LBUTTONUP, 0, lParam);
    return true;
}
} // namespace sampler::platform
