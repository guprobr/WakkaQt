#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include <QDialog>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QList>
#include <QString>
#include <QTimer> 

class PreviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreviewDialog(QWidget *parent = nullptr);
    ~PreviewDialog() override;

    void setAudioFile(const QString &filePath);
    double getVolume() const;

protected:
    void accept() override;

private slots:
    void playAudio();
    void stopAudio();
    void updateVolume(int value);
    void updateDuration(); 

private:
    void setupUi();

    QSlider *volumeSlider;
    QLabel *volumeValueLabel;
    QLabel *durationLabel; 
    QPushButton *playButton;
    QPushButton *stopButton;
    QPushButton *renderButton;

    QString audioFilePath;
    QList<QMediaPlayer *> mediaPlayers;
    QList<QAudioOutput *> audioOutputs;
    QTimer *timer; // Timer to update duration
    int currentVolume;
};

#endif // PREVIEWDIALOG_H
