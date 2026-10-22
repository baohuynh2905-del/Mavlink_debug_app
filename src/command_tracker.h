#pragma once
// Follows each UI action from the SDK call, to its frame on the wire, to the payload's answer.
// Pure (fed with observed frames), so the correlation rules are unit tested.

#include "captured_frame.h"

#include <QString>

#include <deque>
#include <functional>

struct TrackedCommand {
    enum State {
        Queued,        // handed to the SDK, not yet seen on the wire
        NotSent,       // never observed on the wire within the timeout
        SendFailed,    // seen, but sendto() failed: it did not leave this PC
        Sent,          // on the wire, waiting for the defined acknowledgement
        NoAckDefined,  // on the wire; this message has no ACK (check telemetry)
        InProgress,    // ACK with MAV_RESULT_IN_PROGRESS / PARAM_ACK_IN_PROGRESS
        Accepted,
        Rejected,      // any other ACK result
        AckTimeout,    // no ACK in time: the payload may still have executed it
        Answered       // the requested message arrived, without an ACK (the VIO does this for REQUEST_MESSAGE)
    };

    quint64 id = 0;
    QString action;        // what the user did
    quint32 msgid = 0;     // expected outgoing message
    quint16 command = 0;   // COMMAND_LONG: MAV_CMD
    QString paramId;       // PARAM_EXT_SET: param_id
    std::function<bool(const MavFrame &)> content;  // optional extra check on the outgoing frame
    quint32 responseMsgid = 0;  // request commands: the message that answers them (0 = none)
    State state = Queued;
    qint64 requestedMs = 0, sentMs = -1, ackMs = -1;  // monotonic
    quint64 txFrame = 0, ackFrame = 0;                // inspector frame indexes
    int targetSystem = -1, targetComponent = -1;
    int ackFrom = -1;                                 // compid that answered
    int result = -1, progress = -1;                   // raw ACK result / progress
};

class CommandTracker
{
public:
    static constexpr qint64 kSendTimeoutMs = 3000;  // SDK queue: one message per 20 ms
    static constexpr qint64 kAckTimeoutMs = 3000;
    static constexpr size_t kMaxHistory = 500;

    // Register a UI action before calling the SDK. Returns its id.
    quint64 expect(const QString &action, quint32 msgid, quint16 command, const QString &paramId, qint64 nowMs,
                   std::function<bool(const MavFrame &)> content = {}, quint32 responseMsgid = 0);

    // Feed every captured frame (TX and RX) in order. Returns true when a command changed.
    bool onFrame(const CapturedFrame &c);
    bool tick(qint64 nowMs);  // timeouts

    const std::deque<TrackedCommand> &commands() const { return m_cmds; }
    quint64 changes() const { return m_changes; }  // bumps on every change, for cheap UI refresh

    static QString stateText(TrackedCommand::State s);

private:
    TrackedCommand *oldest(TrackedCommand::State s, quint32 msgid, quint16 command, const QString &paramId,
                           const MavFrame *frame = nullptr);

    std::deque<TrackedCommand> m_cmds;
    quint64 m_nextId = 1;
    quint64 m_changes = 0;
};

// The parameter id of a PARAM_EXT_SET / PARAM_EXT_ACK frame (char[16], not NUL-terminated at 16).
QString paramExtId(const MavFrame &f);
