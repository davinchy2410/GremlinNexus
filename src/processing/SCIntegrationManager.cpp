#include "SCIntegrationManager.h"

#include <utility>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QVariantMap>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "DeviceInfo.h"
#include "DeviceManager.h"

namespace {
// Standard RSI install layout - the LIVE branch's default user profile,
// where a plain (non-"Advanced Controls Customization") actionmaps.xml
// export lands.
const QString kDefaultInstallPath =
    QStringLiteral("C:/Program Files/Roberts Space Industries/StarCitizen/LIVE/USER/Client/0/Profiles/default");

// Fase SC-3: where actionmaps.xml exports actually land, per the user's own
// report - a different (and more precise) folder than kDefaultInstallPath
// above, which Fase SC-1 guessed from general RSI install-layout knowledge
// before an exported file was available to check against.
const QString kDefaultMappingsFolder =
    QStringLiteral("C:/Program Files/Roberts Space Industries/StarCitizen/LIVE/user/client/0/controls/mappings");

// Fase SC-5.1: a handful of other drive/launcher locations real installs
// have been seen at - detectMappingsFolder() only ever picked
// kDefaultMappingsFolder (fixed on C:), so any other drive left the folder
// field empty with no obvious explanation. Still just a best-effort list,
// not exhaustive - the "Examinar..." picker in StarCitizenView.qml is the
// actual general-purpose fix for a install path this doesn't guess right.
const QStringList kAlternateMappingsFolders = {
    QStringLiteral("D:/Program Files/Roberts Space Industries/StarCitizen/LIVE/user/client/0/controls/mappings"),
    QStringLiteral("E:/Program Files/Roberts Space Industries/StarCitizen/LIVE/user/client/0/controls/mappings"),
    QStringLiteral("E:/Games/StarCitizen/LIVE/user/client/0/controls/mappings"),
    QStringLiteral("E:/juegos/StarCitizen/LIVE/user/client/0/controls/mappings"),
};

/// vJoy's own [0, 7] axis index space -> short display name, matching the
/// same convention ProfileEditorViewModel's getAxisName()/ActionPickerPopup's
/// vjoyAxisNames already use, so a resolved axis reads the same way here as
/// it would anywhere else in the app.
QString axisDisplayName(int axis)
{
    switch (axis) {
        case 0: return QStringLiteral("X");
        case 1: return QStringLiteral("Y");
        case 2: return QStringLiteral("Z");
        case 3: return QStringLiteral("Rx");
        case 4: return QStringLiteral("Ry");
        case 5: return QStringLiteral("Rz");
        case 6: return QStringLiteral("Slider 1");
        case 7: return QStringLiteral("Slider 2");
        default: return QStringLiteral("Axis %1").arg(axis);
    }
}
}

SCIntegrationManager &SCIntegrationManager::instance()
{
    static SCIntegrationManager s_instance;
    return s_instance;
}

