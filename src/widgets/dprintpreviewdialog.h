#ifndef DPRINTPREVIEWDIALOG_H
#define DPRINTPREVIEWDIALOG_H

#include "ddialog.h"

DWIDGET_BEGIN_NAMESPACE

class DPrintPreviewDialogPrivate;
class DPrintPreviewDialog : public DDialog
{
public:
    explicit DPrintPreviewDialog(QWidget *parent = nullptr);

    D_DECLARE_PRIVATE(DPrintPreviewDialog)
};

DWIDGET_END_NAMESPACE

#endif // DPRINTPREVIEWDIALOG_H
