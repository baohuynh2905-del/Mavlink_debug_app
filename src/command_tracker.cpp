#include "command_tracker.h"

QString paramExtId(const MavFrame &f)
{
    char id[17] = {0};
    if (f.msgid == MAVLINK_MSG_ID_PARAM_EXT_SET) mavlink_msg_param_ext_set_get_param_id(&f.msg, id);
    else if (f.msgid == MAVLINK_MSG_ID_PARAM_EXT_ACK) mavlink_msg_param_ext_ack_get_param_id(&f.msg, id);
    return QString::fromLatin1(id);
}

quint64 CommandTracker::expect(const QString &action, quint32 msgid, quint16 command, const QString &paramId, qint64 nowMs,
                               std::function<bool(const MavFrame &)> content)
{
    TrackedCommand t;
    t.id = m_nextId++;
    t.action = action;
    t.msgid = msgid;
    t.command = command;
    t.paramId = paramId;
    t.content = std::move(content);
    t.requestedMs = nowMs;
    m_cmds.push_back(t);
    if (m_cmds.size() > kMaxHistory) m_cmds.pop_front();
    ++m_changes;
    return t.id;
}

TrackedCommand *CommandTracker::oldest(TrackedCommand::State s, quint32 msgid, quint16 command, const QString &paramId,
                                       const MavFrame *frame)
{
    for (TrackedCommand &t : m_cmds)
        if (t.state == s && t.msgid == msgid && t.command == command && t.paramId == paramId &&
            (!frame || !t.content || t.content(*frame)))
            return &t;
    return nullptr;
}

bool CommandTracker::onFrame(const CapturedFrame &c)
{
    const MavFrame &f = c.frame;
    if (f.status != MavFrame::Ok) return false;
    const qint64 ms = c.monoNs / 1000000;

    if (c.dir == CapturedFrame::Tx) {
        quint16 cmd = 0;
        QString pid;
        if (f.msgid == MAVLINK_MSG_ID_COMMAND_LONG) cmd = mavlink_msg_command_long_get_command(&f.msg);
        else if (f.msgid == MAVLINK_MSG_ID_PARAM_EXT_SET) pid = paramExtId(f);
        TrackedCommand *t = oldest(TrackedCommand::Queued, f.msgid, cmd, pid, &f);
        if (!t) return false;  // not from a UI action (SDK heartbeats, stream requests, ...)
        t->sentMs = ms;
        t->txFrame = c.index;
        t->targetSystem = f.targetSystem;
        t->targetComponent = f.targetComponent;
        if (c.sendErrno) t->state = TrackedCommand::SendFailed;
        else if (f.msgid == MAVLINK_MSG_ID_COMMAND_LONG || f.msgid == MAVLINK_MSG_ID_PARAM_EXT_SET) t->state = TrackedCommand::Sent;
        else t->state = TrackedCommand::NoAckDefined;
        ++m_changes;
        return true;
    }

    // RX acknowledgements
    TrackedCommand *t = nullptr;
    int result = -1, progress = -1;
    bool inProgress = false, accepted = false;
    if (f.msgid == MAVLINK_MSG_ID_COMMAND_ACK) {
        const quint16 cmd = mavlink_msg_command_ack_get_command(&f.msg);
        result = mavlink_msg_command_ack_get_result(&f.msg);
        progress = mavlink_msg_command_ack_get_progress(&f.msg);
        accepted = result == MAV_RESULT_ACCEPTED;
        inProgress = result == MAV_RESULT_IN_PROGRESS;
        // Oldest waiting command with this MAV_CMD whose target is the answering component
        for (TrackedCommand &x : m_cmds) {
            if ((x.state == TrackedCommand::Sent || x.state == TrackedCommand::InProgress) &&
                x.msgid == MAVLINK_MSG_ID_COMMAND_LONG && x.command == cmd &&
                (x.targetComponent <= 0 || x.targetComponent == f.compid)) {
                t = &x;
                break;
            }
        }
    } else if (f.msgid == MAVLINK_MSG_ID_PARAM_EXT_ACK) {
        result = mavlink_msg_param_ext_ack_get_param_result(&f.msg);
        accepted = result == PARAM_ACK_ACCEPTED;
        inProgress = result == PARAM_ACK_IN_PROGRESS;
        const QString pid = paramExtId(f);
        t = oldest(TrackedCommand::Sent, MAVLINK_MSG_ID_PARAM_EXT_SET, 0, pid);
        if (!t) t = oldest(TrackedCommand::InProgress, MAVLINK_MSG_ID_PARAM_EXT_SET, 0, pid);
    }
    if (!t) return false;
    t->ackMs = ms;
    t->ackFrame = c.index;
    t->ackFrom = f.compid;
    t->result = result;
    t->progress = progress;
    t->state = accepted ? TrackedCommand::Accepted : inProgress ? TrackedCommand::InProgress : TrackedCommand::Rejected;
    ++m_changes;
    return true;
}

bool CommandTracker::tick(qint64 nowMs)
{
    bool changed = false;
    for (TrackedCommand &t : m_cmds) {
        if (t.state == TrackedCommand::Queued && nowMs - t.requestedMs > kSendTimeoutMs) {
            t.state = TrackedCommand::NotSent;
            changed = true;
        } else if ((t.state == TrackedCommand::Sent || t.state == TrackedCommand::InProgress) &&
                   nowMs - (t.ackMs >= 0 ? t.ackMs : t.sentMs) > kAckTimeoutMs) {
            t.state = TrackedCommand::AckTimeout;
            changed = true;
        }
    }
    if (changed) ++m_changes;
    return changed;
}

QString CommandTracker::stateText(TrackedCommand::State s)
{
    switch (s) {
    case TrackedCommand::Queued: return QStringLiteral("Queued in SDK");
    case TrackedCommand::NotSent: return QStringLiteral("Not seen on wire");
    case TrackedCommand::SendFailed: return QStringLiteral("Send failed");
    case TrackedCommand::Sent: return QStringLiteral("Sent, awaiting ACK");
    case TrackedCommand::NoAckDefined: return QStringLiteral("Sent (no ACK defined)");
    case TrackedCommand::InProgress: return QStringLiteral("In progress");
    case TrackedCommand::Accepted: return QStringLiteral("Accepted");
    case TrackedCommand::Rejected: return QStringLiteral("Rejected");
    case TrackedCommand::AckTimeout: return QStringLiteral("No ACK (timeout)");
    }
    return {};
}
