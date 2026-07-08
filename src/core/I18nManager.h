#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QTranslator;
class QQmlApplicationEngine;

/**
 * @brief Runtime language switcher (i18n) for the whole app.
 *
 * Follows the same Meyer's-singleton pattern as HidHideManager/DeviceManager:
 * use I18nManager::instance() to obtain the single shared instance. Exposed
 * to QML as the root-context property "I18nManager" (see main.cpp), so
 * `I18nManager.setLanguage("es")` works directly from any .qml file, the
 * same way `Theme.someProperty` reads from the Theme.qml singleton.
 *
 * setEngine() must be called once from main.cpp right after the
 * QQmlApplicationEngine is constructed (and before/around engine.load()) so
 * a later setLanguage() call can trigger QQmlApplicationEngine::retranslate()
 * - without that, every already-instantiated qsTr() binding in the running
 * QML tree would keep showing the old language until the user restarted the
 * app, since qsTr() only re-evaluates when something asks the binding to
 * re-run.
 */
class I18nManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY currentLanguageChanged)

public:
    static I18nManager &instance();

    I18nManager(const I18nManager &) = delete;
    I18nManager &operator=(const I18nManager &) = delete;
    I18nManager(I18nManager &&) = delete;
    I18nManager &operator=(I18nManager &&) = delete;

    /// Declared here but defined in the .cpp (not "= default" inline): the
    /// implicit destructor needs QTranslator's complete type to destroy
    /// m_translator's std::unique_ptr, but this header only forward-declares
    /// QTranslator - moc's generated metatype machinery (QMetaType::getDtor)
    /// instantiates I18nManager's destructor right here otherwise, failing
    /// with "invalid application of 'sizeof' to incomplete type 'QTranslator'".
    ~I18nManager() override;

    /// See the class doc above - call once, right after constructing the
    /// QQmlApplicationEngine in main.cpp. Also applies (and retranslates
    /// into) whatever language was last chosen via setLanguage() - saved to
    /// QSettings under "Language", "en" if nothing was ever chosen - so the
    /// UI restores the user's pick from before instead of always starting
    /// back on English every launch.
    void setEngine(QQmlApplicationEngine *engine);

    /// Installs (or removes) a QTranslator loading ":/i18n/grembling_" +
    /// langCode + ".qm" and retranslates the running QML tree. langCode
    /// "en" (the app's own source language - every qsTr() literal is
    /// already English, except SCIntegrationManager's tr() action names,
    /// which are Spanish literals - see its own docs for that deliberate
    /// mismatch) removes any installed translator instead of trying to
    /// load a nonexistent grembling_en.qm, so qsTr() falls back to the raw
    /// source text. A missing/unloadable .qm for any other langCode is
    /// logged and otherwise a no-op - the UI stays on whatever language was
    /// active before, and neither QSettings nor currentLanguage() are
    /// touched (a failed switch must not be reported/persisted as if it
    /// had actually happened). On success, persists langCode to QSettings
    /// (restoreSavedLanguage() reads it back on the next launch) and
    /// updates currentLanguage.
    Q_INVOKABLE void setLanguage(const QString &langCode);

    /// "en", or whatever langCode the most recent successful setLanguage()
    /// call installed - lets SettingsView.qml's Language ComboBox show the
    /// actually-active language (e.g. after being reopened, or on first
    /// load restoring a saved choice) instead of always defaulting back to
    /// index 0.
    QString currentLanguage() const { return m_currentLanguage; }

signals:
    /// NOTIFY for currentLanguage - fires only on a successful setLanguage() call.
    void currentLanguageChanged();

private:
    explicit I18nManager(QObject *parent = nullptr);

    QQmlApplicationEngine *m_engine = nullptr;
    std::unique_ptr<QTranslator> m_translator;
    QString m_currentLanguage = QStringLiteral("en");
};
