#include "dprintpreviewdialog.h"
#include "private/dprintpreviewdialog_p.h"
#include "dframe.h"
#include "diconbutton.h"
#include "dlabel.h"
#include "dlineedit.h"
#include <DSpinBox>
#include <DBackgroundGroup>
#include <DApplicationHelper>
#include <DFontSizeManager>
#include <DScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QButtonGroup>

DWIDGET_BEGIN_NAMESPACE
void setwidgetfont(QWidget *widget, DFontSizeManager::SizeType type = DFontSizeManager::T5)
{
    QFont font = widget->font();
    font.setBold(true);
    widget->setFont(font);
    DFontSizeManager::instance()->bind(widget, type);
}

DPrintPreviewDialogPrivate::DPrintPreviewDialogPrivate(DPrintPreviewDialog *qq)
    : DDialogPrivate(qq)
{

}

void DPrintPreviewDialogPrivate::startup()
{
    initui();
    initconnections();
}

void DPrintPreviewDialogPrivate::initui()
{
    Q_Q(DPrintPreviewDialog);
    DWidget *titleWidget = new DWidget(q);
    titleWidget->setGeometry(0, 0, q->width(), 50);
    DPalette pa = DApplicationHelper::instance()->palette(titleWidget);
    pa.setBrush(DPalette::Base, pa.base());
    DApplicationHelper::instance()->setPalette(titleWidget, pa);
    titleWidget->setAutoFillBackground(true);

    QHBoxLayout *mainlayout = new QHBoxLayout();
    mainlayout->setContentsMargins(QMargins(0, 0, 0, 0));
    mainlayout->setSpacing(0);
    DFrame *pframe = new DFrame;
    pframe->setLayout(mainlayout);

    QVBoxLayout *pleftlayout = new QVBoxLayout;
    initleft(pleftlayout);
    DVerticalLine *pvline = new DVerticalLine;
    QVBoxLayout *prightlayout = new QVBoxLayout;
    initright(prightlayout);
    mainlayout->addLayout(pleftlayout);
    mainlayout->addWidget(pvline);
    mainlayout->addLayout(prightlayout);
    q->addContent(pframe);
}

void DPrintPreviewDialogPrivate::initleft(QVBoxLayout *layout)
{
    pview = new DFrame;
    pview->setFixedSize(364, 470);
    setfrmaeback(pview);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->addWidget(pview);
    layout->setAlignment(pview, Qt::AlignCenter);
    QHBoxLayout *pbottomlayout = new QHBoxLayout;
    pbottomlayout->setContentsMargins(0, 10, 0, 0);
    layout->addLayout(pbottomlayout);
    firstBtn = new DIconButton(DStyle::SP_ArrowPrev);
    prevPageBtn = new DIconButton(DStyle::SP_ArrowLeft);
    jumpPageEdit = new DLineEdit();
    jumpPageEdit->setMaximumWidth(50);
    jumpPageEdit->setClearButtonEnabled(false);
    totalPageLabel = new DLabel("/123");
    nextPageBtn = new DIconButton(DStyle::SP_ArrowRight);
    lastBtn = new DIconButton(DStyle::SP_ArrowNext);
    pbottomlayout->addWidget(firstBtn);
    pbottomlayout->addWidget(prevPageBtn);
    pbottomlayout->addStretch();
    pbottomlayout->addWidget(jumpPageEdit);
    pbottomlayout->addWidget(totalPageLabel);
    pbottomlayout->addStretch();
    pbottomlayout->addWidget(nextPageBtn);
    pbottomlayout->addWidget(lastBtn);
}

