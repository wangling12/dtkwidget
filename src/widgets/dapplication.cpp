/*
 * Copyright (C) 2015 ~ 2017 Deepin Technology Co., Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QtGlobal>
#ifdef Q_OS_LINUX
#ifdef private
#undef private
#endif
#define private public
#include <QWidget>
#undef private
#endif
#include <QDebug>
#include <QDir>
#include <QLocalSocket>
#include <QLibraryInfo>
#include <QTranslator>
#include <QLocalServer>
#include <QPixmapCache>
#include <QProcess>
#include <QMenu>
#include <QStyleFactory>
#include <QSystemSemaphore>
#include <QtConcurrent/QtConcurrent>

#include <qpa/qplatformintegrationfactory_p.h>
#include <qpa/qplatforminputcontext.h>
#include <private/qapplication_p.h>
#include <private/qcoreapplication_p.h>
#include <private/qwidget_p.h>

#include <DStandardPaths>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

#include "dapplication.h"
#include "dthememanager.h"
#include "private/dapplication_p.h"
#include "daboutdialog.h"
#include "dmainwindow.h"

#include <DPlatformHandle>
#include <DGuiApplicationHelper>

#ifdef Q_OS_UNIX
#include <QDBusError>
#include <QDBusReply>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#endif

#ifdef Q_OS_LINUX
#include "startupnotificationmonitor.h"

#include <DDBusSender>

#include <QGSettings>
#endif

#define DXCB_PLUGIN_KEY "dxcb"
#define DXCB_PLUGIN_SYMBOLIC_PROPERTY "_d_isDxcb"
#define QT_THEME_CONFIG_PATH "D_QT_THEME_CONFIG_PATH"

DCORE_USE_NAMESPACE

DWIDGET_BEGIN_NAMESPACE

class LoadManualServiceWorker : public QThread
{
public:
    explicit LoadManualServiceWorker(QObject *parent = nullptr);
    ~LoadManualServiceWorker() override;
    void checkManualServiceWakeUp();

protected:
    void run() override;
};

LoadManualServiceWorker::LoadManualServiceWorker(QObject *parent)
    : QThread(parent)
{
    if (!parent)
        connect(qApp, &QApplication::aboutToQuit, this, std::bind(&LoadManualServiceWorker::exit, this, 0));
}

LoadManualServiceWorker::~LoadManualServiceWorker()
{
}

void LoadManualServiceWorker::run()
{
    QDBusInterface("com.deepin.Manual.Search",
                   "/com/deepin/Manual/Search",
                   "com.deepin.Manual.Search");
}

void LoadManualServiceWorker::checkManualServiceWakeUp()
{
    if (this->isRunning())
        return;

    start();
}

DApplicationPrivate::DApplicationPrivate(DApplication *q) :
    DObjectPrivate(q)
{
#ifdef Q_OS_LINUX
    StartupNotificationMonitor *monitor = StartupNotificationMonitor::instance();
    auto cancelNotification = [this, q](const QString id) {
        m_monitoredStartupApps.removeAll(id);
        if (m_monitoredStartupApps.isEmpty()) {
            q->restoreOverrideCursor();
        }
    };
    QObject::connect(monitor, &StartupNotificationMonitor::appStartup,
                     q, [this, q, cancelNotification](const QString id) {
        // FIX bug start app quikly cursor will not restore...
        // Every setOverrideCursor() must eventually be followed by a corresponding restoreOverrideCursor(),
        // otherwise the stack will never be emptied.
        if (m_monitoredStartupApps.isEmpty()) {
            q->setOverrideCursor(Qt::WaitCursor);
        }
        m_monitoredStartupApps.append(id);
        // Set a timeout of 5s in case that some apps like pamac-tray started
        // with StartupNotify but don't show a window after startup finished.
        QTimer::singleShot(5 * 1000, q, [id, cancelNotification](){
            cancelNotification(id);
        });
    });
    QObject::connect(monitor, &StartupNotificationMonitor::appStartupCompleted,
                     q, cancelNotification);
#endif

    // If not in dde and not use deepin platform theme, force set style to chameleon.
    if (!DGuiApplicationHelper::testAttribute(DGuiApplicationHelper::Attribute::IsDeepinPlatformTheme) &&
        !DGuiApplicationHelper::testAttribute(DGuiApplicationHelper::Attribute::IsDeepinEnvironment)) {
        DApplication::setStyle("chameleon");
        q->setPalette(DGuiApplicationHelper::instance()->applicationPalette());
    }
}

DApplicationPrivate::~DApplicationPrivate()
{
    if (m_localServer) {
        m_localServer->close();
    }

    while (q_func()->overrideCursor()) {
        q_func()->restoreOverrideCursor();
    }

}

QString DApplicationPrivate::theme() const
{
    return DThemeManager::instance()->theme();
}

void DApplicationPrivate::setTheme(const QString &theme)
{
    DThemeManager *themeManager = DThemeManager::instance();
    themeManager->setTheme(theme);
}

static bool tryAcquireSystemSemaphore(QSystemSemaphore *ss, qint64 timeout = 10)
{
    if (ss->error() != QSystemSemaphore::NoError) {
        return false;
    }

    QSystemSemaphore _tmp_ss(QString("%1-%2").arg("DTK::tryAcquireSystemSemaphore").arg(ss->key()), 1, QSystemSemaphore::Open);

    _tmp_ss.acquire();

    QElapsedTimer t;
    QFuture<bool> request = QtConcurrent::run(ss, &QSystemSemaphore::acquire);

    t.start();

    while (Q_LIKELY(t.elapsed() < timeout && !request.isFinished()));

    if (request.isFinished()) {
        return true;
    }

    if (Q_LIKELY(request.isRunning())) {
        if (Q_LIKELY(ss->release(1))) {
            request.waitForFinished();
        }
    }

    return false;
}

bool DApplicationPrivate::setSingleInstanceBySemaphore(const QString &key)
{
    static QSystemSemaphore ss(key, 1, QSystemSemaphore::Open);
    static bool singleInstance = false;

    if (singleInstance) {
        return true;
    }

    Q_ASSERT_X(ss.error() == QSystemSemaphore::NoError, "DApplicationPrivate::setSingleInstanceBySemaphore:", ss.errorString().toLocal8Bit().constData());

    singleInstance = tryAcquireSystemSemaphore(&ss);

    if (singleInstance) {
        QtConcurrent::run([this] {
            QPointer<DApplication> that = q_func();

            while (ss.acquire() && singleInstance)
            {
                if (!that) {
                    return;
                }

                if (that->startingUp() || that->closingDown()) {
                    break;
                }

                ss.release(1);

                if (that) {
                    Q_EMIT that->newInstanceStarted();
                }
            }
        });

        auto clean_fun = [] {
            ss.release(1);
            singleInstance = false;
        };

        qAddPostRoutine(clean_fun);
        atexit(clean_fun);
    }

    return singleInstance;
}

#ifdef Q_OS_UNIX
/**
* \brief DApplicationPrivate::setSingleInstanceByDbus will check singleinstance by
* register dbus service
* \param key is the last of dbus service name, like "com.deepin.SingleInstance.key"
* \return
*/
bool DApplicationPrivate::setSingleInstanceByDbus(const QString &key)
{
    auto basename = "com.deepin.SingleInstance.";
    QString name = basename + key;
    auto sessionBus = QDBusConnection::sessionBus();
    if (!sessionBus.registerService(name)) {
        qDebug() << "register service failed:" << sessionBus.lastError();
        return  false;
    }
    return true;
}
#endif

