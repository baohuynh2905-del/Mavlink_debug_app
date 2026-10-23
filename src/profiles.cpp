#include "profiles.h"

#include "link_health.h"

#include <QSettings>

static QString check(const ConnectionProfile &p)
{
    if (p.name.trimmed().isEmpty()) return QStringLiteral("Profile name is empty");
    if (const QString e = validateEndpoint(p.ip, p.port); !e.isEmpty()) return e;
    if (p.timeoutS < 1 || p.timeoutS > 60) return QStringLiteral("Timeout must be 1-60 s");
    return {};
}

QList<ConnectionProfile> loadProfiles(QSettings &s)
{
    QList<ConnectionProfile> out;
    const int n = s.beginReadArray(QStringLiteral("profiles"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        ConnectionProfile p{s.value(QStringLiteral("name")).toString().trimmed(), s.value(QStringLiteral("ip")).toString().trimmed(),
                            s.value(QStringLiteral("port")).toInt(), s.value(QStringLiteral("timeoutS"), 3).toInt(),
                            s.value(QStringLiteral("model"), QStringLiteral("VIO")).toString()};
        if (check(p).isEmpty()) out << p;  // never trust stored values
    }
    s.endArray();
    return out;
}

void saveProfiles(QSettings &s, const QList<ConnectionProfile> &profiles)
{
    s.remove(QStringLiteral("profiles"));
    s.beginWriteArray(QStringLiteral("profiles"), int(profiles.size()));
    for (int i = 0; i < profiles.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("name"), profiles[i].name);
        s.setValue(QStringLiteral("ip"), profiles[i].ip);
        s.setValue(QStringLiteral("port"), profiles[i].port);
        s.setValue(QStringLiteral("timeoutS"), profiles[i].timeoutS);
        s.setValue(QStringLiteral("model"), profiles[i].model);
    }
    s.endArray();
}

QString upsertProfile(QList<ConnectionProfile> &profiles, ConnectionProfile p)
{
    p.name = p.name.trimmed();
    p.ip = p.ip.trimmed();
    if (const QString e = check(p); !e.isEmpty()) return e;
    for (ConnectionProfile &x : profiles) {
        if (x.name.compare(p.name, Qt::CaseInsensitive) == 0) {
            x = p;
            return {};
        }
    }
    profiles << p;
    return {};
}
