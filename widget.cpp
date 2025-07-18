#include "widget.h"
#include "ui_widget.h"
#include "errordialog.h"
#include <QFileDialog>
#include <QRegularExpression>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QUrl>
#include <QDir>
#include <QDateTime>
#include <QCoreApplication>

// 定义最大并发下载数
#define MAX_CONCURRENT_DOWNLOADS 20 // 可以根据需要调整此值

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_totalTasksCount(0)      // 初始化总任务数为 0
    , m_completedTasksCount(0)  // 初始化已完成任务数
    , m_activeDownloadsCount(0) // 初始化活跃下载任务数
{
    ui->setupUi(this);

    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &Widget::onDownloadFinished);

    ui->labelLog->setText("等待任务开始...");
    ui->tabWidget->setCurrentIndex(0);
    m_combinedLogStream.setDevice(&m_combinedLogFile);
}

Widget::~Widget()
{
    // 确保在析构时关闭日志文件
    if (m_combinedLogFile.isOpen()) {
        m_combinedLogFile.close();
    }
    delete ui;
}

void Widget::on_pushButtonOpenTXT_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this,
                                                    "选择文本文件",
                                                    "",
                                                    "文本文件 (*.txt);;所有文件 (*.*)");

    if (!filePath.isEmpty()) {
        ui->lineEditTXTpath->setText(filePath);
        ui->labelLog->setText("TXT 文件已选择。");
    } else {
        ui->labelLog->setText("未选择 TXT 文件。");
    }
}

void Widget::on_lineEditTXTpath_editingFinished()
{
    QString currentPath = ui->lineEditTXTpath->text();
    if (!currentPath.isEmpty()) {
        if (!isValidPath(currentPath)) {
            QMessageBox::critical(this, "路径格式错误", "您输入的文本文件路径格式不正确，请检查。\n\n例如：\nWindows: C:\\Users\\User\\Documents\\urls.txt\nLinux/macOS: /home/user/documents/file.txt");
            ui->labelLog->setText("TXT 路径格式错误！");
        } else {
            ui->labelLog->setText("TXT 路径格式已检查。");
        }
    }
}

void Widget::on_pushButtonFolderSelect_clicked()
{
    QString dirPath = QFileDialog::getExistingDirectory(this,
                                                        "选择存储目录",
                                                        "",
                                                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dirPath.isEmpty()) {
        ui->lineEditFolderPath->setText(dirPath);
        ui->labelLog->setText("存储目录已选择。");
    } else {
        ui->labelLog->setText("未选择存储目录。");
    }
}

void Widget::on_lineEditFolderPath_editingFinished()
{
    QString currentPath = ui->lineEditFolderPath->text();
    if (!currentPath.isEmpty()) {
        if (!isValidPath(currentPath)) {
            QMessageBox::critical(this, "路径格式错误", "您输入的存储目录路径格式不正确，请检查。\n\n例如：\nWindows: C:\\Users\\User\\Downloads\nLinux/macOS: /home/user/downloads");
            ui->labelLog->setText("存储目录路径格式错误！");
        } else {
            ui->labelLog->setText("存储目录路径格式已检查。");
        }
    }
}

void Widget::on_pushButtonShowError_clicked()
{
    if (m_failedDownloads.isEmpty()) {
        QMessageBox::information(this, "下载错误信息", "没有失败的下载任务。");
    } else {
        ErrorDialog errorDialog(this);
        errorDialog.setErrorMessages(m_failedDownloads);
        errorDialog.exec();
    }
}

void Widget::on_pushButtonRefresh_clicked()
{
    ui->lineEditTXTpath->clear();
    ui->lineEditFolderPath->clear();
    ui->labelLog->setText("等待任务开始...");
    m_failedDownloads.clear();
    m_downloadQueue.clear(); // 清空下载队列
    m_totalTasksCount = 0;
    m_completedTasksCount = 0;
    m_activeDownloadsCount = 0; // 重置活跃下载数

    // 在刷新时关闭并重新打开日志文件
    if (m_combinedLogFile.isOpen()) {
        m_combinedLogFile.close();
    }

}

void Widget::on_pushButtonClose_clicked()
{
    QCoreApplication::quit();
}

bool Widget::isValidPath(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }

    QRegularExpression invalidChars("[<>:\"|?*]");
    if (path.contains(invalidChars)) {
        return false;
    }

    // 仅检查是否是绝对路径
    if (!QDir::isAbsolutePath(path)) {
        return false;
    }

    return true;
}

