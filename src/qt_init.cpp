#include "qt_init.h"

#include <memory>

#include "app.h"
#include "render_qt.h"
#include "text_qt.h"

// The first callers of Seated<>::install() in the tree. Measured before they were
// written: a scratch probe on 2026-09-16 built a QApplication + App + QWidget that
// installed a QtRenderTarget and a QtTextFactory through these two storages, linked
// with zero undefined symbols, and read the painted pixels back out of
// QWidget::grab() -- amber inside the filled rectangle, the Clear colour outside.
// Two distinct Seated<> instantiations, so one working said nothing about the other
// and both were exercised.

void installQtBackends(App& app) {
    app.textFactory = app.textFactoryStorage.install(std::make_unique<QtTextFactory>());
}

void releaseQtBackends(App& app) {
    // The raw pointer goes first. If install() ran the destructor before the
    // pointer was cleared, App would hold a dangling tinta::TextFactory* for the
    // length of one statement -- harmless today and exactly the kind of ordering
    // that stops being harmless when a destructor grows.
    app.textFactory = nullptr;
    app.textFactoryStorage.install(nullptr);
}

QtScopedRenderTarget::QtScopedRenderTarget(App& app, QPaintDevice& device) : app_(app) {
    target_ = app_.renderTargetStorage.install(std::make_unique<QtRenderTarget>(device));
    app_.renderTarget = target_;
}

QtScopedRenderTarget::~QtScopedRenderTarget() {
    app_.renderTarget = nullptr;
    // This is what ends the QPainter: ~QtRenderTarget calls QPainter::end(), and it
    // runs here because the storage releases the wrapper it is holding.
    app_.renderTargetStorage.install(nullptr);
}
