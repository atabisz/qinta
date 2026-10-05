#include "print_pages.h"
#include "utils.h"

#include "render_d2d.h"
// The command-list recording printDocument shares with src/pdf_export_win32.cpp. See the
// header for why it is a header rather than a copy.
#include "xps_page_emitter.h"

#include <commdlg.h>
#include <documenttarget.h>

#include <string>

// THE WINDOWS ARM OF src/print.cpp, AND EVERYTHING IN IT IS WINDOWS BY WHAT IT DOES. Split out
// 2026-10-06 so print.cpp could compile off Windows. This file holds the printer dialog
// (PrintDlgW, DEVMODEW, DEVNAMES), the XPS print job (IPrintDocumentPackageTarget plus
// ID2D1PrintControl through XpsPageEmitter), and the locale's paper question. Nothing here was
// rewritten in the move: printDocument's body is the one print.cpp had, apart from the guard
// at its top and the page-loop names, which now come from src/print_pages.h.
//
// print.cpp's include comment measured this file's surface on 2026-09-20: ONE PrintDlgW, SIX
// IPrintDocumentPackage* symbols, FIVE WIC symbols and ONE SHCreateStreamOnFileEx. The PDF
// seam took the stream and the package target. The WIC symbols were rasterizePreviewPage's
// staging bitmap, and that function is portable now, so what is left is the dialog and the
// print job. It is the arm the cutover deletes.

bool printLocalePrefersLetter() {
    wchar_t measure[2] = L"0";
    return GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IMEASURE, measure, 2) &&
           measure[0] == L'1';
}

// nullptr: app.renderTarget is the window's for as long as the window lives, so the print
// code never finds it missing while there is anything to print. A null here leaves the
// guards in print.cpp refusing, which is what they did before the hook existed.
tinta::RenderTarget* printOffscreenTarget() { return nullptr; }

bool printDocument(App& app) {
    // NEW GUARD, AND IT IS THE ONE THE PREVIEW USED TO SUPPLY. The body below dereferences
    // app.deviceContext without checking it. That was safe only because openPrintPreview
    // refused to open without one, and the preview's raster now goes through the seam and no
    // longer needs a device context. main_d2d.cpp also calls this directly. So the check
    // moves here, where the dereference is.
    if (!app.deviceContext) return false;

    // Standard printer picker; PD_RETURNDC so the paper size can be read
    PRINTDLGW pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = app.hwnd;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;

    // Preset the preview's paper and orientation; whatever the user picks
    // in the dialog still wins (the geometry is re-read from the DC)
    if (app.printPreviewPaper >= 0) {
        pd.hDevMode = GlobalAlloc(GHND, sizeof(DEVMODEW));
        if (pd.hDevMode) {
            auto* dm = (DEVMODEW*)GlobalLock(pd.hDevMode);
            if (dm) {
                dm->dmSize = sizeof(DEVMODEW);
                dm->dmSpecVersion = DM_SPECVERSION;
                dm->dmFields = DM_ORIENTATION | DM_PAPERSIZE;
                dm->dmOrientation = app.printPreviewLandscape
                    ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
                dm->dmPaperSize = PRINT_PAPERS[app.printPreviewPaper].dmPaper;
                GlobalUnlock(pd.hDevMode);
            }
        }
    }

    if (!PrintDlgW(&pd) || !pd.hDC) {
        if (pd.hDevMode) GlobalFree(pd.hDevMode);
        if (pd.hDevNames) GlobalFree(pd.hDevNames);
        return false;  // cancelled
    }

    float dpiX = (float)GetDeviceCaps(pd.hDC, LOGPIXELSX);
    float dpiY = (float)GetDeviceCaps(pd.hDC, LOGPIXELSY);
    PrintPageGeometry geo{};
    geo.pageW = GetDeviceCaps(pd.hDC, PHYSICALWIDTH) * 96.0f / dpiX;
    geo.pageH = GetDeviceCaps(pd.hDC, PHYSICALHEIGHT) * 96.0f / dpiY;
    if (geo.pageW < 200 || geo.pageH < 200) {  // driver returned nonsense
        geo.pageW = 794.0f;   // A4
        geo.pageH = 1123.0f;
    }
    geo.contentW = geo.pageW - kPrintPageMarginDips * 2;
    geo.contentH = geo.pageH - kPrintPageMarginDips * 2;

    // Printer name from the DEVNAMES block
    std::wstring printerName;
    if (pd.hDevNames) {
        auto* names = (DEVNAMES*)GlobalLock(pd.hDevNames);
        if (names) {
            printerName = (wchar_t*)names + names->wDeviceOffset;
            GlobalUnlock(pd.hDevNames);
        }
    }
    DeleteDC(pd.hDC);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    if (printerName.empty()) return false;

    IPrintDocumentPackageTargetFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(PrintDocumentPackageTargetFactory), nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        return false;
    }
    std::wstring jobName = L"Tinta - " + toWide(app.currentFile);
    IPrintDocumentPackageTarget* target = nullptr;
    HRESULT hr = factory->CreateDocumentPackageTargetForPrintJob(
        printerName.c_str(), jobName.c_str(), nullptr, nullptr, &target);
    factory->Release();
    if (FAILED(hr) || !target) return false;

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    ID2D1PrintControl* printControl = nullptr;
    if (device) {
        device->CreatePrintControl(app.wicFactory, target, nullptr, &printControl);
        device->Release();
    }

    bool ok = false;
    if (printControl) {
        XpsPageEmitter emitter(app.deviceContext, printControl);
        int pages = renderPrintPages(app, geo, [&](int, const tinta::PagePainter& paint) {
            return emitter.addPage(geo.pageW, geo.pageH, paint);
        });
        ok = pages > 0 && SUCCEEDED(printControl->Close());
        printControl->Release();
    }
    target->Release();
    return ok;
}
