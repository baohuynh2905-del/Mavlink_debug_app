#pragma once

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QWidget>

#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class VideoPlayer;

// One line of the video overlay, stacked in a corner of the picture.
struct HudText {
    Qt::Alignment corner;
    QString text;
    QColor color;
};

// Paints the latest frame scaled to fit, keeping the aspect ratio, with an optional telemetry overlay.
class VideoView : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;
    void setImage(const QImage &img);
    void clear(const QString &message);
    // box: a rectangle in 0..1 picture coordinates (empty = none)
    void setHud(std::vector<HudText> items, const QRectF &box = {}, const QColor &boxColor = {});
    void setPickMode(bool on);  // a click then emits picked()

signals:
    void picked(QPointF pos);  // 0..1 picture coordinates

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    QRect pictureRect() const;

    QImage m_image;
    QString m_message = QStringLiteral("No video");
    std::vector<HudText> m_hud;
    QRectF m_box;
    QColor m_boxColor;
    bool m_pick = false;
};

class VideoPage : public QWidget
{
    Q_OBJECT
public:
    explicit VideoPage(QWidget *parent = nullptr);
    ~VideoPage() override;
    void setPayloadIp(const QString &ip);  // updates the URL unless the user edited it
    VideoPlayer *player() const { return m_player; }
    VideoView *view() const { return m_view; }
    QWidget *settingsPanel() const { return m_settings; }  // URL, transport, latency; may be moved elsewhere

private:
    void onStartStop();
    void onState();
    void refreshStatus();

    VideoPlayer *m_player;
    VideoView *m_view;
    QWidget *m_settings;
    QLineEdit *m_url;
    QComboBox *m_transport;
    QSpinBox *m_latency;
    QPushButton *m_startStop;
    QLabel *m_status;
    bool m_urlEdited = false;
};