void DPrintPreviewDialogPrivate::initright(QVBoxLayout *layout)
{
    Q_Q(DPrintPreviewDialog);
    //top
    QVBoxLayout *ptoplayout = new QVBoxLayout;
    ptoplayout->setContentsMargins(0, 0, 0, 0);
    DWidget *ptopwidget = new DWidget;
    ptopwidget->setFixedWidth(442);
    ptopwidget->setLayout(ptoplayout);

    basicsettingwdg = new DWidget;
    advancesettingwdg = new DWidget;
    scrollarea = new DScrollArea;
    scrollarea->setWidget(advancesettingwdg);
    scrollarea->setWidgetResizable(true);
    scrollarea->setFrameShape(QFrame::NoFrame);
    scrollarea->hide();

    advanceBtn = new DPushButton(q->tr("Advanced"));
    DPalette pa = advanceBtn->palette();
    pa.setColor(DPalette::ButtonText, pa.link().color());
    advanceBtn->setPalette(pa);
    advanceBtn->setFlat(true);
    QHBoxLayout *advancelayout = new QHBoxLayout;
    advancelayout->addWidget(advanceBtn);

    ptoplayout->addWidget(basicsettingwdg);
    ptoplayout->addLayout(advancelayout);
    ptoplayout->addWidget(scrollarea);

    initbasicui();
    initadvanceui();
    //bottom
    QHBoxLayout *pbottomlayout = new QHBoxLayout;
    pbottomlayout->setContentsMargins(0, 10, 0, 10);
    cancelBtn = new DPushButton(q->tr("Cancel"));
    printBtn = new DPushButton(q->tr("Print"));
    cancelBtn->setFixedSize(170, 36);
    printBtn->setFixedSize(170, 36);
    pbottomlayout->addWidget(cancelBtn);
    pbottomlayout->addWidget(printBtn);

    layout->addWidget(ptopwidget);
    layout->addLayout(pbottomlayout);
}