bool DApplicationPrivate::loadDtkTranslator(QList<QLocale> localeFallback)
{
    D_Q(DApplication);

    auto qtTranslator = new QTranslator(q);
    qtTranslator->load("qt_" + QLocale::system().name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    q->installTranslator(qtTranslator);

    auto qtbaseTranslator = new QTranslator(q);
    qtTranslator->load("qtbase_" + QLocale::system().name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    q->installTranslator(qtbaseTranslator);

    QList<DPathBuf> translateDirs;
    auto dtkwidgetDir = DWIDGET_TRANSLATIONS_DIR;
    auto dtkwidgetName = "dtkwidget";

    //("/home/user/.local/share", "/usr/local/share", "/usr/share")
    auto dataDirs = DStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const auto &path : dataDirs) {
        DPathBuf DPathBuf(path);
        translateDirs << DPathBuf / dtkwidgetDir;
    }

    DPathBuf runDir(q->applicationDirPath());
    translateDirs << runDir.join("translations");

    DPathBuf currentDir(QDir::currentPath());
    translateDirs << currentDir.join("translations");
    
    translateDirs << DPathBuf(":/translations");

#ifdef DTK_STATIC_TRANSLATION
    translateDirs << DPathBuf(":/dtk/translations");
#endif

    return loadTranslator(translateDirs, dtkwidgetName, localeFallback);
}

bool DApplicationPrivate::loadTranslator(QList<DPathBuf> translateDirs, const QString &name, QList<QLocale> localeFallback)
{
    D_Q(DApplication);

    QStringList missingQmfiles;
    for (auto &locale : localeFallback) {
        QString translateFilename = QString("%1_%2").arg(name).arg(locale.name());
        for (auto &path : translateDirs) {
            QString translatePath = (path / translateFilename).toString();
            if (QFile::exists(translatePath + ".qm")) {
                qDebug() << "load translate" << translatePath;
                auto translator = new QTranslator(q);
                translator->load(translatePath);
                q->installTranslator(translator);
                return true;
            }
        }

        // fix english does not need to translation..
        if (locale.language() != QLocale::English) {
            missingQmfiles << translateFilename + ".qm";
        }

        QStringList parseLocalNameList = locale.name().split("_", QString::SkipEmptyParts);
        if (parseLocalNameList.length() > 0) {
            translateFilename = QString("%1_%2").arg(name)
                                .arg(parseLocalNameList.at(0));
            for (auto &path : translateDirs) {
                QString translatePath = (path / translateFilename).toString();
                if (QFile::exists(translatePath + ".qm")) {
                    qDebug() << "translatePath after feedback:" << translatePath;
                    auto translator = new QTranslator(q);
                    translator->load(translatePath);
                    q->installTranslator(translator);
                    return true;
                }
            }
        }

        // fix english does not need to translation..
        if (locale.language() != QLocale::English) {
            missingQmfiles << translateFilename + ".qm";
        }
    }

    if (missingQmfiles.size() > 0) {
        qWarning() << name << "can not find qm files" << missingQmfiles;
    }
    return false;
}

// 自动激活DMainWindow类型的窗口
void DApplicationPrivate::_q_onNewInstanceStarted()
{
    if (!autoActivateWindows)
        return;

    for (QWidget *window : qApp->topLevelWidgets()) {
        if (qobject_cast<DMainWindow*>(window)) {
            // 如果窗口最小化或者隐藏了，應當先將其show出來
            if (window->isMinimized() || window->isHidden())
                window->showNormal();

            window->activateWindow();
            break; // 只激活找到的第一个窗口
        }
    }
}

void DApplicationPrivate::doAcclimatizeVirtualKeyboard(QWidget *window, QWidget *widget, bool allowResizeContentsMargins)
{
    // 如果新激活的输入窗口跟已经在处理中的窗口不一致，则恢复旧窗口的状态
    if (activeInputWindow && activeInputWindow != window) {
        activeInputWindow->setContentsMargins(activeInputWindowContentsMargins);
        activeInputWindow = nullptr;
    }

    auto platform_context = QGuiApplicationPrivate::platformIntegration()->inputContext();
    auto input_method = QGuiApplication::inputMethod();
    // 先检查输入面板的状态
    if (!platform_context->inputMethodAccepted() || !input_method->isVisible()) {
        if (activeInputWindow) {
            activeInputWindow->setContentsMargins(activeInputWindowContentsMargins);
            activeInputWindow = nullptr;
        }

        return;
    }

    // 如果输入控件的主窗口处于未激活状态则忽略
    if (!window->isActiveWindow())
        return;

    // 虚拟键盘相对于当前窗口的geometry
    const QRectF &kRect = input_method->keyboardRectangle().translated(-window->mapToGlobal(QPoint(0, 0)));
    if (kRect.isEmpty() || !kRect.isValid())
        return;

    // 记录待处理窗口的数据
    if (!activeInputWindow) {
        activeInputWindow = window;
        activeInputWindowContentsMargins = window->contentsMargins();
        lastContentsMargins = qMakePair(0, 0);
    }

    // 可以被压缩的高度
    int resizeableHeight = lastContentsMargins.first;
    // 将要平移的距离
    int panValue = 0;

    const QRectF &cRect = input_method->cursorRectangle();
    const QRectF &iRect = input_method->inputItemClipRectangle();
    const QRectF &wRect = window->rect().marginsRemoved(activeInputWindowContentsMargins);

    if (allowResizeContentsMargins) {
        // 判断输入控件是否处于一个可滚动区域中
        QWidget *scrollWidget = widget;
        while (scrollWidget && !qobject_cast<QAbstractScrollArea*>(scrollWidget)) {
            scrollWidget = scrollWidget->parentWidget();
        }

        if (QAbstractScrollArea *scrollArea = qobject_cast<QAbstractScrollArea*>(scrollWidget)) {
            resizeableHeight = scrollArea->maximumViewportSize().height()
                    // 至少要保证可滚动区域的最小高度，以及光标的可显示区域
                    - qMax(scrollArea->minimumHeight(), qRound(cRect.height()));
            resizeableHeight = qMax(0, resizeableHeight);
        }
    }

    // 优先保证输入框下面的内容都能正常显示
    int shadowHeight = wRect.bottom() - kRect.top();
    if (shadowHeight > cRect.y()) {
        // 如果窗口底部的内容必须要被遮挡，则优先保证能完整显示整个输入区域，且最低限度是要显示输入光标
        shadowHeight = qMin(cRect.y(), iRect.bottom() - kRect.top());
    }
    // 如果输入区域没有被盖住则忽略
    if (shadowHeight <= 0)
        return;

    // 更新需要平移的区域
    int resizeHeight = qMin(resizeableHeight, shadowHeight);
    panValue = shadowHeight - resizeHeight;

    if (lastContentsMargins.first == resizeableHeight
            && lastContentsMargins.second == panValue) {
        return;
    }

    // 记录本次的计算结果
    lastContentsMargins.first = resizeableHeight;
    lastContentsMargins.second = panValue;

    // 更新窗口内容显示区域以确保虚拟键盘能正常显示
    window->setContentsMargins(0, -panValue, 0, resizeHeight + panValue);
}

void DApplicationPrivate::acclimatizeVirtualKeyboardForFocusWidget(bool allowResizeContentsMargins)
{
    auto focus = QApplication::focusWidget();
    if (!focus)
        return;

    for (auto window : acclimatizeVirtualKeyboardWindows) {
        if (window->isAncestorOf(focus)) {
            return doAcclimatizeVirtualKeyboard(window ,focus, allowResizeContentsMargins);
        }
    }
}

void DApplicationPrivate::_q_panWindowContentsForVirtualKeyboard()
{
    acclimatizeVirtualKeyboardForFocusWidget(false);
}

void DApplicationPrivate::_q_resizeWindowContentsForVirtualKeyboard()
{
    // TODO(zccrs): 暂时不支持压缩窗口高度适应虚拟键盘的模式
    // 需要做到ScrollArea中的输入控件能在改变窗口高度之后还处于可见状态
    acclimatizeVirtualKeyboardForFocusWidget(false);
}

bool DApplicationPrivate::isUserManualExists()
{
#ifdef Q_OS_LINUX
    auto loadManualFromLocalFile = [=]() -> bool {
        const QString appName = qApp->applicationName();
        bool dmanAppExists = QFile::exists("/usr/bin/dman");
        bool dmanDataExists = false;
        // search all subdirectories
        QString strManualPath = "/usr/share/deepin-manual";
        QDirIterator it(strManualPath, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFileInfo file(it.next());
            if (file.isDir() && file.fileName().contains(appName, Qt::CaseInsensitive)) {
                dmanDataExists = true;
                break;
            }

            if (file.isDir())
                continue;
        }
        return  dmanAppExists && dmanDataExists;
    };

    QDBusConnection conn = QDBusConnection::sessionBus();
    if (conn.interface()->isServiceRegistered("com.deepin.Manual.Search")) {
        QDBusInterface manualSearch("com.deepin.Manual.Search",
                                    "/com/deepin/Manual/Search",
                                    "com.deepin.Manual.Search");
        if (manualSearch.isValid()) {
            QDBusReply<bool> reply = manualSearch.call("ManualExists", qApp->applicationName());
            return reply.value();
        } else {
            return loadManualFromLocalFile();
        }
    } else {
        static LoadManualServiceWorker *manualWorker = new LoadManualServiceWorker;
        manualWorker->checkManualServiceWakeUp();

        return loadManualFromLocalFile();
    }
#else
    return false;
#endif
}



/*!
 * \~chinese \class DApplication
 *
 * \~chinese \brief DApplication 是 DTK 中用于替换 QCoreApplication 相关功能实现的类。
 * \~chinese 继承自 QApplication ，并在此之上添加了一些特殊的设定，如：
 * \~chinese - 在 FORCE_RASTER_WIDGETS 宏生效的情况下，默认设置 Qt::AA_ForceRasterWidgets 以减少 glx 相关库的加载，减少程序启动时间；
 * \~chinese - 自动根据 applicationName 和 系统 locale 加载对应的翻译文件；
 * \~chinese - 会根据系统gsettings中 com.deepin.dde.dapplication 的 qpixmapCacheLimit 值来设置 QPixmapCache::cacheLimit ；
 * \~chinese - 会根据系统gsettings中 com.deepin.dde.touchscreen longpress-duration 的值来设置 QTapAndHoldGesture::timeout ；
 * \~chinese - 方便地通过 setSingleInstance 来实现程序的单实例。
 *
 * \~chinese \note DApplication 设置的 QTapAndHoldGesture::timeout 会比 gsettings
 * \~chinese 中的值小 100，用来绕过 Dock 长按松开容易导致应用启动的问题，详细解释见
 * \~chinese 见代码注释或者 https://github.com/linuxdeepin/internal-discussion/issues/430
 *
 * \~chinese \sa loadTranslator, setSingleInstance.
 */


/*!
 * \~chinese \fn DApplication::newInstanceStarted()
 * \~chinese \brief newInstanceStarted 信号会在程序的一个新实例启动的时候被触发。
 *
 * \~chinese \fn DApplication::iconThemeChanged()
 * \~chinese \brief iconThemeChanged 信号会在系统图标主题发生改变的时候被触发。
 *
 * \~chinese \fn DApplication::screenDevicePixelRatioChanged(QScreen *screen)
 * \~chinese \brief screenDevicePixelRatioChanged 信号会在对应屏幕的缩放比可能发现变化
 * \~chinese 时触发。
 *
 * \~chinese 依赖于 deepin 平台主题插件（dde-qt5integration 包中提供），实时更改
 * \~chinese 屏幕缩放比是通过更改配置文件 ~/.config/deepin/qt-theme.ini 实现，与此相关的
 * \~chinese 配置项有三个：
 * \~chinese - ScreenScaleFactors：多屏幕设置不同缩放比，值格式和环境变量QT_SCREEN_SCALE_FACTORS一致
 * \~chinese - ScaleFactor: 设置所有屏幕缩放比，值格式和环境变量QT_SCALE_FACTOR一致
 * \~chinese - ScaleLogcailDpi：指定屏幕逻辑dpi，可影响仅设置了 point size 的 QFont 的绘制大小。
 * \~chinese 未设置此值时，默认会在 \a ScreenScaleFactors 值改变后将屏幕逻辑dpi更改为主屏默认值，一般情况下，不需要设置此值。
 * \~chinese \a ScreenScaleFactors 和 \a ScaleFactor 的值改变后，会触发所有屏幕的 QScreen::geometryChanged, 且会根据当前缩放
 * \~chinese 更新所有QWindow的geometry（更新时保持窗口的真实大小不变，新窗口大小=窗口真实大小/新的缩放比）。另外，可在构造
 * \~chinese DApplication 对象之前设置 \a Qt::AA_DisableHighDpiScaling 为 true，或添加环境变量 \a D_DISABLE_RT_SCREEN_SCALE
 * \~chinese 禁用实时缩放的支持。
 *
 * \~chinese \sa QScreen::devicePixelRatio
 */

/**
 * \~english @brief DApplication::DApplication constructs an instance of DApplication.
 * \~english @param argc is the same as in the main function.
 * \~english @param argv is the same as in the main function.
 *
 *
 * \~chinese \brief DApplication::DApplication 用于构建 DApplication 实例的构造函数
 *
 * \~chinese 对象构造时会判断环境变量 DTK_FORCE_RASTER_WIDGETS 的值，如果为 TRUE 则开启
 * \~chinese Qt::AA_ForceRasterWidgets，为 FALSE 则不开启，当没有设置此环境变量时，如果
 * \~chinese 编译时使用了宏 FORCE_RASTER_WIDGETS（龙芯和申威平台默认使用），则开启
 * \~chinese Qt::AA_ForceRasterWidgets，否则不开启。
 * \~chinese \param argc 作用同 QApplication::QApplication 参数 argc。
 * \~chinese \param argv 作用同 QApplication::QApplication 参数 argv。
 */

/*!
 * \~chinese \brief DApplication::globalApplication 返回一个DApplicatioin实例
 * \~chinese 如果在执行此函数之前DApplication已经被创建则返回已存在的实例，否则直接创建一个
 * \~chinese 新的DApplication实例并返回。主要用于与deepin-trubo服务相配合，用于共享
 * \~chinese deepin-turbo dtkwidget booster中已经创建的DApplication对象，以此节省初始化时间。
 * \~chinese \param argc 传递给DApplication的构造函数
 * \~chinese \param argv 传递给DApplication的构造函数
 * \~chinese \return 返回一个DApplication对象
 * \~chinese \warning 不保证获取的DApplication对象一定有效，如果实例已存在，则直接使
 * \~chinese 用static_case将其转换为DApplication对象
 */
DApplication *DApplication::globalApplication(int &argc, char **argv)
{
    if (instance()) {
        auto dp = static_cast<QCoreApplicationPrivate*>(qApp->QCoreApplication::d_ptr.data());
        Q_ASSERT(dp);

        // 清理QGuiApplication库的命令行参数
        int j = argc ? 1 : 0;
        const QByteArrayList qt_command = {
            "-platformpluginpath",
            "-platform",
            "-platformtheme",
            "-qwindowgeometry",
            "-geometry",
            "-qwindowtitle",
            "-title",
            "-qwindowicon",
            "-icon",
            "-plugin",
            "-reverse",
            "-session",
            "-style"
        };
        for (int i = 1; i < argc; i++) {
            if (!argv[i])
                continue;
            if (*argv[i] != '-') {
                argv[j++] = argv[i];
                continue;
            }
            const char *arg = argv[i];
            if (arg[1] == '-') // startsWith("--")
                ++arg;
            if (qt_command.indexOf(arg) >= 0) {
                // 跳过这个参数的值
                ++i;
            } else if (strcmp(arg, "-testability") != 0
                       && strncmp(arg, "-style=", 7) != 0) {
                argv[j++] = argv[i];
            }
        }

        if (j < argc) {
            argv[j] = nullptr;
            argc = j;
        }

        dp->argc = argc;
        dp->argv = argv;
        dp->processCommandLineArguments();
        static_cast<QApplicationPrivate*>(dp)->process_cmdline();
        return qApp;
    }

    return new DApplication(argc, argv);
}

DApplication::DApplication(int &argc, char **argv) :
    QApplication(argc, argv),
    DObject(*new DApplicationPrivate(this))
{
    qputenv("QT_QPA_PLATFORM", QByteArray());

    // FIXME: fix bug in nvidia prime workaround, do not know effoct, must test more!!!
    // 在龙芯和申威上，xcb插件中加载glx相关库（r600_dri.so等）会额外耗时1.xs（申威应该更长）
    if (
#ifdef FORCE_RASTER_WIDGETS
            QLatin1String("FALSE") !=
#else
            QLatin1String("TRUE") ==
#endif
    qgetenv("DTK_FORCE_RASTER_WIDGETS")) {
        setAttribute(Qt::AA_ForceRasterWidgets);
    }

#ifdef Q_OS_LINUX
    // set qpixmap cache limit
    if (QGSettings::isSchemaInstalled("com.deepin.dde.dapplication"))
    {
        QGSettings gsettings("com.deepin.dde.dapplication", "/com/deepin/dde/dapplication/");
        if (gsettings.keys().contains("qpixmapCacheLimit"))
            QPixmapCache::setCacheLimit(gsettings.get("qpixmap-cache-limit").toInt());
    }

    // set QTapAndHoldGesture::timeout
    if (QGSettings::isSchemaInstalled("com.deepin.dde.touchscreen")) {
        QGSettings gsettings("com.deepin.dde.touchscreen");
        if (gsettings.keys().contains("longpressDuration"))
            // NOTE(hualet): -100 is a workaround against the situation that that sometimes longpress
            // and release on Dock cause App launches which should be avoided.
            //
            // I guess it happens like this: longpress happens on Dock,
            // Dock menu shows(doesn't grab the mouse which is a bug can't be fixed easily),
            // user ends the longpress, DDE Dock recevies mouseReleaseEvent and checks for
            // QTapAndHoldGesture  which is still not happening (maybe because the timer used is
            // a CoarseTimer?), so Dock treats the event as a normal mouseReleaseEvent, launches the
            // App or triggers the action.
            //
            // see: https://github.com/linuxdeepin/internal-discussion/issues/430
            //
            // This workaround hopefully can fix most of this situations.
            QTapAndHoldGesture::setTimeout(gsettings.get("longpress-duration").toInt() - 100);
    }
#endif
}



/*!
 *
 * \~chinese \enum DApplication::SingleScope
 * \~chinese DApplication::SingleScope 定义了 DApplication 单实例的效应范围。
 *
 * \~chinese \var DApplication::SingleScope DApplication::UserScope
 * \~chinese 代表单实例的范围为用户范围，即同一个用户会话中不允许其他实例出现。
 *
 * \~chinese \var DApplication::SingleScope DApplication::SystemScope
 * \~chinese 代表单实例的范围为系统范围，当前系统内只允许一个程序实例运行。
 */



/**
 * \~english @brief DApplication::theme returns name of the theme that the application is currently using.
 *
 * \~english theme name can be one of light, dark, semidark or semilight.
 *
 * \~english @return the theme name.
 *
 *
 * \~chinese \property DApplication::theme
 * \~chinese \brief theme 属性表示当前程序使用的主题名称，目前可选的主题名称有 light、dark、semidark 和 semilight。
 */
QString DApplication::theme() const
{
    D_DC(DApplication);

    return d->theme();
}

/**
 * @brief DApplication::setTheme for the application to use the theme we provide.
 * @param theme is the name of the theme we want to set.
 */
void DApplication::setTheme(const QString &theme)
{
    D_D(DApplication);

    d->setTheme(theme);
}

#ifdef Q_OS_UNIX
/*!
 * @brief DApplication::setOOMScoreAdj set Out-Of-Memory score
 * @param score vaild range is [-1000, 1000]
 *
 * \~chinese \brief DApplication::setOOMScoreAdj setOOMScoreAdj 用于调整当前进程的
 * \~chinse Out-Of-Memory 分数（linux环境下），这个分数影响了内核在系统资源（内存）不足的
 * \~chinse 情况下为了释放内存资源而挑选进程杀掉的算法，分数越高越容易被杀。
 * \~chinese \param score 指定 oom-score，范围为 [-1000, 1000]。
 */
void DApplication::setOOMScoreAdj(const int score)
{
    if (score > 1000 || score < -1000)
        qWarning() << "OOM score adjustment value out of range: " << score;

    QFile f("/proc/self/oom_score_adj");
    if (!f.open(QIODevice::WriteOnly))
    {
        qWarning() << "OOM score adjust failed, open file error: " << f.errorString();
        return;
    }

    f.write(std::to_string(score).c_str());
}
#endif

/**
 * \~chinese \brief DApplication::setSingleInstance setSingleInstance 用于将程序
 * \~chinese 设置成单实例。
 * \~chinese \param key 是确定程序唯一性的ID，一般使用程序的二进制名称即可。
 *
 * \~chinese \note 一般情况下单实例的实现使用 QSystemSemaphore，如果你的程序需要在沙箱
 * \~chinese 环境如 flatpak 中运行，可选的一套方案是通过 DTK_DBUS_SINGLEINSTANCE 这个
 * \~chinese 编译宏来控制单实例使用 DBus 方案。
 *
 * \~chinese \return 设置成功返回 true，否则返回 false。
 */
bool DApplication::setSingleInstance(const QString &key)
{
    return setSingleInstance(key, UserScope);
}

/*!
 * \~chinese \brief DApplication::setSingleInstance 是一个重写函数，增加了控制单实例范围的 \a singleScope 参数。
 * \~chinese        在Linux环境下默认使用DBus的方式实现单例判断，在其它环境或者设置了环境变量 DTK_USE_SEMAPHORE_SINGLEINSTANCE
 * \~chinese        时使用系统信号量的方式实现单例判断
 * \~chinese \param key 是确定程序唯一性的ID，一般使用程序的二进制名称即可。
 * \~chinese \param singleScope 用于指定单实例的影响范围，具体见 \a DApplication::SingleScope。
 * \~chinese \return 设置成功返回 true，否则返回 false。
 */
bool DApplication::setSingleInstance(const QString &key, SingleScope singleScope)
{
    D_D(DApplication);

    auto scope = singleScope == SystemScope ? DGuiApplicationHelper::WorldScope : DGuiApplicationHelper::UserScope;
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::newProcessInstance,
            this, &DApplication::newInstanceStarted, Qt::UniqueConnection);

    return DGuiApplicationHelper::setSingleInstance(key, scope);
}

