#pragma once

#include <QMainWindow>

#include "DeviceInfo.h"
#include "ProfileManager.h"

class EventRouter;
class QTableWidget;
class QTableWidgetItem;

/**
 * @brief Desktop shell for GremblingEx: live device inventory + profile
 *        loading/migration controls, sitting on top of the EventRouter
 *        engine that runs underneath regardless of whether this window is
 *        ever shown.
 *
 * Holds only a reference to EventRouter (does not own it — see main.cpp,
 * where EventRouter outlives MainWindow by construction order) and its own
 * ProfileManager instance for translating file-dialog selections into
 * EventRouter routing state.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(EventRouter &router, QWidget *parent = nullptr);

private slots:
    /// Upserts device's row in the device table, keyed by systemPath.
    /// Idempotent: safe to call more than once for the same device (used
    /// both for live deviceAdded signals and for the startup snapshot —
    /// see the constructor for why both are needed).
    void onDeviceAdded(const DeviceInfo &device);

    /// Removes systemPath's row from the device table, if present.
    void onDeviceRemoved(const QString &systemPath);

    void onLoadProfileClicked();
    void onMigrateXmlClicked();

private:
    void setupDeviceTable();
    void setupToolBar();

    /// Returns the row whose column-0 item carries systemPath in
    /// Qt::UserRole, or -1 if no such row exists.
    int findRowForSystemPath(const QString &systemPath) const;

    EventRouter &m_router;
    ProfileManager m_profileManager;

    QTableWidget *m_deviceTable = nullptr;
};