void DPrintPreviewDialogPrivate::initbasicui()
{
    Q_Q(DPrintPreviewDialog);
    QVBoxLayout *layout = new QVBoxLayout(basicsettingwdg);
    layout->setSpacing(10);
    DLabel *basicLabel = new DLabel(q->tr("Basic"), basicsettingwdg);
    QHBoxLayout *basictitlelayout = new QHBoxLayout;
    layout->addLayout(basictitlelayout);
    basictitlelayout->setContentsMargins(0, 0, 0, 0);
    basictitlelayout->addWidget(basicLabel);
    basictitlelayout->setAlignment(basicLabel, Qt::AlignLeft | Qt::AlignBottom);

    QFont font = basicLabel->font();
    font.setBold(true);
    basicLabel->setFont(font);
    DFontSizeManager::instance()->bind(basicLabel, DFontSizeManager::T5);
    //打印机选择
    DFrame *printerFrame = new DFrame(basicsettingwdg);
    layout->addWidget(printerFrame);
    printerFrame->setFixedSize(422, 48);
    setfrmaeback(printerFrame);
    QHBoxLayout *printerlayout = new QHBoxLayout(printerFrame);
    printerlayout->setContentsMargins(10, 0, 10, 0);
    DLabel *printerlabel = new DLabel(q->tr("Printer"), printerFrame);
    printDeviceComBox = new DComboBox(basicsettingwdg);
    printDeviceComBox->setFixedSize(275, 36);
    printerlayout->addWidget(printerlabel);
    printerlayout->addWidget(printDeviceComBox);
//    printerlayout->setAlignment(printerlabel, Qt::AlignVCenter);
    printerlayout->setAlignment(printDeviceComBox, Qt::AlignVCenter);

    //打印份数
    DFrame *copycountFrame = new DFrame(basicsettingwdg);
    layout->addWidget(copycountFrame);
    copycountFrame->setFixedSize(422, 48);
    setfrmaeback(copycountFrame);
    QHBoxLayout *copycountlayout = new QHBoxLayout(copycountFrame);
    copycountlayout->setContentsMargins(10, 0, 10, 0);
    DLabel *copycountlabel = new DLabel(q->tr("Copies"), copycountFrame);
    copycountspinbox = new DSpinBox(copycountFrame);
    copycountspinbox->setEnabledEmbedStyle(true);
    copycountspinbox->setMinimum(1);
    copycountspinbox->setFixedSize(275, 36);
    copycountlayout->addWidget(copycountlabel);
    copycountlayout->addWidget(copycountspinbox);


    //页码范围
    DFrame *pageFrame = new DFrame(basicsettingwdg);
    layout->addWidget(pageFrame);
    pageFrame->setFixedSize(422, 94);
    setfrmaeback(pageFrame);
    QVBoxLayout *pagelayout = new QVBoxLayout(pageFrame);
    DLabel *pagerangelabel = new DLabel(q->tr("Page range"), pageFrame);
    pagerangeBox = new DComboBox(pageFrame);
    pagerangeBox->addItem(q->tr("All"));
    pagerangeBox->addItem(q->tr("Current page"));
    pagerangeBox->addItem(q->tr("Select pages"));
    QHBoxLayout *hrangebox = new QHBoxLayout();
    hrangebox->addWidget(pagerangelabel);
    hrangebox->addWidget(pagerangeBox);

    DLabel *fromLabel = new DLabel(q->tr("From"), pageFrame);
    fromeBox = new DSpinBox(pageFrame);
    DLabel *toLabel = new DLabel(q->tr("To"), pageFrame);
    toBox = new DSpinBox(pageFrame);
    fromeBox->setEnabledEmbedStyle(true);
    toBox->setEnabledEmbedStyle(true);
    QHBoxLayout *hfromtolayout = new QHBoxLayout();
    hfromtolayout->addWidget(fromLabel);
    hfromtolayout->addWidget(fromeBox);
    hfromtolayout->addWidget(toLabel);
    hfromtolayout->addWidget(toBox);
    pagelayout->addLayout(hrangebox);
    pagelayout->addLayout(hfromtolayout);

    //打印方向
    DLabel *orientationLabel = new DLabel(q->tr("Orientation"), basicsettingwdg);
    font = orientationLabel->font();
    font.setBold(true);
    orientationLabel->setFont(font);
    DFontSizeManager::instance()->bind(orientationLabel, DFontSizeManager::T5);
    QHBoxLayout *orientationtitlelayout = new QHBoxLayout;
    orientationtitlelayout->addWidget(orientationLabel);
    orientationtitlelayout->setAlignment(orientationLabel, Qt::AlignLeft | Qt::AlignBottom);
    layout->addLayout(orientationtitlelayout);

    QVBoxLayout *orientationlayout = new QVBoxLayout;
    orientationlayout->setContentsMargins(0, 0, 0, 0);
    verRadio = new DRadioButton;
    horRadio = new DRadioButton;
    orientationgroup = new QButtonGroup(basicsettingwdg);
    orientationgroup->addButton(verRadio, 0);
    orientationgroup->addButton(horRadio, 1);

    //纵向
    DWidget *portraitwdg = new DWidget;
    portraitwdg->setFixedSize(422, 48);
    QHBoxLayout *portraitlayout = new QHBoxLayout;
    DLabel *orientationlabel = new DLabel;
    DLabel *orientationTextLabel = new DLabel(q->tr("Portrait"), portraitwdg);
    portraitlayout->addWidget(verRadio);
    portraitlayout->addWidget(orientationlabel);
    portraitlayout->addWidget(orientationTextLabel);
    portraitwdg->setLayout(portraitlayout);

    //横向
    DWidget *landscapewdg = new DWidget;
    landscapewdg->setFixedSize(422, 48);
    QHBoxLayout *landscapelayout = new QHBoxLayout;
    DLabel *landscapelabel = new DLabel;
    DLabel *landscapeTextLabel = new DLabel(q->tr("Landscape"), portraitwdg);
    landscapelayout->addWidget(horRadio);
    landscapelayout->addWidget(landscapelabel);
    landscapelayout->addWidget(landscapeTextLabel);
    landscapewdg->setLayout(landscapelayout);

    orientationlayout->addWidget(portraitwdg);
    orientationlayout->addWidget(landscapewdg);
    DBackgroundGroup *back = new DBackgroundGroup(orientationlayout);
    back->setItemSpacing(2);
    DPalette pa = DApplicationHelper::instance()->palette(back);
    pa.setBrush(DPalette::Base, pa.itemBackground());
    DApplicationHelper::instance()->setPalette(back, pa);
    layout->addWidget(back);


}