SCIntegrationManager::SCIntegrationManager(QObject *parent)
    : QObject(parent)
{
    // Movimiento
    m_actionNames[QStringLiteral("spaceship_movement")] = tr("Vuelo");
    m_actionNames[QStringLiteral("v_pitch")] = tr("Cabeceo (Pitch)");
    m_actionNames[QStringLiteral("v_yaw")] = tr("Guiñada (Yaw)");
    m_actionNames[QStringLiteral("v_roll")] = tr("Alabeo (Roll)");
    m_actionNames[QStringLiteral("v_strafe_vertical")] = tr("Traslación Arriba/Abajo");
    m_actionNames[QStringLiteral("v_strafe_lateral")] = tr("Traslación Izq/Der");
    m_actionNames[QStringLiteral("v_strafe_forward")] = tr("Traslación Adelante");
    m_actionNames[QStringLiteral("v_strafe_back")] = tr("Traslación Atrás");
    m_actionNames[QStringLiteral("v_space_brake")] = tr("Freno Espacial");
    m_actionNames[QStringLiteral("v_afterburner")] = tr("Postquemador (Boost)");
    m_actionNames[QStringLiteral("v_ifcs_toggle_cruise_control")] = tr("Velocidad Crucero");
    m_actionNames[QStringLiteral("v_ifcs_speed_limiter_abs")] = tr("Limitador de Velocidad");
    m_actionNames[QStringLiteral("v_ifcs_toggle_vector_decoupling")] = tr("Modo Desacoplado");

    // Armas
    m_actionNames[QStringLiteral("spaceship_weapons")] = tr("Armas");
    m_actionNames[QStringLiteral("v_weapon_preset_fire_guns0")] = tr("Disparar Armas 1");
    m_actionNames[QStringLiteral("v_weapon_preset_fire_guns1")] = tr("Disparar Armas 2");
    m_actionNames[QStringLiteral("spaceship_missiles")] = tr("Misiles");
    m_actionNames[QStringLiteral("v_weapon_toggle_launch_missile")] = tr("Lanzar Misil");
    m_actionNames[QStringLiteral("v_weapon_cycle_missile_fwd")] = tr("Siguiente Misil");
    m_actionNames[QStringLiteral("spaceship_defensive")] = tr("Defensas");
    m_actionNames[QStringLiteral("v_weapon_countermeasure_decoy_launch")] = tr("Lanzar Señuelos (Decoys)");
    m_actionNames[QStringLiteral("v_weapon_countermeasure_noise_launch")] = tr("Lanzar Ruido (Noise)");

    // Targeting
    m_actionNames[QStringLiteral("spaceship_targeting")] = tr("Radares / Targeting");
    m_actionNames[QStringLiteral("v_target_cycle_all_fwd")] = tr("Fijar Siguiente Objetivo");
    m_actionNames[QStringLiteral("v_target_cycle_hostile_fwd")] = tr("Fijar Siguiente Enemigo");
    m_actionNames[QStringLiteral("v_target_cycle_friendly_fwd")] = tr("Fijar Siguiente Aliado");
    m_actionNames[QStringLiteral("v_target_toggle_pin_index1")] = tr("Fijar/Desfijar Objetivo (Pin 1)");

    // Sistemas
    m_actionNames[QStringLiteral("spaceship_general")] = tr("Nave / Sistemas");
    m_actionNames[QStringLiteral("seat_general")] = tr("Asiento / General");
    m_actionNames[QStringLiteral("v_toggle_quantum_mode")] = tr("Modo Quantum");
    m_actionNames[QStringLiteral("v_toggle_landing_system")] = tr("Tren de Aterrizaje");
    m_actionNames[QStringLiteral("v_toggle_vtol")] = tr("Modo VTOL");
    m_actionNames[QStringLiteral("v_eject")] = tr("Eyectarse");
    m_actionNames[QStringLiteral("v_toggle_power")] = tr("Encender/Apagar Nave");
    m_actionNames[QStringLiteral("v_toggle_engines")] = tr("Encender/Apagar Motores");
    m_actionNames[QStringLiteral("v_toggle_shields")] = tr("Encender/Apagar Escudos");
    m_actionNames[QStringLiteral("v_toggle_weapons")] = tr("Encender/Apagar Armas");

    // Master Modes / Operator Modes (nuevos en SC 3.23/3.24)
    m_actionNames[QStringLiteral("v_operator_mode_cycle_forward")] = tr("Siguiente Modo de Operador");
    m_actionNames[QStringLiteral("v_set_flight_mode")] = tr("Modo de Vuelo (NAV)");
    m_actionNames[QStringLiteral("v_set_guns_mode")] = tr("Modo de Armas (SCM)");
    m_actionNames[QStringLiteral("v_set_mining_mode")] = tr("Modo Minería");
    m_actionNames[QStringLiteral("v_set_missile_mode")] = tr("Modo Misiles");
    m_actionNames[QStringLiteral("v_set_quantum_mode")] = tr("Modo Quantum (NAV)");
    m_actionNames[QStringLiteral("v_set_salvage_mode")] = tr("Modo Salvataje");
    m_actionNames[QStringLiteral("v_set_scan_mode")] = tr("Modo Escáner");
    m_actionNames[QStringLiteral("v_toggle_mining_mode")] = tr("Alternar Modo Minería");
    m_actionNames[QStringLiteral("v_toggle_missile_mode")] = tr("Alternar Modo Misiles");
    m_actionNames[QStringLiteral("v_toggle_salvage_mode")] = tr("Alternar Modo Salvataje");

    // Sistemas de Nave y Puertas
    m_actionNames[QStringLiteral("v_flightready")] = tr("Flight Ready (Encendido Total)");
    m_actionNames[QStringLiteral("v_self_destruct")] = tr("Autodestrucción");
    m_actionNames[QStringLiteral("v_close_all_doors")] = tr("Cerrar Todas las Puertas");
    m_actionNames[QStringLiteral("v_open_all_doors")] = tr("Abrir Todas las Puertas");
    m_actionNames[QStringLiteral("v_toggle_all_doors")] = tr("Abrir/Cerrar Puertas");
    m_actionNames[QStringLiteral("v_lock_all_doors")] = tr("Bloquear Todas las Puertas");
    m_actionNames[QStringLiteral("v_unlock_all_doors")] = tr("Desbloquear Todas las Puertas");
    m_actionNames[QStringLiteral("v_toggle_all_doorlocks")] = tr("Bloquear/Desbloquear Puertas");
    m_actionNames[QStringLiteral("v_lock_all_ports")] = tr("Bloquear Puertos (Ports)");
    m_actionNames[QStringLiteral("v_unlock_all_ports")] = tr("Desbloquear Puertos (Ports)");
    m_actionNames[QStringLiteral("v_toggle_all_portlocks")] = tr("Bloquear/Desbloquear Puertos");
    m_actionNames[QStringLiteral("v_cooler_throttle_down")] = tr("Reducir Enfriadores");
    m_actionNames[QStringLiteral("v_cooler_throttle_up")] = tr("Aumentar Enfriadores");

    // Vistas y Paneles
    m_actionNames[QStringLiteral("v_mfd_interact_cycle_backwards_short")] = tr("Ciclar MFD Anterior");
    m_actionNames[QStringLiteral("v_mfd_interact_cycle_forwards_short")] = tr("Ciclar MFD Siguiente");
    m_actionNames[QStringLiteral("v_view_look_behind")] = tr("Mirar hacia atrás");
    m_actionNames[QStringLiteral("v_view_cycle_fwd")] = tr("Cambiar Vista (Siguiente)");
    m_actionNames[QStringLiteral("v_view_cycle_internal_fwd")] = tr("Cambiar Vista Interna");
    m_actionNames[QStringLiteral("v_view_dynamic_zoom_abs")] = tr("Zoom Dinámico");

    // FPS / Apuntado (si aplican a naves/torretas)
    m_actionNames[QStringLiteral("v_ads_hold")] = tr("Apuntar con la mira (Mantener)");
    m_actionNames[QStringLiteral("v_ads_stable_max_zoom_hold")] = tr("Estabilizar / Zoom Máximo (Mantener)");

    // --------------------------------------------------------
    // Mapeo Final de actionmaps.xml del usuario
    // --------------------------------------------------------

    // Visión y Modos Especiales
    m_actionNames[QStringLiteral("v_light_amplification_off")] = tr("Visión Nocturna (Apagar)");
    m_actionNames[QStringLiteral("v_light_amplification_on")] = tr("Visión Nocturna (Encender)");
    m_actionNames[QStringLiteral("v_operator_mode_cycle_back")] = tr("Modo de Operador Anterior");
    m_actionNames[QStringLiteral("v_master_mode_set_nav")] = tr("Master Mode: NAV");
    m_actionNames[QStringLiteral("v_master_mode_set_scm")] = tr("Master Mode: SCM");

    // Movimiento, VTOL y Estado de Nave
    m_actionNames[QStringLiteral("v_ifcs_vector_decoupling_off")] = tr("Apagar Modo Desacoplado");
    m_actionNames[QStringLiteral("v_ifcs_vector_decoupling_on")] = tr("Encender Modo Desacoplado");
    m_actionNames[QStringLiteral("v_ifcs_vector_decoupling_toggle")] = tr("Modo Desacoplado (Alternar)");
    m_actionNames[QStringLiteral("v_retract_landing_system")] = tr("Retraer Tren de Aterrizaje");
    m_actionNames[QStringLiteral("v_strafe_trim_set_100_short")] = tr("Strafe Trim 100%");
    m_actionNames[QStringLiteral("v_toggle_jump_request")] = tr("Solicitar Salto Quantum");
    m_actionNames[QStringLiteral("v_toggle_qdrive_engagement")] = tr("Iniciar Salto Quantum");
    m_actionNames[QStringLiteral("v_transform_cycle")] = tr("Cambiar Estado de Nave (Alas)");
    m_actionNames[QStringLiteral("v_transform_deploy")] = tr("Desplegar Estado de Nave");
    m_actionNames[QStringLiteral("v_transform_retract")] = tr("Retraer Estado de Nave");
    m_actionNames[QStringLiteral("v_vtol_off")] = tr("Apagar VTOL");
    m_actionNames[QStringLiteral("v_vtol_on")] = tr("Encender VTOL");

    // Vistas
    m_actionNames[QStringLiteral("v_view_pitch")] = tr("Vista (Cabeceo)");
    m_actionNames[QStringLiteral("v_view_yaw")] = tr("Vista (Guiñada)");
    m_actionNames[QStringLiteral("v_dock_toggle_view")] = tr("Vista de Acoplamiento");

    // Sensores y Radares
    m_actionNames[QStringLiteral("v_auto_targeting_toggle_short")] = tr("Auto-Targeting (Alternar)");
    m_actionNames[QStringLiteral("v_look_ahead_start_target_tracking")] = tr("Look Ahead (Fijar Objetivo)");
    m_actionNames[QStringLiteral("v_target_unlock")] = tr("Desfijar Objetivo");
    m_actionNames[QStringLiteral("v_target_cycle_hostile_reset")] = tr("Reiniciar Fijación de Enemigos");
    m_actionNames[QStringLiteral("v_target_cycle_in_view_fwd")] = tr("Fijar Siguiente Objetivo a la vista");
    m_actionNames[QStringLiteral("v_target_cycle_in_view_reset")] = tr("Reiniciar Fijación en la vista");
    m_actionNames[QStringLiteral("v_invoke_ping")] = tr("Ping de Radar");
    m_actionNames[QStringLiteral("v_dec_scan_focus_level")] = tr("Reducir Foco de Escáner");
    m_actionNames[QStringLiteral("v_inc_scan_focus_level")] = tr("Aumentar Foco de Escáner");
    m_actionNames[QStringLiteral("v_scanning_trigger_scan")] = tr("Escanear");

    // Minería y Salvataje
    m_actionNames[QStringLiteral("v_mining_throttle")] = tr("Acelerador del Láser de Minería");
    m_actionNames[QStringLiteral("v_toggle_mining_laser_fire")] = tr("Disparar Láser de Minería");
    m_actionNames[QStringLiteral("v_toggle_mining_laser_type")] = tr("Cambiar Tipo de Láser");
    m_actionNames[QStringLiteral("v_salvage_toggle_fire_focused")] = tr("Disparar Rayo de Salvataje");

    // Torretas
    m_actionNames[QStringLiteral("turret_gyromode")] = tr("Modo Giroscópico (Torreta)");
    m_actionNames[QStringLiteral("turret_change_position")] = tr("Cambiar Posición (Torreta)");
    m_actionNames[QStringLiteral("turret_esp_toggle")] = tr("Asistencia ESP (Torreta)");

    // Combate
    m_actionNames[QStringLiteral("v_weapon_preset_attack")] = tr("Preseteo de Ataque");
    m_actionNames[QStringLiteral("v_weapon_preset_emp")] = tr("Activar EMP");
    m_actionNames[QStringLiteral("v_weapon_preset_next")] = tr("Siguiente Grupo de Armas");
    m_actionNames[QStringLiteral("v_weapon_preset_prev")] = tr("Grupo de Armas Anterior");
    m_actionNames[QStringLiteral("v_weapon_staggered_fire_off")] = tr("Apagar Fuego Escalonado");
    m_actionNames[QStringLiteral("v_weapon_staggered_fire_on")] = tr("Encender Fuego Escalonado");
    m_actionNames[QStringLiteral("v_weapon_staggered_fire_toggle")] = tr("Fuego Escalonado (Alternar)");
    m_actionNames[QStringLiteral("v_weapon_cycle_missile_back")] = tr("Misil Anterior");
    m_actionNames[QStringLiteral("v_weapon_decrease_max_missiles")] = tr("Disminuir Misiles Simultáneos");
    m_actionNames[QStringLiteral("v_weapon_increase_max_missiles")] = tr("Aumentar Misiles Simultáneos");
    m_actionNames[QStringLiteral("v_weapon_launch_missile_cinematic_hold")] = tr("Cámara Cinemática de Misil");
    m_actionNames[QStringLiteral("v_weapon_reset_max_missiles")] = tr("Reiniciar Misiles Simultáneos");
    m_actionNames[QStringLiteral("v_shield_raise_level_back")] = tr("Escudos Atrás");
    m_actionNames[QStringLiteral("v_shield_raise_level_forward")] = tr("Escudos Adelante");
    m_actionNames[QStringLiteral("v_shield_raise_level_left")] = tr("Escudos Izquierda");
    m_actionNames[QStringLiteral("v_shield_raise_level_right")] = tr("Escudos Derecha");
    m_actionNames[QStringLiteral("v_shield_reset_level")] = tr("Reiniciar Escudos");
    m_actionNames[QStringLiteral("v_weapon_countermeasure_decoy_burst_decrease")] = tr("Reducir Ráfaga de Señuelos");
    m_actionNames[QStringLiteral("v_weapon_countermeasure_decoy_burst_increase")] = tr("Aumentar Ráfaga de Señuelos");
    m_actionNames[QStringLiteral("v_weapon_countermeasure_decoy_launch_panic")] = tr("Lanzar Señuelos (Pánico)");

    // Distribución de Energía (Triángulo)
    m_actionNames[QStringLiteral("v_engineering_assignment_engine_decrease")] = tr("Energía a Motores (Reducir)");
    m_actionNames[QStringLiteral("v_engineering_assignment_engine_increase")] = tr("Energía a Motores (Aumentar)");
    m_actionNames[QStringLiteral("v_engineering_assignment_shields_decrease")] = tr("Energía a Escudos (Reducir)");
    m_actionNames[QStringLiteral("v_engineering_assignment_shields_increase")] = tr("Energía a Escudos (Aumentar)");
    m_actionNames[QStringLiteral("v_engineering_assignment_weapons_decrease")] = tr("Energía a Armas (Reducir)");
    m_actionNames[QStringLiteral("v_engineering_assignment_weapons_increase")] = tr("Energía a Armas (Aumentar)");

    // Vehículos Terrestres y Otros
    m_actionNames[QStringLiteral("v_hud_open_scoreboard")] = tr("Abrir Marcador (Scoreboard)");
    m_actionNames[QStringLiteral("v_lights")] = tr("Luces");
    m_actionNames[QStringLiteral("v_lights_off")] = tr("Luces (Apagar)");
    m_actionNames[QStringLiteral("v_lights_on")] = tr("Luces (Encender)");
    m_actionNames[QStringLiteral("eva_view_pitch")] = tr("EVA Cabeceo");
    m_actionNames[QStringLiteral("eva_view_yaw")] = tr("EVA Guiñada");
    m_actionNames[QStringLiteral("v_boost")] = tr("Vehículo: Boost");
    m_actionNames[QStringLiteral("v_brake")] = tr("Vehículo: Frenar");
    m_actionNames[QStringLiteral("v_move")] = tr("Vehículo: Moverse");
    m_actionNames[QStringLiteral("v_move_back")] = tr("Vehículo: Reversa");
    m_actionNames[QStringLiteral("v_move_forward")] = tr("Vehículo: Acelerar");
    // v_yaw: NO redefinido a propósito - ya existe arriba (línea ~78, sección
    // "Movimiento") como "Guiñada (Yaw)" para la nave espacial. m_actionNames
    // es un QHash<QString,QString> plano sin distinción por categoría
    // (translateName() solo recibe el actionId, ver SCIntegrationManager.h),
    // así que Star Citizen reutiliza este mismo id para el vehículo terrestre
    // y para la nave - redefinirlo aquí habría pisado silenciosamente la
    // traducción de la nave (el uso principal de esta app) con "Vehículo:
    // Guiñada" para TODOS los contextos.
    m_actionNames[QStringLiteral("respawn")] = tr("Reaparecer");
    m_actionNames[QStringLiteral("emote_sit")] = tr("Emote: Sentarse");
    m_actionNames[QStringLiteral("headtrack_enabled")] = tr("Activar Headtracking (TrackIR/Tobii)");

    // Fase SC-4/SC-5.3: Master DB - the ~15 most critical Star Citizen
    // actions, seeded with a sensible default vJoy input so a brand-new
    // install (no prior actionmaps.xml export to load) still shows a full,
    // translated, ready-to-map action list rather than one full of blanks.
    // Star Citizen's own export only ever contains actions the user
    // explicitly rebound in-game - anything left at its in-game default
    // (very plausibly "Pitch"/"Yaw"/"Roll" themselves) is omitted from the
    // XML entirely, so without a seeded default here loadActionMaps()'
    // merge (Fase SC-4) would leave that action blank forever. loadProfile()
    // still overwrites this default the moment the user's own XML actually
    // specifies one (matching by actionId) - this is only a fallback.
    struct SeedAction { const char *category; const char *actionId; const char *displayName; const char *defaultInput; };
    static const SeedAction kSeedActions[] = {
        // Vuelo
        {"spaceship_movement", "v_pitch", "Pitch", "js1_y"},
        {"spaceship_movement", "v_yaw", "Yaw", "js1_x"},
        {"spaceship_movement", "v_roll", "Roll", "js1_z"},
        {"spaceship_movement", "v_afterburner", "Postquemador (Boost)", "js1_button1"},
        {"spaceship_movement", "v_space_brake", "Freno Espacial", "js1_button2"},
        {"spaceship_movement", "v_strafe_forward", "Strafe Adelante", "js2_y"},
        {"spaceship_movement", "v_strafe_back", "Strafe Atrás", "js2_slider1"},
        {"spaceship_movement", "v_strafe_vertical", "Strafe Arriba/Abajo", "js2_z"},
        {"spaceship_movement", "v_strafe_lateral", "Strafe Izq/Der", "js2_x"},
        // Armas y Defensas
        {"spaceship_weapons", "v_weapon_preset_fire_guns0", "Disparar Grupo 1", "js1_button3"},
        {"spaceship_weapons", "v_weapon_preset_fire_guns1", "Disparar Grupo 2", "js1_button4"},
        {"spaceship_missiles", "v_weapon_toggle_launch_missile", "Lanzar Misil", "js1_button5"},
        {"spaceship_defensive", "v_weapon_countermeasure_decoy_launch", "Lanzar Señuelos (Decoys)", "js1_button6"},
        // Otros
        {"spaceship_general", "v_toggle_quantum_mode", "Modo Quantum", "js1_button7"},
        {"spaceship_general", "v_toggle_landing_system", "Tren de Aterrizaje", "js1_button8"},
    };

    // displayName is intentionally unused here now (i18n): m_actionNames is
    // fully populated by the tr()-wrapped block above, keyed by these same
    // actionIds - writing seed.displayName into it here too would silently
    // clobber those translated entries with this array's own untranslated
    // hardcoded text every time the app starts.
    for (const SeedAction &seed : kSeedActions) {
        m_binds.append(SCActionBind{QString::fromLatin1(seed.category), QString::fromLatin1(seed.actionId),
                                     QString::fromLatin1(seed.defaultInput)});
    }
}

