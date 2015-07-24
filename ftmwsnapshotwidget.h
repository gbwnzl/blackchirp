#ifndef FTMWSNAPSHOTWIDGET_H
#define FTMWSNAPSHOTWIDGET_H

#include <QWidget>

#include "fid.h"

class QThread;
class QListWidget;
class QSpinBox;
class QPushButton;
class SnapWorker;

class FtmwSnapshotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FtmwSnapshotWidget(int num, QWidget *parent = 0);
    ~FtmwSnapshotWidget();

    int count() const;
    bool isEmpty() const;
    QList<Fid> getSnapList() const;
    int snapListSize() const;
    Fid getSnapFid(int i) const;
    Fid getRefFid(int i);
    Fid getDiffFid(int i);

    QSize sizeHint() const;

signals:
    void loadFailed(const QString errMsg);
    void snapListChanged();
    void refChanged();
    void diffChanged();
    void finalizedList(const QList<Fid>);

public slots:
    void setSelectionEnabled(bool en);
    void setDiffMode(bool en);
    void setFinalizeEnabled(bool en);
    bool readSnapshots();
    void updateSnapList();
    void snapListUpdated(const QList<Fid> l);
    void finalize();

private:
    QListWidget *p_lw;
    QSpinBox *p_refBox, *p_diffBox;
    QPushButton *p_finalizeButton;
    QThread *p_workerThread;
    SnapWorker *p_sw;

    int d_num;
    bool d_busy, d_updateWhenDone;
    QList<Fid> d_snapList;
};

#endif // FTMWSNAPSHOTWIDGET_H