void DPrintPreviewDialogPrivate::initadvanceui()
{
    Q_Q(DPrintPreviewDialog);
    QVBoxLayout *layout = new QVBoxLayout(advancesettingwdg);
    layout->setContentsMargins(0, 0, 0, 0);
    advancesettingwdg->setMinimumSize(422, 800);
    //页面设置
    QVBoxLayout *pagelayout = new QVBoxLayout;
    pagelayout->setSpacing(10);
    pagelayout->setContentsMargins(10, 0, 10, 0);
    DLabel *pagesLabel = new DLabel(q->tr("Pages"), advancesettingwdg);
    setwidgetfont(pagesLabel, DFontSizeManager::T5);
    QHBoxLayout *pagestitlelayout = new QHBoxLayout;
    pagestitlelayout->setContentsMargins(0, 0, 0, 0);
    pagestitlelayout->addWidget(pagesLabel, Qt::AlignLeft | Qt::AlignBottom);

    DFrame  *colorframe = new DFrame;
    setfrmaeback(colorframe);
    colorframe->setFixedHeight(48);

    DFrame *marginsframe = new DFrame;
    setfrmaeback(marginsframe);
    marginsframe->setFixedHeight(102);

    pagelayout->addLayout(pagestitlelayout);
    pagelayout->addWidget(colorframe);
    pagelayout->addWidget(marginsframe);

    layout->addLayout(pagelayout);

    //缩放
    QVBoxLayout *scalinglayout = new QVBoxLayout;
    scalinglayout->setContentsMargins(10, 0, 10, 0);
    DLabel *scalingLabel = new DLabel(q->tr("Scaling"), advancesettingwdg);
    QHBoxLayout *scalingtitlelayout = new QHBoxLayout;
    scalingtitlelayout->setContentsMargins(0, 0, 0, 0);
    scalingtitlelayout->addWidget(scalingLabel, Qt::AlignLeft | Qt::AlignBottom);
    setwidgetfont(scalingLabel, DFontSizeManager::T5);

    QVBoxLayout *scalingcontentlayout = new QVBoxLayout;
    scalingcontentlayout->setContentsMargins(0, 0, 0, 0);
    DWidget *fitwdg = new DWidget;
    fitwdg->setFixedHeight(48);

    DWidget *actualwdg = new DWidget;
    actualwdg->setFixedHeight(48);

    DWidget *shrinkwdg = new DWidget;
    shrinkwdg->setFixedHeight(48);

    DWidget *customscalewdg = new DWidget;
    customscalewdg->setFixedHeight(48);

    scalingcontentlayout->addWidget(fitwdg);
    scalingcontentlayout->addWidget(actualwdg);
    scalingcontentlayout->addWidget(shrinkwdg);
    scalingcontentlayout->addWidget(customscalewdg);

    DBackgroundGroup *back = new DBackgroundGroup(scalingcontentlayout);
    back->setItemSpacing(2);
    DPalette pa = DApplicationHelper::instance()->palette(back);
    pa.setBrush(DPalette::Base, pa.itemBackground());
    DApplicationHelper::instance()->setPalette(back, pa);
    scalinglayout->addLayout(scalingtitlelayout);
    scalinglayout->addWidget(back);
    layout->addLayout(scalinglayout);

    //纸张
    QVBoxLayout *paperlayout = new QVBoxLayout;
    paperlayout->setContentsMargins(10, 0, 10, 0);
    DLabel *paperLabel = new DLabel(q->tr("Paper"), advancesettingwdg);
    setwidgetfont(paperLabel, DFontSizeManager::T5);
    QHBoxLayout *papertitlelayout = new QHBoxLayout;
    papertitlelayout->setContentsMargins(0, 0, 0, 0);
    papertitlelayout->addWidget(paperLabel, Qt::AlignLeft | Qt::AlignBottom);

    DFrame  *paperframe = new DFrame;
    setfrmaeback(paperframe);
    paperframe->setFixedHeight(48);
    paperlayout->addLayout(papertitlelayout);
    paperlayout->addWidget(paperframe);
    layout->addLayout(paperlayout);
    {
        //打印方式
        QVBoxLayout *drawinglayout = new QVBoxLayout;
        drawinglayout->setSpacing(10);
        drawinglayout->setContentsMargins(10, 0, 10, 0);
        DLabel *drawingLabel = new DLabel(q->tr("Layout"), advancesettingwdg);
        setwidgetfont(drawingLabel, DFontSizeManager::T5);
        QHBoxLayout *drawingtitlelayout = new QHBoxLayout;
        drawingtitlelayout->setContentsMargins(0, 0, 0, 0);
        drawingtitlelayout->addWidget(drawingLabel, Qt::AlignLeft | Qt::AlignBottom);

        DFrame  *duplexframe = new DFrame;
        setfrmaeback(duplexframe);
        duplexframe->setFixedHeight(48);

        DFrame  *drawingframe = new DFrame;
        setfrmaeback(drawingframe);
        drawingframe->setFixedHeight(94);


        drawinglayout->addLayout(drawingtitlelayout);
        drawinglayout->addWidget(duplexframe);
        drawinglayout->addWidget(drawingframe);
        layout->addLayout(drawinglayout);
    }
    //打印顺序
//    QVBoxLayout *orderlayout = new QVBoxLayout;
//    orderlayout->setContentsMargins(10, 0, 10, 0);
//    DLabel *orderLabel = new DLabel(q->tr("Page Order"), advancesettingwdg);
//    setwidgetfont(orderLabel, DFontSizeManager::T5);
//    QHBoxLayout *ordertitlelayout = new QHBoxLayout;
//    ordertitlelayout->setContentsMargins(0, 0, 0, 0);
//    ordertitlelayout->addWidget(orderLabel, Qt::AlignLeft | Qt::AlignBottom);



    //水印
//    QVBoxLayout *watermarklayout = new QVBoxLayout;
//    watermarklayout->setContentsMargins(10, 0, 10, 0);
//    DLabel *watermarkLabel = new DLabel(q->tr("Watermark"), advancesettingwdg);
//    QHBoxLayout *watermarktitlelayout = new QHBoxLayout;
//    watermarktitlelayout->setContentsMargins(0, 0, 0, 0);
//    watermarktitlelayout->addWidget(watermarkLabel, Qt::AlignLeft | Qt::AlignBottom);
//    setwidgetfont(watermarkLabel, DFontSizeManager::T5);











}