QString SCIntegrationManager::detectInstallPath() const
{
    return QDir(kDefaultInstallPath).exists() ? kDefaultInstallPath : QString();
}

QString SCIntegrationManager::detectMappingsFolder() const
{
    if (QDir(kDefaultMappingsFolder).exists()) {
        return kDefaultMappingsFolder;
    }
    for (const QString &candidate : kAlternateMappingsFolders) {
        if (QDir(candidate).exists()) {
            return candidate;
        }
    }
    return QString();
}

QStringList SCIntegrationManager::getAvailableXmlFiles(const QString &folderPath) const
{
    const QDir dir(folderPath);
    if (!dir.exists()) {
        return {};
    }
    return dir.entryList(QStringList{QStringLiteral("*.xml")}, QDir::Files, QDir::Name);
}

bool SCIntegrationManager::loadProfile(const QString &filePath)
{
    if (!loadActionMaps(filePath)) {
        qWarning() << "SCIntegrationManager::loadProfile: failed to load" << filePath
                   << "- see prior warning for the specific cause (missing file, no <ActionMaps> root, or malformed XML).";
        return false;
    }
    emit bindsChanged();
    return true;
}

bool SCIntegrationManager::exportProfile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "SCIntegrationManager: could not open" << filePath << "for writing:" << file.errorString();
        return false;
    }

    // Group binds by category, preserving first-seen order (master-DB
    // actions first, same order they were seeded in the constructor) rather
    // than sorting alphabetically. Actions with no input assigned yet are
    // skipped outright - Star Citizen treats an omitted <action> the same
    // way it treats one with a blank <rebind input="">.
    QStringList categoryOrder;
    QHash<QString, QList<const SCActionBind *>> byCategory;
    for (const SCActionBind &bind : m_binds) {
        if (bind.input.isEmpty()) {
            continue;
        }
        if (!byCategory.contains(bind.category)) {
            categoryOrder.append(bind.category);
        }
        byCategory[bind.category].append(&bind);
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(1);
    xml.writeStartDocument();

    xml.writeStartElement(QStringLiteral("ActionMaps"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1"));
    xml.writeAttribute(QStringLiteral("optionsVersion"), QStringLiteral("2"));
    xml.writeAttribute(QStringLiteral("rebindVersion"), QStringLiteral("2"));
    xml.writeAttribute(QStringLiteral("profileName"), QStringLiteral("Gremlin Nexus"));

    xml.writeStartElement(QStringLiteral("CustomisationUIHeader"));
    xml.writeAttribute(QStringLiteral("label"), QStringLiteral("Gremlin Nexus"));
    xml.writeAttribute(QStringLiteral("description"), QString());
    xml.writeAttribute(QStringLiteral("image"), QString());
    xml.writeStartElement(QStringLiteral("devices"));
    for (int instance = 1; instance <= 2; ++instance) {
        xml.writeStartElement(QStringLiteral("joystick"));
        xml.writeAttribute(QStringLiteral("instance"), QString::number(instance));
        xml.writeEndElement(); // joystick
    }
    xml.writeEndElement(); // devices
    xml.writeEndElement(); // CustomisationUIHeader

    static const QString kVJoyProduct = QStringLiteral("vJoy Device  {BEAD1234-0000-0000-0000-504944564944}");
    for (int instance = 1; instance <= 2; ++instance) {
        xml.writeStartElement(QStringLiteral("options"));
        xml.writeAttribute(QStringLiteral("type"), QStringLiteral("joystick"));
        xml.writeAttribute(QStringLiteral("instance"), QString::number(instance));
        xml.writeAttribute(QStringLiteral("Product"), kVJoyProduct);
        xml.writeEndElement(); // options
    }

    xml.writeEmptyElement(QStringLiteral("modifiers"));

    for (const QString &category : std::as_const(categoryOrder)) {
        xml.writeStartElement(QStringLiteral("actionmap"));
        xml.writeAttribute(QStringLiteral("name"), category);
        for (const SCActionBind *bind : std::as_const(byCategory[category])) {
            xml.writeStartElement(QStringLiteral("action"));
            xml.writeAttribute(QStringLiteral("name"), bind->actionId);
            xml.writeStartElement(QStringLiteral("rebind"));
            xml.writeAttribute(QStringLiteral("input"), bind->input);
            xml.writeEndElement(); // rebind
            xml.writeEndElement(); // action
        }
        xml.writeEndElement(); // actionmap
    }

    xml.writeEndElement(); // ActionMaps
    xml.writeEndDocument();
    return true;
}

QVariantList SCIntegrationManager::scBindsAsVariantList() const
{
    QVariantList result;
    result.reserve(m_binds.size());
    for (const SCActionBind &bind : m_binds) {
        QVariantMap entry;
        entry[QStringLiteral("category")] = bind.category;
        entry[QStringLiteral("actionId")] = bind.actionId;
        entry[QStringLiteral("actionName")] = translateName(bind.actionId);
        entry[QStringLiteral("input")] = bind.input;
        entry[QStringLiteral("formattedInput")] = formatExpectedVJoyInput(bind.input);
        result.append(entry);
    }
    return result;
}

bool SCIntegrationManager::loadActionMaps(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "SCIntegrationManager: could not open" << filePath << ":" << file.errorString();
        return false;
    }

    // Fase SC-5: loop rather than assume the very first start element found
    // is <ActionMaps> - QXmlStreamReader already skips whitespace/comments/
    // the "<?xml ...?>" declaration on its own, but this stays tolerant of
    // any other stray leading element some exports have been seen to
    // include, rather than bailing out the moment the first element isn't
    // an exact match.
    QXmlStreamReader xml(&file);
    bool foundRoot = false;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("ActionMaps")) {
            foundRoot = true;
            break;
        }
        xml.skipCurrentElement();
    }
    if (!foundRoot) {
        qWarning() << "SCIntegrationManager:" << filePath << "has no <ActionMaps> root element";
        return false;
    }

    QList<SCActionBind> parsedBinds;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("actionmap")) {
            parseActionMap(xml, parsedBinds);
        } else {
            // <CustomisationUIHeader>, <options>, or anything else this
            // reader doesn't need yet.
            xml.skipCurrentElement();
        }
    }

    if (xml.hasError()) {
        qWarning() << "SCIntegrationManager: XML error in" << filePath << "at line" << xml.lineNumber() << "column"
                    << xml.columnNumber() << ":" << xml.errorString();
        return false;
    }

    // Fase SC-4: merge into the master DB rather than replacing it outright
    // - an action already known (master-seeded, or from an earlier load) has
    // its input updated in place; anything new is appended.
    for (const SCActionBind &parsed : parsedBinds) {
        bool matched = false;
        for (SCActionBind &existing : m_binds) {
            if (existing.actionId == parsed.actionId) {
                existing.input = parsed.input;
                matched = true;
                break;
            }
        }
        if (!matched) {
            m_binds.append(parsed);
        }
    }
    return true;
}

