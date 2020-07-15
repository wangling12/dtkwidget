#ifndef DPRINTPREVIEWDIALOG_P_H
#define DPRINTPREVIEWDIALOG_P_H

#include "dprintpreviewdialog.h"
#include "ddialog_p.h"

DWIDGET_BEGIN_NAMESPACE

class DPrintPreviewDialogPrivate : public DDialogPrivate
{
public:
    explicit DPrintPreviewDialogPrivate(DPrintPreviewDialog *qq);

    Q_DECLARE_PUBLIC(DPrintPreviewDialog)
};

DWIDGET_END_NAMESPACE

#endif // DPRINTPREVIEWDIALOG_P_H
