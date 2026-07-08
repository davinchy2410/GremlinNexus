#pragma once

#include <QObject>
#include <QString>

/**
 * @brief Root ViewModel for the QML frontend (Phase 10).
 *
 * A single instance is owned by main.cpp and injected into the QML engine's
 * root context (as "mainViewModel") rather than registered as a creatable
 * QML type: this is meant to be one app-wide object the whole QML tree
 * shares, not something individual screens instantiate copies of.
 *
 * Part 1 exposes only what the design-system skeleton needs - the window
 * title and which top-level section (sidebar entry) is currently selected.
 * Later Fase 10 parts will add per-screen ViewModels (Profiles, Curves,
 * Control Center, Settings) that this one may come to own/expose, but this
 * class itself stays a thin, UI-shell-level concern - it does not reach
 * into EventRouter/ProfileManager directly.
 */
class MainViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString windowTitle READ windowTitle CONSTANT)
    Q_PROPERTY(QString currentView READ currentView WRITE setCurrentView NOTIFY currentViewChanged)

public:
    explicit MainViewModel(QObject *parent = nullptr);

    QString windowTitle() const;

    /// One of "Profiles" / "Curves" / "DeviceTester" / "Settings" /
    /// "LogConsole" / "StarCitizen" - which sidebar entry (and therefore
    /// which placeholder/screen) is active.
    QString currentView() const;
    void setCurrentView(const QString &view);

signals:
    void currentViewChanged();

private:
    QString m_currentView;
};
