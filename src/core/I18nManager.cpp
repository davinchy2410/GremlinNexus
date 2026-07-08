#include "I18nManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QTranslator>

namespace {
const QString kLanguageSettingsKey = QStringLiteral("Language");
}

I18nManager &I18nManager::instance()
{
    static I18nManager inst;
    return inst;
}

I18nManager::I18nManager(QObject *parent)
    : QObject(parent)
{
}

I18nManager::~I18nManager() = default;

void I18nManager::setEngine(QQmlApplicationEngine *engine)
{
    m_engine = engine;
    setLanguage(QSettings().value(kLanguageSettingsKey, QStringLiteral("en")).toString());
}

void I18nManager::setLanguage(const QString &langCode)
{
    if (m_translator) {
        QCoreApplication::removeTranslator(m_translator.get());
        m_translator.reset();
    }

    // Only persisted/reported via currentLanguageChanged() once actually
    // applied - a failed .qm load below must not leave QSettings/
    // currentLanguage() claiming a language that isn't really showing.
    bool applied = true;

    if (langCode != QStringLiteral("en")) {
        auto translator = std::make_unique<QTranslator>();
        if (translator->load(QStringLiteral(":/i18n/grembling_%1.qm").arg(langCode))) {
            QCoreApplication::installTranslator(translator.get());
            m_translator = std::move(translator);
        } else {
            qWarning() << "I18nManager: failed to load translation for" << langCode;
            applied = false;
        }
    }

    if (applied) {
        QSettings().setValue(kLanguageSettingsKey, langCode);
        if (m_currentLanguage != langCode) {
            m_currentLanguage = langCode;
            emit currentLanguageChanged();
        }
    }

    if (m_engine) {
        m_engine->retranslate();
    }
}
