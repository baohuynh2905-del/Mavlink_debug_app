#pragma once
// Named connection profiles persisted in QSettings. Entries that fail validation are dropped on load.

#include <QList>
#include <QString>

class QSettings;

struct ConnectionProfile {
    QString name;
    QString ip;
    int port = 14566;
    int timeoutS = 3;
};

QList<ConnectionProfile> loadProfiles(QSettings &s);
void saveProfiles(QSettings &s, const QList<ConnectionProfile> &profiles);
// Adds or replaces (same name, case-insensitive). Returns an error instead when the profile is invalid.
QString upsertProfile(QList<ConnectionProfile> &profiles, ConnectionProfile p);
