#ifndef TINTA_XPS_PAGE_EMITTER_H
#define TINTA_XPS_PAGE_EMITTER_H

// WINDOWS-ONLY INTERNAL HEADER, AND NOT A SEAM. It lives under src/ rather than
// include/ to say that: nothing outside this tree's two XPS consumers may include it,
// it names Direct2D types freely, and it has no second implementation and never will.
// The seam is include/pdf_export.h; this is the shared body of one arm of it.
//
// WHY IT EXISTS: exactly two callers record a painted page into an ID2D1CommandList and
// hand it to an ID2D1PrintControl -- printDocument (src/print.cpp, driving a printer
// chosen in PrintDlgW) and the Win32 PDF backend (src/pdf_export_win32.cpp, driving the
// "Microsoft Print to PDF" driver at a file stream). The two differ entirely in how they
// obtain the print control and not at all in what they do with it. When this struct lived
// in print.cpp's anonymous namespace, moving it into the PDF backend would have orphaned
// printDocument and copying it would have recreated the duplication the renderPages
// inversion was written to avoid. One definition, two consumers, and the grep for
// `struct XpsPageEmitter` returns 1 -- which is a criterion in that slice's ISA rather
// than an aspiration here.
//
// IT IS ALSO A DELETION UNIT. printDocument moves to QPrintDialog + QPrinter and the PDF
// backend gains a QPdfWriter arm; when the second of those lands, this file is deleted
// whole rather than found in two places.

#include <d2d1.h>
#include <d2d1_1.h>  // ID2D1Device, ID2D1DeviceContext, ID2D1CommandList, ID2D1PrintControl

#include "geometry.h"
#include "pdf_export.h"  // tinta::PagePainter -- the one spelling of the page callback
#include "render_d2d.h"

// Records a painted page into an ID2D1CommandList and adds it to an ID2D1PrintControl.
//
// Note what is being wrapped in addPage: the device context's target is an
// ID2D1CommandList, so the drawing the seam records here is replayed later by
// ID2D1PrintControl. The seam neither knows nor cares -- it holds an ID2D1RenderTarget*
// and forwards.
struct XpsPageEmitter {
    ID2D1DeviceContext* dc = nullptr;
    ID2D1PrintControl* control = nullptr;

    // `sourceContext` is only a route to the device -- a context is asked for its
    // ID2D1Device and a SECOND context is created off that device for the recording.
    // It used to be reached as app.deviceContext through an App& parameter, and taking
    // the context directly is what lets this header stay out of app.h and therefore out
    // of windows.h's orbit. Both callers pass a context they already hold.
    XpsPageEmitter(ID2D1DeviceContext* sourceContext, ID2D1PrintControl* printControl)
        : control(printControl) {
        ID2D1Device* device = nullptr;
        if (sourceContext) sourceContext->GetDevice(&device);
        if (device) {
            device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
            device->Release();
        }
    }
    ~XpsPageEmitter() {
        if (dc) dc->Release();
    }
    XpsPageEmitter(const XpsPageEmitter&) = delete;
    XpsPageEmitter& operator=(const XpsPageEmitter&) = delete;

    // Page size in DIPs rather than a PageGeometry, because PageGeometry is a
    // print.cpp-local struct and hoisting it here would have moved a layout type into a
    // Direct2D header to save two floats at two call sites.
    bool addPage(float pageW, float pageH, const tinta::PagePainter& paint) {
        if (!dc || !control) return false;
        ID2D1CommandList* commandList = nullptr;
        if (FAILED(dc->CreateCommandList(&commandList)) || !commandList) return false;
        dc->SetTarget(commandList);
        dc->BeginDraw();
        dc->Clear(tinta::colorF(1.0f, 1.0f, 1.0f));
        ID2D1SolidColorBrush* brush = nullptr;
        dc->CreateSolidColorBrush(tinta::colorF(0, 0, 0), &brush);
        if (brush) {
            D2DRenderTarget seamTarget(dc);
            D2DBrush seamBrush(brush);
            paint(seamTarget, &seamBrush);
            brush->Release();
        }
        bool ok = SUCCEEDED(dc->EndDraw()) && SUCCEEDED(commandList->Close());
        if (ok) {
            ok = SUCCEEDED(control->AddPage(
                commandList, tinta::sizeF(pageW, pageH), nullptr));
        }
        commandList->Release();
        dc->SetTarget(nullptr);
        return ok;
    }
};

#endif  // TINTA_XPS_PAGE_EMITTER_H