/*!
 * \~english \brief DApplication::loadTranslator loads translate file form
 * \~english system or application data path;
 * \~english You must name the file correctly; if the program is dde-dock，
 * \~english then the qm file for English locale would be dde-dock_en.qm.
 * \~english Translation files must be placed in correct directories as well.
 * \~english The lookup order is as follows:
 * \~english 1. ~/.local/share/APPNAME/translations;
 * \~english 2. /usr/local/share/APPNAME/translations;
 * \~english 3. /usr/share/APPNAME/translations;
 * \~english 4. :/translations;
 * \~english APPNAME is the name of program executable.
 * \~english \param localeFallback, a list of fallback locale you want load.
 * \~english \return load success
 *
 * \~chinese \brief DApplication::loadTranslator 加载程序的翻译文件。
 * \~chinese 使用这个函数需要保证翻译文件必须正确命名: 例如程序名叫 dde-dock，
 * \~chinese 那么翻译文件在中文locale下的名称必须是 dde-dock_zh_CN.qm；翻译文件还需要放置
 * \~chinese 在特定的位置，此函数会按照优先级顺序在以下目录中查找翻译文件：
 * \~chinese 1. ~/.local/share/APPNAME/translations;
 * \~chinese 2. /usr/local/share/APPNAME/translations;
 * \~chinese 3. /usr/share/APPNAME/translations;
 * \~chinese 4. :/translations;
 * \~chinese APPNAME即可执行文件的名称。
 *
 * \~chinese \param localeFallback 指定了回退的locale列表，默认只有系统locale。
 * \~chinese \return 加载成功返回 true，否则返回 false。
 */
