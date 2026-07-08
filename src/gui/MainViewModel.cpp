#include "MainViewModel.h"

MainViewModel::MainViewModel(QObject *parent)
    : QObject(parent)
    , m_currentView(QStringLiteral("Profiles"))
{
}

QString MainViewModel::windowTitle() const
{
    return QStringLiteral("Grembling Nexus");
}

QString MainViewModel::currentView() const
{
    return m_currentView;
}

void MainViewModel::setCurrentView(const QString &view)
{
    if (m_currentView == view) {
        return;
    }
    m_currentView = view;
    emit currentViewChanged();
}