// 启动下一个下载任务
void Widget::startNextDownload()
{
    // 只有当有待下载任务且活跃下载数未达上限时才启动
    while (!m_downloadQueue.isEmpty() && m_activeDownloadsCount < MAX_CONCURRENT_DOWNLOADS) {
        DownloadTask task = m_downloadQueue.dequeue(); // 从队列中取出任务

        // 协议自动补全
        QString processedUrlString = task.originalUrl;
        // 如果原始URL字符串不包含 "://" (即没有明确的协议头)
        if (!processedUrlString.contains("://", Qt::CaseInsensitive)) {
            // 在前面添加 "http://"
            processedUrlString.prepend("http://");
        }

        QUrl finalUrl(processedUrlString); // 使用处理后的字符串构造QUrl

        // 重新校验URL的有效性
        if (!finalUrl.isValid()) {
            QString logMessage = QString("[%1] [下载失败] 无效URL: %2 (原因: 原始URL格式非法或补全后仍无效)")
                                     .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                                     .arg(task.originalUrl); // 日志中使用原始URL
            ui->labelLog->setText(QString("已处理 %1/%2: 跳过无效URL“%3”").arg(m_completedTasksCount + 1).arg(m_totalTasksCount).arg(task.fileName));
            m_combinedLogStream << logMessage << "\n";
            m_failedDownloads.append(QString("下载失败: %1 (错误: 原始格式非法或补全后仍无效)").arg(task.originalUrl));
            m_combinedLogStream.flush();
            m_completedTasksCount++; // 算作一个已处理的任务
            // 每次同步处理完一个任务，都检查是否所有任务都已完成
            if (m_completedTasksCount == m_totalTasksCount) {
                ui->labelLog->setText("所有任务已完成。");
                if (m_combinedLogFile.isOpen()) {
                    m_combinedLogStream << "--- 所有下载任务已完成 ---\n";
                    m_combinedLogStream.flush();
                    if (!m_failedDownloads.isEmpty()) {
                        m_combinedLogStream << "\n--- 以下文件未成功下载/处理 ---\n";
                        for (const QString &failedItem : m_failedDownloads) {
                            m_combinedLogStream << failedItem << "\n";
                        }
                        m_combinedLogStream << "--------------------------------\n";
                        m_combinedLogStream.flush();
                    }
                    m_combinedLogFile.close();
                }
            }
            continue; // 跳过此任务
        }


        // 文件已存在处理
        if (QFile::exists(task.savePath)) {
            QString logMessage = QString("[%1] [跳过] 文件已存在: %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(task.savePath);
            ui->labelLog->setText(QString("已处理 %1/%2: “%3”已存在，跳过").arg(m_completedTasksCount + 1).arg(m_totalTasksCount).arg(task.fileName));
            m_combinedLogStream << logMessage << "\n";
            m_completedTasksCount++; // 已存在文件也算一个已处理的任务
            m_combinedLogStream.flush();
            // 每次同步处理完一个任务，都检查是否所有任务都已完成
            if (m_completedTasksCount == m_totalTasksCount) {
                ui->labelLog->setText("所有任务已完成。");
                if (m_combinedLogFile.isOpen()) {
                    m_combinedLogStream << "--- 所有下载任务已完成 ---\n";
                    m_combinedLogStream.flush();
                    if (!m_failedDownloads.isEmpty()) {
                        m_combinedLogStream << "\n--- 以下文件未成功下载/处理 ---\n";
                        for (const QString &failedItem : m_failedDownloads) {
                            m_combinedLogStream << failedItem << "\n";
                        }
                        m_combinedLogStream << "--------------------------------\n";
                        m_combinedLogStream.flush();
                    }
                    m_combinedLogFile.close();
                }
            }
            continue;
        }

        // 目录创建失败处理
        QDir localDir(QFileInfo(task.savePath).absolutePath()); // 获取文件所在的目录
        if (!localDir.exists()) {
            if (!localDir.mkpath(".")) {
                QString errorMessage = QString("[%1] [下载失败] 无法创建本地目录: %2 (URL: %3)").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(QFileInfo(task.savePath).absolutePath()).arg(task.originalUrl);
                ui->labelLog->setText(QString("已处理 %1/%2: 下载错误“%3” (目录)").arg(m_completedTasksCount + 1).arg(m_totalTasksCount).arg(task.fileName));
                m_combinedLogStream << errorMessage << "\n";
                m_combinedLogStream.flush();
                m_failedDownloads.append(QString("下载失败: 目录创建失败: %1 (URL: %2)").arg(QFileInfo(task.savePath).absolutePath()).arg(task.originalUrl));
                m_completedTasksCount++; // 也算已处理的任务
                // 检查是否所有任务都已完成
                if (m_completedTasksCount == m_totalTasksCount) {
                    ui->labelLog->setText("所有任务已完成。");
                    if (m_combinedLogFile.isOpen()) {
                        m_combinedLogStream << "--- 所有下载任务已完成 ---\n";
                        m_combinedLogStream.flush();
                        if (!m_failedDownloads.isEmpty()) {
                            m_combinedLogStream << "\n--- 以下文件未成功下载/处理 ---\n";
                            for (const QString &failedItem : m_failedDownloads) {
                                m_combinedLogStream << failedItem << "\n";
                            }
                            m_combinedLogStream << "--------------------------------\n";
                            m_combinedLogStream.flush();
                        }
                        m_combinedLogFile.close();
                    }
                }
                continue;
            }
        }

        // 准备下载请求
        QNetworkRequest request(finalUrl);
        QNetworkReply *reply = m_networkManager->get(request);
        reply->setProperty("savePath", task.savePath);
        reply->setProperty("originalUrl", task.originalUrl); // 原始URL
        reply->setProperty("fileName", task.fileName);

        QString logMessage = QString("[%1] [开始下载] %2 -> %3").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(task.originalUrl).arg(task.savePath);
        ui->labelLog->setText(QString("正在下载 %1/%2: “%3”...").arg(m_completedTasksCount + 1).arg(m_totalTasksCount).arg(task.fileName)); // 更新进度
        m_combinedLogStream << logMessage << "\n";
        m_combinedLogStream.flush();

        m_activeDownloadsCount++; // 活跃下载数
    }
}

void Widget::on_pushButtonConfirm_clicked()
{
    // 在开始新任务前清空所有状态
    m_failedDownloads.clear();
    m_downloadQueue.clear(); // 清空上次遗留的队列
    m_totalTasksCount = 0;
    m_completedTasksCount = 0;
    m_activeDownloadsCount = 0; // 重置活跃下载数

    QString txtFilePath = ui->lineEditTXTpath->text();
    QString outputFolderPath = ui->lineEditFolderPath->text();

    // === 步骤 1: 基础路径验证 ===
    if (txtFilePath.isEmpty()) {
        QMessageBox::critical(this, "错误", "请选择或输入包含URL的文本文件路径。");
        ui->labelLog->setText("错误：未指定 TXT 文件。");
        return;
    }
    if (!QFile::exists(txtFilePath)) {
        QMessageBox::critical(this, "错误", "指定的URL列表文件不存在: " + txtFilePath);
        ui->labelLog->setText("错误：TXT 文件不存在。");
        return;
    }
    if (outputFolderPath.isEmpty()) {
        QMessageBox::critical(this, "错误", "请选择或输入本地存储目录。");
        ui->labelLog->setText("错误：未指定输出目录。");
        return;
    }
    if (!QDir(outputFolderPath).exists()) {
        QMessageBox::critical(this, "错误", "指定的本地存储目录不存在: " + outputFolderPath);
        ui->labelLog->setText("错误：输出目录不存在。");
        return;
    }

    // === 步骤 2: 初始化日志文件 ===
    m_logFilesFolderPath = QDir(outputFolderPath).filePath("logFiles");
    QDir logDir(m_logFilesFolderPath);
    if (!logDir.exists()) {
        if (!logDir.mkpath(".")) {
            QMessageBox::critical(this, "错误", "无法创建日志文件夹: " + m_logFilesFolderPath);
            ui->labelLog->setText("错误：无法创建日志文件夹。");
            return;
        }
    }

    QString currentDateTime = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString combinedLogFileName = "download_log_" + currentDateTime + ".txt";
    m_combinedLogFile.setFileName(QDir(m_logFilesFolderPath).filePath(combinedLogFileName));

    // 关闭旧文件（如果开着）并重新打开新文件
    if (m_combinedLogFile.isOpen()) {
        m_combinedLogFile.close();
    }
    if (!m_combinedLogFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        QMessageBox::critical(this, "错误", "无法打开日志文件: " + m_combinedLogFile.errorString());
        ui->labelLog->setText("错误：无法打开日志文件。");
        return;
    }
    m_combinedLogStream.setDevice(&m_combinedLogFile); // 确保关联到新的文件
    m_combinedLogStream << "--- 下载任务开始： " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << " ---\n";
    m_combinedLogStream.flush();


    // === 步骤 3: 读取 URL 文件，填充下载队列 ===
    QFile file(txtFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "文件打开失败", "无法打开URL列表文件: " + file.errorString());
        ui->labelLog->setText("错误：无法读取URL文件。");
        if (m_combinedLogFile.isOpen()) { m_combinedLogFile.close(); }
        return;
    }

    m_combinedLogStream << QString("[%1] [信息] URL列表文件已打开: %2\n").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(txtFilePath);
    m_combinedLogStream.flush();

    QTextStream in(&file);
    QStringList allLines; // 用于存储所有行
    while (!in.atEnd()) {
        allLines.append(in.readLine());
    }
    file.close(); // 提前关闭文件

    // **计算总任务数，并将有效任务添加到队列**
    for (const QString &line : allLines) {
        QString trimmedLine = line.trimmed();
        if (trimmedLine.isEmpty() || trimmedLine.startsWith("#")) {
            continue; // 跳过空行和注释行
        }

        m_totalTasksCount++; // 统计所有有效行作为总任务

        QUrl url(trimmedLine);
        QString fileName;

        QString pathInUrl = url.path();
        if (pathInUrl.startsWith('/')) {
            pathInUrl.remove(0, 1);
        }
        QFileInfo fileInfo(pathInUrl);
        fileName = fileInfo.fileName();

        if (fileName.isEmpty()) {
            QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
            if (!parts.isEmpty()) {
                fileName = parts.last();
            } else {
                fileName = "unknown_file";
            }
        }

        // === 处理主机名作为一级目录 ===
        QString hostName = url.host();
        if (hostName.isEmpty()) {
            hostName = "local_files"; // 默认目录名
        } else {
            hostName.replace('.', '_'); // 将主机名中的点号替换为下划线
        }

        QString relativeDirPathFromUrl = fileInfo.dir().path();
        if (relativeDirPathFromUrl.startsWith('/')) {
            relativeDirPathFromUrl.remove(0, 1);
        }
        QString fullLocalDirPath = QDir(outputFolderPath).filePath(hostName);
        fullLocalDirPath = QDir(fullLocalDirPath).filePath(relativeDirPathFromUrl);
        QString fullLocalFilePath = QDir(fullLocalDirPath).filePath(fileName);

        // 将任务添加到队列，暂时不在此处立即发起下载
        m_downloadQueue.enqueue({trimmedLine, fullLocalFilePath, fileName});
    }

    if (m_totalTasksCount == 0) {
        QMessageBox::information(this, "信息", "URL列表文件不包含有效的URL或所有行都被跳过。");
        ui->labelLog->setText("没有需要下载的文件。");
        m_combinedLogStream << "--- 没有文件需要下载 --- \n";
        m_combinedLogStream.flush();
        if (m_combinedLogFile.isOpen()) { m_combinedLogFile.close(); }
        return;
    }

    ui->labelLog->setText(QString("总任务数：%1，开始下载...").arg(m_totalTasksCount));
    m_combinedLogStream << QString("[%1] [信息] 共识别 %2 个有效任务，开始调度下载。\n")
                               .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                               .arg(m_totalTasksCount);
    m_combinedLogStream.flush();

    // 开始调度下载任务
    startNextDownload();

    // 改进点 5: 移除此处关于所有任务完成的判断，该判断现在完全由 onDownloadFinished 负责
    // ui->labelLog 初始状态会显示“总任务数：X，开始下载...”，后续由 onDownloadFinished 更新进度
}