void DPrintPreviewDialogPrivate::initconnections()
{
    Q_Q(DPrintPreviewDialog);
    QObject::connect(advanceBtn, &QPushButton::clicked, q, &DPrintPreviewDialog::showAdvanceSetting);
}

void DPrintPreviewDialogPrivate::setfrmaeback(DWidget *frame)
{
    Q_Q(DPrintPreviewDialog);
    DPalette pa = DApplicationHelper::instance()->palette(frame);
    pa.setBrush(DPalette::Base, pa.itemBackground());
    DApplicationHelper::instance()->setPalette(frame, pa);
    //frame->setAutoFillBackground(true);
}

void DPrintPreviewDialogPrivate::showadvancesetting()
{
    if (scrollarea->isHidden()) {
        basicsettingwdg->hide();
        scrollarea->show();
    } else {
        basicsettingwdg->show();
        scrollarea->hide();
    }

}

DPrintPreviewDialog::DPrintPreviewDialog(QWidget *parent)
    : DDialog(*new DPrintPreviewDialogPrivate(this), parent)
{
    Q_D(DPrintPreviewDialog);
    setFixedSize(851, 606);
    d->startup();

}

void DPrintPreviewDialog::showAdvanceSetting()
{
    Q_D(DPrintPreviewDialog);
    d->showadvancesetting();
}

DWIDGET_END_NAMESPACE
