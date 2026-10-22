#include "mavlink_dict.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QXmlStreamReader>

QString MavlinkDict::defaultDir()
{
    const QString env = qEnvironmentVariable("MAVLINK_XML_DIR");
    if (!env.isEmpty()) return env;
    const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/message_definitions");
    if (QDir(local).exists()) return local;
    return QStringLiteral(MAVLINK_XML_DIR);  // shortcut: build-tree path; an install must ship the XML next to the binary
}

bool MavlinkDict::load(const QString &dir, const QString &rootFile, QString *error)
{
    m_fields.clear();
    m_msgDesc.clear();
    m_enums.clear();
    m_enumBitmask.clear();
    m_files.clear();
    return loadFile(dir, rootFile, error);
}

bool MavlinkDict::loadFile(const QString &dir, const QString &file, QString *error)
{
    if (m_files.contains(file)) return true;  // dialects include common.xml more than once
    m_files << file;

    QFile f(dir + QLatin1Char('/') + file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot open %1: %2").arg(f.fileName(), f.errorString());
        return false;
    }
    QXmlStreamReader x(&f);
    QString enumName;
    qint64 msgid = -1;

    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            const auto n = x.name();
            const auto a = x.attributes();
            if (n == QLatin1String("include")) {
                if (!loadFile(dir, x.readElementText().trimmed(), error)) return false;
            } else if (n == QLatin1String("enum")) {
                enumName = a.value(QLatin1String("name")).toString();
                if (a.value(QLatin1String("bitmask")) == QLatin1String("true")) m_enumBitmask[enumName] = true;
            } else if (n == QLatin1String("entry") && !enumName.isEmpty()) {
                bool ok = false;
                const qint64 v = a.value(QLatin1String("value")).toString().toLongLong(&ok, 0);
                if (ok) m_enums[enumName][v] = a.value(QLatin1String("name")).toString();
            } else if (n == QLatin1String("message")) {
                msgid = a.value(QLatin1String("id")).toLongLong();
            } else if (n == QLatin1String("field") && msgid >= 0) {
                Field fd;
                fd.units = a.value(QLatin1String("units")).toString();
                fd.enumName = a.value(QLatin1String("enum")).toString();
                fd.invalid = a.value(QLatin1String("invalid")).toString();
                fd.bitmask = a.value(QLatin1String("display")) == QLatin1String("bitmask");
                const QString name = a.value(QLatin1String("name")).toString();
                fd.description = x.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
                m_fields[quint32(msgid)][name] = fd;
            } else if (n == QLatin1String("description") && msgid >= 0) {  // field texts are consumed above
                m_msgDesc[quint32(msgid)] = x.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            }
        } else if (x.isEndElement()) {
            if (x.name() == QLatin1String("enum")) enumName.clear();
            else if (x.name() == QLatin1String("message")) msgid = -1;
        }
    }
    if (x.hasError()) {
        if (error) *error = QStringLiteral("%1:%2: %3").arg(file).arg(x.lineNumber()).arg(x.errorString());
        return false;
    }
    return true;
}

const MavlinkDict::Field *MavlinkDict::field(quint32 msgid, const QString &name) const
{
    auto m = m_fields.constFind(msgid);
    if (m == m_fields.cend()) return nullptr;
    auto f = m->constFind(name);
    return f == m->cend() ? nullptr : &*f;
}

QString MavlinkDict::label(const Field &f, qint64 value) const
{
    if (f.enumName.isEmpty()) return {};
    const auto e = m_enums.constFind(f.enumName);
    if (e == m_enums.cend()) return {};
    const bool bitmask = f.bitmask || m_enumBitmask.value(f.enumName);
    if (!bitmask) return e->value(value);

    QStringList names;
    qint64 rest = value;
    for (auto it = e->cbegin(); it != e->cend(); ++it) {
        if (it.key() > 0 && (value & it.key()) == it.key()) {
            names << it.value();
            rest &= ~it.key();
        }
    }
    if (rest) names << QStringLiteral("0x%1").arg(quint64(rest), 0, 16);  // bits with no name: shown, not hidden
    return names.isEmpty() ? (value == 0 ? QStringLiteral("(none)") : QString()) : names.join(QStringLiteral(" | "));
}
