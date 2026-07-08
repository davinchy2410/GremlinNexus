#include "MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonObject>
#include <QJsonValue>
#include <QMessageBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>

#include "DeviceManager.h"
#include "EventRouter.h"
#include "LegacyProfileImporter.h"

namespace {
constexpr int kSystemPathRole = Qt::UserRole;
}

MainWindow::MainWindow(EventRouter &router, QWidget *parent)
    : QMainWindow(parent)
    , m_router(router)
{
    setWindowTitle(tr("GremblingEx"));
    resize(900, 400);

    setupDeviceTable();
    setupToolBar();
    statusBar()->showMessage(tr("Listo."));

    // Connect first, *then* take a snapshot: DeviceManager's background
    // scan may already have found devices before this window existed, so
    // relying on the live signal alone would miss them. onDeviceAdded() is
    // an upsert keyed by systemPath, so if a deviceAdded signal for one of
    // those same devices was already queued when we connected, it just
    // updates the row the snapshot created instead of duplicating it.
    connect(&DeviceManager::instance(), &DeviceManager::deviceAdded, this, &MainWindow::onDeviceAdded);
    connect(&DeviceManager::instance(), &DeviceManager::deviceRemoved, this, &MainWindow::onDeviceRemoved);

    const auto devices = DeviceManager::instance().getConnectedDevices();
    for (const auto &device : devices) {
        onDeviceAdded(device);
    }
}

void MainWindow::setupDeviceTable()
{
    m_deviceTable = new QTableWidget(this);
    m_deviceTable->setColumnCount(7);
    m_deviceTable->setHorizontalHeaderLabels(
        {tr("Nombre"), tr("VID"), tr("PID"), tr("Ejes"), tr("Botones"), tr("Hats"), tr("Ruta del sistema")});
    m_deviceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_deviceTable->horizontalHeader()->setStretchLastSection(true);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceTable->verticalHeader()->setVisible(false);

    setCentralWidget(m_deviceTable);
}

void MainWindow::setupToolBar()
{
    QToolBar *toolBar = addToolBar(tr("Perfiles"));
    toolBar->setMovable(false);

    QAction *loadJsonAction = toolBar->addAction(tr("Cargar Perfil JSON"));
    connect(loadJsonAction, &QAction::triggered, this, &MainWindow::onLoadProfileClicked);

    QAction *migrateXmlAction = toolBar->addAction(tr("Migrar XML Antiguo"));
    connect(migrateXmlAction, &QAction::triggered, this, &MainWindow::onMigrateXmlClicked);
}

int MainWindow::findRowForSystemPath(const QString &systemPath) const
{
    for (int row = 0; row < m_deviceTable->rowCount(); ++row) {
        const QTableWidgetItem *item = m_deviceTable->item(row, 0);
        if (item && item->data(kSystemPathRole).toString() == systemPath) {
            return row;
        }
    }
    return -1;
}

void MainWindow::onDeviceAdded(const DeviceInfo &device)
{
    int row = findRowForSystemPath(device.systemPath);
    if (row < 0) {
        row = m_deviceTable->rowCount();
        m_deviceTable->insertRow(row);
        for (int col = 0; col < m_deviceTable->columnCount(); ++col) {
            m_deviceTable->setItem(row, col, new QTableWidgetItem());
        }
        m_deviceTable->item(row, 0)->setData(kSystemPathRole, device.systemPath);
    }

    m_deviceTable->item(row, 0)->setText(device.deviceName);
    m_deviceTable->item(row, 1)->setText(device.vendorId);
    m_deviceTable->item(row, 2)->setText(device.productId);
    m_deviceTable->item(row, 3)->setText(QString::number(device.numAxes));
    m_deviceTable->item(row, 4)->setText(QString::number(device.numButtons));
    m_deviceTable->item(row, 5)->setText(QString::number(device.numHats));
    m_deviceTable->item(row, 6)->setText(device.systemPath);

    statusBar()->showMessage(tr("Dispositivo conectado: %1").arg(device.deviceName), 3000);
}

void MainWindow::onDeviceRemoved(const QString &systemPath)
{
    const int row = findRowForSystemPath(systemPath);
    if (row >= 0) {
        m_deviceTable->removeRow(row);
    }
    statusBar()->showMessage(tr("Dispositivo desconectado."), 3000);
}

void MainWindow::onLoadProfileClicked()
{
    const QString filePath =
        QFileDialog::getOpenFileName(this, tr("Cargar perfil JSON"), QString(), tr("Perfiles JSON (*.json)"));
    if (filePath.isEmpty()) {
        return;
    }

    if (m_profileManager.loadProfile(filePath, m_router)) {
        statusBar()->showMessage(tr("Perfil cargado: %1").arg(filePath), 5000);
    } else {
        QMessageBox::warning(this, tr("Error al cargar perfil"),
                              tr("No se pudo cargar \"%1\". Revisa la consola para más detalles.").arg(filePath));
    }
}

void MainWindow::onMigrateXmlClicked()
{
    const QString xmlPath =
        QFileDialog::getOpenFileName(this, tr("Migrar perfil XML antiguo"), QString(), tr("Perfiles XML (*.xml)"));
    if (xmlPath.isEmpty()) {
        return;
    }

    const QJsonObject imported = LegacyProfileImporter::importFromXml(xmlPath);
    if (imported.contains(QLatin1String("error"))) {
        QMessageBox::warning(this, tr("Error al migrar perfil"), imported.value(QLatin1String("error")).toString());
        return;
    }

    const QString jsonPath = QFileDialog::getSaveFileName(this, tr("Guardar perfil migrado como"), QString(),
                                                            tr("Perfiles JSON (*.json)"));
    if (jsonPath.isEmpty()) {
        return;
    }

    if (!m_profileManager.saveProfile(jsonPath, imported)) {
        QMessageBox::warning(this, tr("Error al guardar perfil"), tr("No se pudo escribir \"%1\".").arg(jsonPath));
        return;
    }

    if (m_profileManager.loadProfile(jsonPath, m_router)) {
        statusBar()->showMessage(tr("Perfil migrado y aplicado: %1").arg(jsonPath), 5000);
    } else {
        QMessageBox::warning(this, tr("Error al aplicar perfil migrado"),
                              tr("El perfil se guardó en \"%1\" pero no pudo aplicarse.").arg(jsonPath));
    }
}