bool DApplication::loadTranslator(QList<QLocale> localeFallback)
{
    D_D(DApplication);

    d->loadDtkTranslator(localeFallback);

    QList<DPathBuf> translateDirs;
    auto appName = applicationName();
    //("/home/user/.local/share", "/usr/local/share", "/usr/share")
    auto dataDirs = DStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const auto &path : dataDirs) {
        DPathBuf DPathBuf(path);
        translateDirs << DPathBuf / appName / "translations";
    }
    DPathBuf runDir(this->applicationDirPath());
    translateDirs << runDir.join("translations");
    DPathBuf currentDir(QDir::currentPath());
    translateDirs << currentDir.join("translations");
    translateDirs << DPathBuf(":/translations");

#ifdef DTK_STATIC_TRANSLATION
    translateDirs << DPathBuf(":/dtk/translations");
#endif

    return d->loadTranslator(translateDirs, appName, localeFallback);
}

/*!
 * \~chinese \brief DApplication::loadDXcbPlugin 强制程序使用的平台插件到dxcb。
 * \~chinese 这个函数的工作原理是通过设置 QT_QPA_PLATFORM 来影响平台插件的加载，所以此函数
 * \~chinese 必须在 DApplication 实例创建前进行调用。
 * \~chinese \return 设置成功返回 true，否则返回 false。
 */