void Widget::onDownloadFinished(QNetworkReply *reply)
{
    // 递减活跃下载数
    m_activeDownloadsCount--;

    QString savePath = reply->property("savePath").toString();
    QString originalUrl = reply->property("originalUrl").toString();
    QString fileName = reply->property("fileName").toString();

    QVariant statusCodeVariant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    int statusCode = statusCodeVariant.isValid() ? statusCodeVariant.toInt() : -1;

    QVariant contentTypeVariant = reply->header(QNetworkRequest::ContentTypeHeader);
    QString contentType = contentTypeVariant.isValid() ? contentTypeVariant.toString() : "";

    QString currentFileStatusMessage = ""; // 用于 UI labelLog 的状态信息
    QString logPrefix = "[下载失败]";      // 用于日志文件的前缀，默认设置为下载失败
    QString failedReasonForList = "";      // 记录到 m_failedDownloads的原因

    // 假设默认是失败
    bool isSuccessOrSkipped = false;

    if (reply->error() == QNetworkReply::NoError) {
        // HTTP请求成功完成
        if (statusCode >= 200 && statusCode < 300) {
            // 检查是否是HTML页面
            if (contentType.startsWith("text/html", Qt::CaseInsensitive) ||
                contentType.startsWith("application/xhtml+xml", Qt::CaseInsensitive)) {

                currentFileStatusMessage = QString("跳过 HTML 页面“%1”").arg(fileName);
                logPrefix = "[跳过]"; // 明确跳过HTML页面的前缀
                m_combinedLogStream << QString("[%1] %2 非文件内容 (HTML 页面): %3 (URL: %4)\n")
                                           .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                                           .arg(logPrefix)
                                           .arg(contentType)
                                           .arg(originalUrl);
                failedReasonForList = QString("内容为HTML页面: %1").arg(originalUrl);
                isSuccessOrSkipped = true; // 标记为已处理且非失败

            } else {
                // 尝试保存文件
                QFile file(savePath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(reply->readAll());
                    file.close();
                    currentFileStatusMessage = QString("“%1”下载完成").arg(fileName);
                    logPrefix = "[下载完成]"; // 下载完成的前缀
                    m_combinedLogStream << QString("[%1] %2 %3 -> %4\n")
                                               .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                                               .arg(logPrefix)
                                               .arg(originalUrl)
                                               .arg(savePath);
                    isSuccessOrSkipped = true; // 标记为已处理且非失败
                } else {
                    // 文件保存失败，归类为“下载失败”
                    currentFileStatusMessage = QString("下载错误“%1” (文件保存失败)").arg(fileName);
                    // logPrefix 保持默认的 "[下载失败]"
                    m_combinedLogStream << QString("[%1] %2 文件保存失败: 无法保存到 %3 (URL: %4) 错误: %5\n")
                                               .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                                               .arg(logPrefix)
                                               .arg(savePath)
                                               .arg(originalUrl)
                                               .arg(file.errorString());
                    failedReasonForList = QString("文件保存失败: %1 (错误: %2)").arg(originalUrl).arg(file.errorString());
                }
            }
        } else {
            // 服务器返回非2xx状态码 (如404, 500, 400等)，归类为“下载失败”
            currentFileStatusMessage = QString("下载错误“%1” (服务器返回 %2)").arg(fileName).arg(statusCode);
            // logPrefix 保持默认的 "[下载失败]"
            m_combinedLogStream << QString("[%1] %2 服务器返回状态码 %3: %4 (URL: %5)\n")
                                       .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                                       .arg(logPrefix)
                                       .arg(statusCode)
                                       .arg(reply->errorString())
                                       .arg(originalUrl);
            failedReasonForList = QString("服务器错误 %1 (%2): %3").arg(statusCode).arg(reply->errorString()).arg(originalUrl);
        }

    } else {
        // QNetworkReply 报告了网络层面的任何错误，统一归类为“下载失败”
        currentFileStatusMessage = QString("下载错误“%1” (网络或URL问题)").arg(fileName); // 统一的UI消息
        // logPrefix 保持默认的 "[下载失败]"
        m_combinedLogStream << QString("[%1] %2 下载失败: %3 错误: %4\n")
                                   .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                                   .arg(logPrefix) // 使用统一的 logPrefix
                                   .arg(originalUrl)
                                   .arg(reply->errorString());
        failedReasonForList = QString("[下载失败]: %1 (错误: %2)").arg(originalUrl).arg(reply->errorString());
    }

    // 添加到失败列表
    if (!isSuccessOrSkipped && !failedReasonForList.isEmpty()) {
        m_failedDownloads.append(failedReasonForList);
    }

    m_combinedLogStream.flush(); // 确保日志写入文件

    reply->deleteLater(); // 释放QNetworkReply对象

    m_completedTasksCount++; // 增加已完成任务计数

    // 根据完成进度更新 labelLog
    if (m_completedTasksCount < m_totalTasksCount) {
        ui->labelLog->setText(QString("已完成 %1/%2: %3").arg(m_completedTasksCount).arg(m_totalTasksCount).arg(currentFileStatusMessage));
    } else {
        ui->labelLog->setText("所有任务已完成。");
        if (m_combinedLogFile.isOpen()) {
            m_combinedLogStream << "--- 所有下载任务已完成 --- \n";
            m_combinedLogStream.flush();

            if (!m_failedDownloads.isEmpty()) {
                m_combinedLogStream << "\n--- 以下文件未成功下载/处理 ---\n";
                for (const QString &failedItem : m_failedDownloads) {
                    m_combinedLogStream << failedItem << "\n";
                }
                m_combinedLogStream << "--------------------------------\n";
                m_combinedLogStream.flush();
            }
            m_combinedLogFile.close();
        }
    }

    // 调度下一个下载任务
    startNextDownload();
}