void SCIntegrationManager::parseActionMap(QXmlStreamReader &xml, QList<SCActionBind> &outBinds)
{
    const QString category = xml.attributes().value(QLatin1String("name")).toString();

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("action")) {
            parseAction(xml, category, outBinds);
        } else {
            xml.skipCurrentElement();
        }
    }
}

void SCIntegrationManager::parseAction(QXmlStreamReader &xml, const QString &category, QList<SCActionBind> &outBinds)
{
    const QString actionId = xml.attributes().value(QLatin1String("name")).toString();

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("rebind")) {
            // Star Citizen's own "unassigned" marker: a trailing space (e.g.
            // "js2_ ") rather than an omitted attribute or an empty string
            // outright - trimmed() catches all three the same way.
            const QString input = xml.attributes().value(QLatin1String("input")).toString().trimmed();
            if (!input.isEmpty()) {
                outBinds.append(SCActionBind{category, actionId, input});
            }
        }
        xml.skipCurrentElement();
    }
}

const QList<SCIntegrationManager::SCActionBind> &SCIntegrationManager::actionBinds() const
{
    return m_binds;
}

QString SCIntegrationManager::translateName(const QString &rawId) const
{
    return m_actionNames.value(rawId, rawId);
}

SCIntegrationManager::ParsedScInput SCIntegrationManager::parseScInput(const QString &scInput) const
{
    ParsedScInput result;

    static const QRegularExpression kInputPattern(QStringLiteral("^js(\\d+)_(.+)$"));
    const QRegularExpressionMatch inputMatch = kInputPattern.match(scInput);
    if (!inputMatch.hasMatch()) {
        return result;
    }

    result.vJoyId = inputMatch.captured(1).toInt();
    // Fase SC-6: the game's own "jsN" enumeration is unreliable when two
    // vJoy devices share VID/PID/product string, so the raw captured id is
    // translated through the user-editable jsN -> vJoy id map before use -
    // falls back to the raw id itself if no override has been set.
    result.vJoyId = m_jsTranslations.value(result.vJoyId, result.vJoyId);
    const QString control = inputMatch.captured(2);

    static const QRegularExpression kButtonPattern(QStringLiteral("^button(\\d+)$"));
    const QRegularExpressionMatch buttonMatch = kButtonPattern.match(control);
    if (buttonMatch.hasMatch()) {
        // SC's own button numbering is 1-based; GremblingEx's targetButton is 0-based.
        result.isButton = true;
        result.index = buttonMatch.captured(1).toInt() - 1;
        result.valid = true;
        return result;
    }

    static const QHash<QString, int> kAxisIndices{
        {QStringLiteral("x"), 0},     {QStringLiteral("y"), 1},     {QStringLiteral("z"), 2},
        {QStringLiteral("rotx"), 3},  {QStringLiteral("roty"), 4},  {QStringLiteral("rotz"), 5},
        {QStringLiteral("slider1"), 6}, {QStringLiteral("slider2"), 7},
    };
    const auto it = kAxisIndices.constFind(control);
    if (it == kAxisIndices.constEnd()) {
        // A hat/pov, keyboard, or mouse input - no vJoy target index to
        // cross-reference/format against.
        return result;
    }
    result.isButton = false;
    result.index = it.value();
    result.valid = true;
    return result;
}