bool DApplication::loadDXcbPlugin()
{
    Q_ASSERT_X(!qApp, "DApplication::loadDxcbPlugin", "Must call before QGuiApplication defined object");

    if (!QPlatformIntegrationFactory::keys().contains(DXCB_PLUGIN_KEY)) {
        return false;
    }

    // fix QGuiApplication::platformName() to xcb
    qputenv("DXCB_FAKE_PLATFORM_NAME_XCB", "true");

    return qputenv("QT_QPA_PLATFORM", DXCB_PLUGIN_KEY);
}

/*!
 * \~chinese \brief DApplication::isDXcbPlatform 检查当前程序是否使用了dxcb平台插件。
 * \~chinese \return 正在使用返回 true，否则返回 false。
 */
bool DApplication::isDXcbPlatform()
{
    return DGUI_NAMESPACE::DPlatformHandle::isDXcbPlatform();
}

/*!
 * \~chinese \brief DApplication::buildDtkVersion 返回编译时的dtk版本；
 */
int DApplication::buildDtkVersion()
{
    return DtkBuildVersion::value;
}

/*!
 * \~chinese \brief DApplication::runtimeDtkVersion 返回运行时的dtk版本；
 */
int DApplication::runtimeDtkVersion()
{
    return DTK_VERSION;
}

/*!
 * \~chinese \brief DApplication::registerDDESession 用于跟 startdde 进行通信，告知
 * \~chinese startdde 进程已经启动成功。
 * \~chinese \note 只有DDE系统组件需要使用此函数，普通应用无需使用。
 */
void DApplication::registerDDESession()
{
#ifdef Q_OS_LINUX
    QString envName("DDE_SESSION_PROCESS_COOKIE_ID");

    QByteArray cookie = qgetenv(envName.toUtf8().data());
    qunsetenv(envName.toUtf8().data());

    if (!cookie.isEmpty()) {
        DDBusSender()
                .service("com.deepin.SessionManager")
                .path("/com/deepin/SessionManager")
                .interface("com.deepin.SessionManager")
                .method("Register")
                .arg(QString(cookie))
                .call();
    }
#endif
}

