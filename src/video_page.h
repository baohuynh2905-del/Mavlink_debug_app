#pragma once

#include <QImage>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class VideoPlayer;

// Paints the latest frame scaled to fit, keeping the aspect ratio.
class VideoView : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;
    void setImage(const QImage &img);
    void clear(const QString &message);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QImage m_image;
    QString m_message = QStringLiteral("No video");
};

class VideoPage : public QWidget
{
    Q_OBJECT
public:
    explicit VideoPage(QWidget *parent = nullptr);
    ~VideoPage() override;
    void setPayloadIp(const QString &ip);  // updates the URL unless the user edited it
    VideoPlayer *player() const { return m_player; }

private:
    void onStartStop();
    void onState();
    void refreshStatus();

    VideoPlayer *m_player;
    VideoView *m_view;
    QLineEdit *m_url;
    QComboBox *m_transport;
    QSpinBox *m_latency;
    QPushButton *m_startStop;
    QLabel *m_status;
    bool m_urlEdited = false;
};
