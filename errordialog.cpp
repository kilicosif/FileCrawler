#include "errordialog.h"
#include "ui_errordialog.h"

#include <QTextEdit>
#include <QString>

ErrorDialog::ErrorDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ErrorDialog)
{
    ui->setupUi(this);
    setWindowTitle("下载失败详情");
}

ErrorDialog::~ErrorDialog()
{
    delete ui;
}

void ErrorDialog::setErrorMessages(const QStringList& messages)
{

    ui->textEdit->setText(messages.join("\n"));
    ui->textEdit->moveCursor(QTextCursor::Start);
}