/*!
 * \~chinese \brief DApplication::customQtThemeConfigPathByUserHome
 * \~chinese 根据用户家目录设置Qt主题配置文件的目录。
 * \~chinese \param home 家目录，不要以 "/" 结尾
 * \~chinese \warning 必须在构造 DApplication 对象之前调用
 * \~chinese \sa DApplication::customQtThemeConfigPathByUserHome
 */
void DApplication::customQtThemeConfigPathByUserHome(const QString &home)
{
    customQtThemeConfigPath(home + "/.config");
}

/*!
 * \~chinese \brief DApplication::customQtThemeConfigPath
 * \~chinese 自定义Qt主题配置文件的路径。
 *
 * \~chinese 默认文件通常为 "~/.config/deepin/qt-theme.ini"
 * \~chinese 其中包含应用的图标主题、字体、、屏幕缩放等相关的配置项。可应用于以root用户启动的
 * \~chinese 应用，需要跟随某个一般用户的主题设置项。
 * \~chinese \a path 中不包含 "/deepin/qt-theme.ini" 部分，如：path = "/tmp"，
 * \~chinese 则配置文件路径为："/tmp/deepin/qt-theme.ini"。
 * \~chinese \param path 不要以 "/" 结尾
 * \~chinese \warning 必须在构造 DApplication 对象之前调用
 */
void DApplication::customQtThemeConfigPath(const QString &path)
{
    Q_ASSERT_X(!qApp, "DApplication::customQtThemeConfigPath", "Must call before QGuiApplication defined object");
    qputenv(QT_THEME_CONFIG_PATH, path.toLocal8Bit());
}

/*!
 * \~chinese \brief DApplication::customizedQtThemeConfigPath
 * \~chinese \return 返回自定义的 Qt 主题配置文件路径，未设置过此路径时返回为空。
 * \~chinese \sa DApplication::customQtThemeConfigPath
 */
QString DApplication::customizedQtThemeConfigPath()
{
    return QString::fromLocal8Bit(qgetenv(QT_THEME_CONFIG_PATH));
}

/**
 * \~english @brief DApplication::productName returns the product name of this application.
 *
 * \~english It's mainly used to construct an about dialog of the application.
 *
 * \~english @return the product name of this application if set, otherwise the applicationDisplayName.
 *
 *
 * \~chinese \property DApplication::productName
 * \~chinese \brief productName属性是程序的产品名称，
 * \~chinese 产品名称不同与 applicationName ，应该是类似如“深度终端”，而不是 deepin-terminal，
 * \~chinese 这个名称主要用于在程序的关于对话框中进行展示。
 * \~chinese 如果没有手动通过 setProductName 来设置，会尝试使用 QApplication::applicationDisplayName 来充当产品名称。
 *
 * \sa productIcon, aboutDialog
 */
QString DApplication::productName() const
{
    D_DC(DApplication);

    return d->productName.isEmpty() ? applicationDisplayName() : d->productName;
}

/**
 * \~english @brief DApplication::setProductName sets the product name of this application.
 * \~english @param productName is the product name to be set.
 */
void DApplication::setProductName(const QString &productName)
{
    D_D(DApplication);

    d->productName = productName;
}

/**
 * \~english @brief DApplication::productIcon returns the product icon of this application.
 *
 * \~english It's mainly used to construct an about dialog of the application.
 *
 * \~english @return the product icon of this application if set, otherwise empty.
 *
 *
 * \~chinese \property DApplication::productIcon
 * \~chinese \brief productIcon 属性是程序的产品图标，
 * \~chinese 主要用于在关于对话框中进行展示。
 *
 * \sa productName, aboutDialog
 */
const QIcon &DApplication::productIcon() const
{
    D_DC(DApplication);

    return d->productIcon;
}

/**
 * \~english @brief DApplication::setProductIcon sets the product icon of this application.
 * \~english @param productIcon is the product icon to be set.
 */
void DApplication::setProductIcon(const QIcon &productIcon)
{
    D_D(DApplication);

    d->productIcon = productIcon;
}

/**
 * \~english @brief DApplication::applicationLicense returns the license used by this application.
 *
 * \~english It's mainly used to construct an about dialog of the application.
 *
 * \~english @return the license used by this application.
 *
 *
 * \~chinese \property DApplication::applicationLicense
 * \~chinese \brief applicationLicense 属性是程序所使用的授权协议；
 * \~chinese 主要用于在关于对话框中进行展示，默认值为 GPLv3。
 */
QString DApplication::applicationLicense() const
{
    D_DC(DApplication);

    return d->appLicense;
}

/**
 * \~english @brief DApplication::setApplicationLicense sets the license of this application.
 * \~english @param license is the license to be set.
 */
void DApplication::setApplicationLicense(const QString &license)
{
    D_D(DApplication);

    d->appLicense = license;
}

/**
 * \~english @brief DApplication::applicationDescription returns the long description of the application.
 *
 * \~english It's mainly used to construct an about dialog of the application.
 *
 * \~english @return the description of the application if set, otherwise empty.
 *
 *
 * \~chinese \property DApplication::applicationDescription
 * \~chinese \brief applicationDescription 属性记录了程序的描述信息，主要用于关于对话框中的信息展示。
 */
QString DApplication::applicationDescription() const
{
    D_DC(DApplication);

    return d->appDescription;
}

/**
 * \~english @brief DApplication::setApplicationDescription sets the description of the application.
 * \~english @param description is description to be set.
 */
void DApplication::setApplicationDescription(const QString &description)
{
    D_D(DApplication);

    d->appDescription = description;
}

/*!
 * \~chinese \property DApplication::applicationHomePage
 * \~chinese \brief applicationHomePage 属性记录程序的主页网址，主要用于在关于对话框中进行展示。
 */
QString DApplication::applicationHomePage() const
{
    D_DC(DApplication);

    return d->homePage;
}

void DApplication::setApplicationHomePage(const QString &link)
{
    D_D(DApplication);

    d->homePage = link;
}

/**
 * \~english @brief DApplication::applicationAcknowledgementPage returns the acknowlegement page of the application.
 *
 * \~english It's mainly used to construct an about dialog of the application.
 * \~english @return the acknowlegement page of the application if set, otherwise empty.
 *
 *
 * \~chinese \property DApplication::applicationAcknowledgementPage
 * \~chinese \brief applicationAcknowledgementPage 属性记录程序的鸣谢信息网址，主要用于在关于对话框中进行展示。
 */
QString DApplication::applicationAcknowledgementPage() const
{
    D_DC(DApplication);

    return d->acknowledgementPage;
}

/**
 * \~english @brief DApplication::setApplicationAcknowledgementPage sets the acknowlegement page of the application.
 * \~english @param link is the acknowlegement page link to be shown in the about dialog.
 */
void DApplication::setApplicationAcknowledgementPage(const QString &link)
{
    D_D(DApplication);

    d->acknowledgementPage = link;
}


/*!
 * \~chinese \property DApplication::applicationAcknowledgementVisible
 * \~chinese \brief applicationAcknowledgementVisible 属性控制是否显示关于对话框中的鸣谢地址显示。
 */
