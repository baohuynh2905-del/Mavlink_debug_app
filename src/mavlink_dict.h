#pragma once
// Metadata the generated C headers do not carry (units, enum names, bitmask flags, descriptions),
// read from the MAVLink XML definitions shipped with the SDK next to those headers.

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

class MavlinkDict
{
public:
    struct Field {
        QString units, enumName, invalid, description;
        bool bitmask = false;
    };

    // Loads rootFile and its <include>s from dir. Returns false with a reason in *error.
    bool load(const QString &dir, const QString &rootFile = QStringLiteral("ardupilotmega.xml"),
              QString *error = nullptr);
    bool isLoaded() const { return !m_fields.isEmpty(); }
    QStringList files() const { return m_files; }

    const Field *field(quint32 msgid, const QString &name) const;
    QString messageDescription(quint32 msgid) const { return m_msgDesc.value(msgid); }

    // "MAV_TYPE_GIMBAL" / "YAW_LOCK | NEUTRAL" style label, or empty when the value has no name.
    QString label(const Field &f, qint64 value) const;
    QString enumLabel(const QString &enumName, qint64 value) const { return m_enums.value(enumName).value(value); }

    // $MAVLINK_XML_DIR, else <app dir>/message_definitions, else the SDK copy this build used.
    static QString defaultDir();

private:
    bool loadFile(const QString &dir, const QString &file, QString *error);

    QHash<quint32, QHash<QString, Field>> m_fields;
    QHash<quint32, QString> m_msgDesc;
    QHash<QString, QMap<qint64, QString>> m_enums;
    QHash<QString, bool> m_enumBitmask;
    QStringList m_files;
};
