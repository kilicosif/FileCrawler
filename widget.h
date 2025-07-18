#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QTextStream>
#include <QQueue> // 新增：用于下载队列

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

// 定义一个结构体来存储下载任务的信息
struct DownloadTask {
    QString originalUrl;
    QString savePath;
    QString fileName;
};

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void on_pushButtonOpenTXT_clicked();
    void on_lineEditTXTpath_editingFinished();
    void on_pushButtonFolderSelect_clicked();
    void on_lineEditFolderPath_editingFinished();
    void on_pushButtonConfirm_clicked();
    void onDownloadFinished(QNetworkReply *reply);
    void on_pushButtonShowError_clicked();
    void on_pushButtonRefresh_clicked();
    void on_pushButtonClose_clicked();

private:
    Ui::Widget *ui;
    QNetworkAccessManager *m_networkManager;
    QFile m_combinedLogFile;
    QTextStream m_combinedLogStream;
    QString m_logFilesFolderPath;
    QStringList m_failedDownloads;

    int m_totalTasksCount;
    int m_completedTasksCount;  // 已处理完成的任务数
    int m_activeDownloadsCount; // 正在进行的下载任务数

    QQueue<DownloadTask> m_downloadQueue; // 待下载的任务队列

    bool isValidPath(const QString &path);
    void startNextDownload(); // 新增：开始下一个下载任务的函数
};
#endif // WIDGET_H