bool DApplication::applicationAcknowledgementVisible() const
{
    D_DC(DApplication);
    return d->acknowledgementPageVisible;
}

void DApplication::setApplicationAcknowledgementVisible(bool visible)
{
    D_D(DApplication);
    d->acknowledgementPageVisible = visible;
}

/**
 * \~english @brief DApplication::aboutDialog returns the about dialog of this application.
 *
 * \~english If the about dialog is not set, it will automatically construct one.
 *
 * \~english @return the about dialog instance.
 *
 *
 * \~chinese \brief DApplication::aboutDialog 返回一个基于当前程序信息的关于对话框。
 * \~chinese 此对话框可以通过 DApplication::setAboutDialog 进行设置，如果没有设置就使用此函数进行获取，
 * \~chinese 系统会创建一个新的关于对话框。
 *
 * \sa setAboutDialog
 */
DAboutDialog *DApplication::aboutDialog()
{
    D_D(DApplication);

    return d->aboutDialog;
}

/**
 * \~english @brief DApplication::setAboutDialog sets the about dialog of this application.
 *
 * \~english It's mainly used to override the auto-constructed about dialog which is not
 * \~english a common case, so please do double check before using this method.
 *
 * \~english @param aboutDialog
 *
 *
 * \~chinese \brief DApplication::setAboutDialog 为当前程序设置一个关于对话框。
 *
 * \sa aboutDialog
 */
void DApplication::setAboutDialog(DAboutDialog *aboutDialog)
{
    D_D(DApplication);

    if (d->aboutDialog && d->aboutDialog != aboutDialog) {
        d->aboutDialog->deleteLater();
    }

    d->aboutDialog = aboutDialog;
}

/*!
 * \~chinese \property DApplication::visibleMenuShortcutText
 * \~chinese \brief visibleMenuShortcutText 属性代表了程序中菜单项是否显示对应的快捷键。
 */
bool DApplication::visibleMenuShortcutText() const
{
    D_DC(DApplication);

    return d->visibleMenuShortcutText;
}

void DApplication::setVisibleMenuShortcutText(bool value)
{
    D_D(DApplication);

    d->visibleMenuShortcutText = value;
}

/*!
 * \~chinese \property DApplication::visibleMenuCheckboxWidget
 * \~chinese \brief visibleMenuCheckboxWidget 属性代表了程序中菜单项是否显示Checkbox控件。
 */
bool DApplication::visibleMenuCheckboxWidget() const
{
    D_DC(DApplication);

    return d->visibleMenuCheckboxWidget;
}

void DApplication::setVisibleMenuCheckboxWidget(bool value)
{
    D_D(DApplication);

    d->visibleMenuCheckboxWidget = value;
}

/*!
 * \~chinese \property DApplication::visibleMenuIcon
 * \~chinese \brief visibleMenuIcon 属性代表了程序中菜单项是否显示图标。
 */
bool DApplication::visibleMenuIcon() const
{
    D_DC(DApplication);

    return d->visibleMenuIcon;
}

void DApplication::setVisibleMenuIcon(bool value)
{
    D_D(DApplication);

    d->visibleMenuIcon = value;
}

bool DApplication::autoActivateWindows() const
{
    D_DC(DApplication);

    return d->autoActivateWindows;
}

void DApplication::setAutoActivateWindows(bool autoActivateWindows)
{
    D_D(DApplication);

    d->autoActivateWindows = autoActivateWindows;

    if (autoActivateWindows) {
        connect(DGuiApplicationHelper::instance(), SIGNAL(newProcessInstance(qint64, const QStringList &)),
                this, SLOT(_q_onNewInstanceStarted()));
    } else {
        disconnect(DGuiApplicationHelper::instance(), SIGNAL(newProcessInstance(qint64, const QStringList &)),
                this, SLOT(_q_onNewInstanceStarted()));
    }
}

/**
 * \~chinese @brief DApplication::acclimatizeVirtualKeyboard
 *
 * \~chinese 为窗口的可输入控件添加自动适应虚拟键盘输入法的功能。开启此功能后，当
 * \~chinese 监听到 \a QInputMethod::keyboardRectangleChanged 后，会判断当
 * \~chinese 前的可输入（不仅仅是处于焦点状态）控件是否为此 \a window 的子控件，
 * \~chinese 如果是，则将通过 \a QWidget::setContentsMargins 更新 \a window
 * \~chinese 的布局区域，以此确保可输入控件处于可见区域。如果可输入控件处于一个
 * \~chinese \a QAbstractScrollArea 中，将会压缩 \a window 的布局空间，促使
 * \~chinese 可滚动区域缩小，再使用 \a QAbstraceScrollArea::scrollContentsBy
 * \~chinese 将可输入控件滚动到合适的区域，否则将直接把 \a 的内容向上移动为虚拟键盘
 * \~chinese 腾出空间。
 * \~chinese \note 在使用之前要确保窗口的 \a Qt::WA_LayoutOnEntireRect
 * \~chinese \a 和 Qt::WA_ContentsMarginsRespectsSafeArea 都为 false
 * \~chinese \param window 需是一个顶层窗口
 * \~chinese \sa QWidget::isTopLevel
 * \~chinese \sa QWidget::setContentsMargins
 * \~chinese \sa QInputMethod::cursorRectangle
 * \~chinese \sa QInputMethod::inputItemClipRectangle
 * \~chinese \sa QInputMethod::keyboardRectangle
 * \~chinese \sa QAbstractScrollArea
 */