QString SCIntegrationManager::formatExpectedVJoyInput(const QString &scInput) const
{
    const ParsedScInput parsed = parseScInput(scInput);
    if (!parsed.valid) {
        return QString();
    }
    // Star Citizen button numbering (1-based) shown back to the user, not
    // GremblingEx's own 0-based targetButton index parseScInput() carries.
    return parsed.isButton ? QStringLiteral("vJoy %1 (Botón %2)").arg(parsed.vJoyId).arg(parsed.index + 1)
                            : QStringLiteral("vJoy %1 (Eje %2)").arg(parsed.vJoyId).arg(axisDisplayName(parsed.index));
}

void SCIntegrationManager::setJsTranslation(int jsIndex, int vJoyId)
{
    if (m_jsTranslations.value(jsIndex, jsIndex) != vJoyId) {
        m_jsTranslations[jsIndex] = vJoyId;
        emit bindsChanged(); // Fundamental: obliga a la tabla QML a repintarse al instante con los nuevos valores.
    }
}

int SCIntegrationManager::getJsTranslation(int jsIndex) const
{
    return m_jsTranslations.value(jsIndex, jsIndex);
}

QString SCIntegrationManager::resolvePhysicalSource(const QString &scInput, const QJsonObject &gremblingProfile) const
{
    const ParsedScInput parsed = parseScInput(scInput);
    if (!parsed.valid) {
        return QString();
    }

    QString sourceDevice;
    int sourceIndex = -1;
    bool found = false;

    // Fase SC-2: gremblingProfile is a *live GremblingEx profile* - the same
    // {"profileName", "bindings": [...]} shape ProfileManager::loadProfile()/
    // serializeProfile() use (see ProfileManager.h's schema docs), not a
    // pre-indexed lookup table - so this scans every binding rather than a
    // single hash lookup. Only a plain "ButtonRemapHandler"/"CurveHandler"
    // binding is matched (see this method's own header docs for why a
    // wrapped target is deliberately left unmatched for now).
    const QJsonArray bindings = gremblingProfile.value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue &bindingValue : bindings) {
        const QJsonObject binding = bindingValue.toObject();
        const QString actionType = binding.value(QStringLiteral("actionType")).toString();

        if (parsed.isButton) {
            if (actionType != QStringLiteral("ButtonRemapHandler") ||
                binding.value(QStringLiteral("targetOutputId")).toInt() != parsed.vJoyId ||
                binding.value(QStringLiteral("targetButton")).toInt() != parsed.index) {
                continue;
            }
            sourceDevice = binding.value(QStringLiteral("sourceDevice")).toString();
            sourceIndex = binding.value(QStringLiteral("sourceButton")).toInt();
        } else {
            if (actionType != QStringLiteral("CurveHandler") ||
                binding.value(QStringLiteral("targetOutputId")).toInt() != parsed.vJoyId ||
                binding.value(QStringLiteral("targetAxis")).toInt() != parsed.index) {
                continue;
            }
            sourceDevice = binding.value(QStringLiteral("sourceDevice")).toString();
            sourceIndex = binding.value(QStringLiteral("sourceAxis")).toInt();
        }

        found = true;
        break;
    }

    if (!found) {
        return QString();
    }

    QString deviceName = QStringLiteral("Dispositivo desconocido");
    for (const DeviceInfo &device : DeviceManager::instance().getConnectedDevices()) {
        if (device.systemPath == sourceDevice) {
            deviceName = device.deviceName;
            break;
        }
    }

    // Physical index shown to the user is always 1-based, regardless of
    // Star Citizen's or vJoy's own numbering.
    return parsed.isButton ? QStringLiteral("%1 (Botón %2)").arg(deviceName).arg(sourceIndex + 1)
                            : QStringLiteral("%1 (Eje %2)").arg(deviceName, axisDisplayName(sourceIndex));
}
