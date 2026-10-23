#pragma once
// Boot splash: an animated reticle, the station title and the real startup checks, then a fade into the
// main window. A click or any key skips it.

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <vector>

struct BootCheck {
    QString label, value;
    bool ok = true;
};

class BootSplash : public QWidget
{
    Q_OBJECT
public:
    explicit BootSplash(std::vector<BootCheck> checks, QWidget *parent = nullptr);
    void start();  // shows it centred on the screen

    static constexpr int kShowMs = 2600, kFadeMs = 350;

signals:
    void finished();  // the fade-out begins: show the main window now

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    void tick();
    void beginFade();

    std::vector<BootCheck> m_checks;
    QElapsedTimer m_clock;
    QTimer m_frame;
    qint64 m_fadeAt = -1;  // ms on m_clock when the fade started
};
