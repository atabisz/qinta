// The Qt half of the file-picker seam (include/file_dialog.h), and the first file
// in this port to need Qt6::Widgets.
//
// WHY WIDGETS, WHICH IS A NEW MODULE FOR THIS TREE
//
// There is no QtGui-only file dialog. QFileDialog lives in QtWidgets, and the only
// other Qt answer is Qt Quick Dialogs, which is QML and would drag in Qml and Quick
// to replace a call that returns a string. So the module is the cost of having a
// real second implementation rather than a stub, and it is stated in CMakeLists.txt
// as a hard requirement off Windows for exactly that reason: unlike Svg and Network,
// which only one optional target needs, this seam has no other non-Windows half.
//
// A QFileDialog needs a QApplication, not merely a QGuiApplication. Nothing in the
// test suite calls the two functions below -- a test cannot click a dialog, which is
// the whole reason the header hoists the filter encodings out where they CAN be
// asserted -- so no test constructs one. The application will have one when tinta's
// window becomes a QWidget, which is the same cutover that makes `owner` meaningful.

#include "file_dialog.h"

#include <QDialog>
#include <QFileDialog>
#include <QString>
#include <QStringList>

namespace platform {
namespace {

QString toQt(const std::wstring& text) { return QString::fromStdWString(text); }

// An instance rather than the static QFileDialog::getSaveFileName helpers, and
// defaultExtension is the reason: the setting that implements it, setDefaultSuffix,
// exists only on an instance. Reaching for the statics would have silently dropped
// the flag at two of the five sites -- editor.cpp:1048 (L"md") and pandoc.cpp:171
// (the format's own extension) -- so a name typed without an extension would land
// on disk without one.
std::optional<FileDialogResult> show(const FileDialogRequest& request, bool save) {
    // nullptr parent: `request.owner` is an HWND and cannot be converted. See the
    // header for why the parameter is kept anyway.
    QFileDialog dialog(nullptr);
    dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);

    // setNameFilter takes the `;;`-separated form the header's builder produces, so
    // the builder is load-bearing here and not merely a thing a test looks at. The
    // dialog splits it, which is why nameFilters() below can be indexed.
    dialog.setNameFilter(toQt(buildQtNameFilter(request.filters)));

    if (!request.defaultExtension.empty()) {
        dialog.setDefaultSuffix(toQt(request.defaultExtension));
    }
    if (!request.initialName.empty()) {
        dialog.selectFile(toQt(request.initialName));
    }

    // OFN_OVERWRITEPROMPT has no field here because it is Qt's default: an
    // AcceptSave dialog confirms an overwrite unless DontConfirmOverwrite is set,
    // and nothing sets it. OFN_FILEMUSTEXIST is the ExistingFile file mode above.
    //
    // restoreDirectory and hideReadOnly are deliberately not consulted. Neither
    // effect exists in Qt -- QFileDialog does not change the process's current
    // directory at all, and it has no read-only checkbox to hide -- so honouring
    // them would mean inventing behaviour Windows has and Qt does not.

    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    QStringList chosen = dialog.selectedFiles();
    if (chosen.isEmpty()) return std::nullopt;

    FileDialogResult result;
    result.path = chosen.first().toStdWString();

    // 1-based, matching OPENFILENAMEW::nFilterIndex, because export_html.cpp
    // compares against 2 and 3. indexOf returns -1 when the selected filter is not
    // in the list, which should be impossible -- Qt returns one of the strings it
    // was given -- and falls back to the first filter, which is what Windows does
    // for nFilterIndex == 0.
    int index = dialog.nameFilters().indexOf(dialog.selectedNameFilter());
    result.filterIndex = index < 0 ? 1 : index + 1;
    return result;
}

}  // namespace

std::optional<FileDialogResult> requestOpenPath(const FileDialogRequest& request) {
    return show(request, false);
}

std::optional<FileDialogResult> requestSavePath(const FileDialogRequest& request) {
    return show(request, true);
}

}  // namespace platform