void DApplication::acclimatizeVirtualKeyboard(QWidget *window)
{
    Q_ASSERT(window->isTopLevel()
             && !window->testAttribute(Qt::WA_LayoutOnEntireRect)
             && !window->testAttribute(Qt::WA_ContentsMarginsRespectsSafeArea));

    D_D(DApplication);
    if (d->acclimatizeVirtualKeyboardWindows.contains(window))
        return;

    if (d->acclimatizeVirtualKeyboardWindows.isEmpty()) {
        connect(this, SIGNAL(focusChanged(QWidget *, QWidget *)),
                this, SLOT(_q_resizeWindowContentsForVirtualKeyboard()),
                Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
        connect(inputMethod(), SIGNAL(keyboardRectangleChanged()),
                this, SLOT(_q_resizeWindowContentsForVirtualKeyboard()),
                Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
        connect(inputMethod(), SIGNAL(visibleChanged()),
                this, SLOT(_q_resizeWindowContentsForVirtualKeyboard()),
                Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
        connect(inputMethod(), SIGNAL(cursorRectangleChanged()),
                this, SLOT(_q_panWindowContentsForVirtualKeyboard()),
                Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
        connect(inputMethod(), SIGNAL(inputItemClipRectangleChanged()),
                this, SLOT(_q_panWindowContentsForVirtualKeyboard()),
                Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
    }

    d->acclimatizeVirtualKeyboardWindows << window;

    connect(window, &QWidget::destroyed, this, [this, window] {
        this->ignoreVirtualKeyboard(window);
    });

    if (window->isAncestorOf(focusWidget())) {
        d->doAcclimatizeVirtualKeyboard(window, focusWidget(), true);
    }
}

/**
 * \~chinese @brief DApplication::ignoreVirtualKeyboard
 * \~chinese 恢复到默认状态，将不会为虚拟键盘的环境做任何自适应操作
 * \~chinese \note 此操作不会恢复对 \a QWidget::contentsMargins 的修改
 * \~chinese \param window 需是一个调用过 \a acclimatizeVirtualKeyboard 的窗口
 */
void DApplication::ignoreVirtualKeyboard(QWidget *window)
{
    D_D(DApplication);

    if (!d->acclimatizeVirtualKeyboardWindows.removeOne(window))
        return;

    if (d->acclimatizeVirtualKeyboardWindows.isEmpty()) {
        disconnect(this, SIGNAL(focusChanged(QWidget *, QWidget *)),
                   this, SLOT(_q_resizeWindowContentsForVirtualKeyboard()));
        disconnect(inputMethod(), SIGNAL(keyboardRectangleChanged()),
                   this, SLOT(_q_resizeWindowContentsForVirtualKeyboard()));
        disconnect(inputMethod(), SIGNAL(visibleChanged()),
                   this, SLOT(_q_resizeWindowContentsForVirtualKeyboard()));
        disconnect(inputMethod(), SIGNAL(cursorRectangleChanged()),
                   this, SLOT(_q_panWindowContentsForVirtualKeyboard()));
        disconnect(inputMethod(), SIGNAL(inputItemClipRectangleChanged()),
                   this, SLOT(_q_panWindowContentsForVirtualKeyboard()));
    }
}

/**
 * \~chinese @brief DApplication::isAcclimatizedVirtualKeyboard
 * \~chinese 如果 \a window 会自适应虚拟键盘返回 true,否则返回 false
 * \~chinese \param window
 */
bool DApplication::isAcclimatizedVirtualKeyboard(QWidget *window) const
{
    D_DC(DApplication);
    return d->acclimatizeVirtualKeyboardWindows.contains(window);
}

/**
 * \~english @brief DApplication::handleHelpAction
 *
 * \~english Triggered when user clicked the help menu item of this window's titlebar,
 * \~english default action is to open the user manual of this program, override this
 * \~english method if you want to change the default action.
 *
 * \~chinese \brief DApplication::handleHelpAction 函数在用户点击窗口标题栏的帮助按钮
 * \~chinese 时触发，默认实现为打开当前程序的帮助手册，子类可以重现实现此函数以覆盖其默认行为。
 */
void DApplication::handleHelpAction()
{
#ifdef Q_OS_LINUX
    QString appid = applicationName();

    // new interface use applicationName as id
    QDBusInterface manual("com.deepin.Manual.Open",
                          "/com/deepin/Manual/Open",
                          "com.deepin.Manual.Open");
    QDBusReply<void> reply = manual.call("ShowManual", appid);
    if (reply.isValid())  {
        qDebug() << "call com.deepin.Manual.Open success";
        return;
    }
    qDebug() << "call com.deepin.Manual.Open failed" << reply.error();
    // fallback to old interface
    QProcess::startDetached("dman", QStringList() << appid);
#else
    qWarning() << "not support dman now";
#endif
}

/**
 * \~english @brief DApplication::handleAboutAction
 *
 * \~english Triggered when user clicked the about menu item of this window's titlebar,
 * \~english default action is to show the about dialog of this window(if there is one),
 * \~english override this method if you want to change the default action.
 *
 * \~chinese \brief DApplication::handleAboutAction 函数在用户点击窗口标题栏的关于按钮
 * \~chinese 时触发，默认实现为打开程序关于对话框，子类可以重现实现此函数以覆盖其默认行为。
 */
void DApplication::handleAboutAction()
{
    D_D(DApplication);

    if (d->aboutDialog) {
        d->aboutDialog->show();
        d->aboutDialog->activateWindow();
        d->aboutDialog->raise();
        return;
    }

    DAboutDialog *aboutDialog = new DAboutDialog(activeWindow());
    aboutDialog->setProductName(productName());
    aboutDialog->setProductIcon(productIcon());
    aboutDialog->setVersion(translate("DAboutDialog", "Version: %1").arg(applicationVersion()));
    aboutDialog->setDescription(applicationDescription());

    if (!applicationLicense().isEmpty()) {
        aboutDialog->setLicense(translate("DAboutDialog", "%1 is released under %2").arg(productName()).arg(applicationLicense()));
    }
    if (!applicationAcknowledgementPage().isEmpty()) {
        aboutDialog->setAcknowledgementLink(applicationAcknowledgementPage());
    }
    aboutDialog->setAcknowledgementVisible(d->acknowledgementPageVisible);

    aboutDialog->show();
    aboutDialog->setAttribute(Qt::WA_DeleteOnClose);

    //目前的关于对话框是非模态的,这里的处理是防止关于对话框可以打开多个的情况
    // 不能使用aboutToClose信号 应用能够打开多个的情况下 打开关于后直接关闭程序
    // 此时aboutToColose信号不会触发 再次打开程序并打开关于会出现访问野指针 程序崩溃的情况
    d->aboutDialog = aboutDialog;
    connect(d->aboutDialog, &DAboutDialog::destroyed, this, [=] {
        d->aboutDialog = nullptr;
    });
}

/**
 * \~english @brief DApplication::handleQuitAction
 *
 * \~english Triggered when user clicked the exit menu item of this window's titlebar,
 * \~english default action is to quit this program, you can try to save your data before
 * \~english the program quitting by connecting to the aboutToQuit signal of this application.
 * \~english override this method if you want to change the default action.
 *
 * \~chinese \brief DApplication::handleQuitAction 函数在用户点击窗口标题栏的关闭按钮
 * \~chinese 时触发，默认行为是退出整个程序，子类可以重写此函数以覆盖其行为。
 */
void DApplication::handleQuitAction()
{
    quit();
}

static inline bool basePrintPropertiesDialog(const QWidget *w)
{
    while (w) {
        if (w->inherits("QPrintPropertiesDialog")
                || w->inherits("QPageSetupDialog")) {
            return true;
        }

        w = w->parentWidget();
    }

    return false;
}

bool DApplication::notify(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::FocusIn) {
        QFocusEvent *fe = static_cast<QFocusEvent*>(event);
        QWidget *widget = qobject_cast<QWidget*>(obj);

        if (widget && fe->reason() == Qt::ActiveWindowFocusReason && !widget->isTopLevel()
                && (widget->focusPolicy() & Qt::StrongFocus) != Qt::StrongFocus) {
            // 针对激活窗口所获得的焦点，为了避免被默认给到窗口内部的控件上，此处将焦点还给主窗口
            // 并且只设置一次
#define NON_FIRST_ACTIVE "_d_dtk_non_first_active_focus"
            QWidget *top_window = widget->topLevelWidget();

            if (top_window->isWindow() && !top_window->property(NON_FIRST_ACTIVE).toBool()) {
                top_window->setFocus();
                top_window->setProperty(NON_FIRST_ACTIVE, true);
            }
        }
    }

    return QApplication::notify(obj, event);
}

int DtkBuildVersion::value = 0;

DWIDGET_END_NAMESPACE

#include "moc_dapplication.cpp"
