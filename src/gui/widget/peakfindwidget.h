#ifndef PEAKFINDWIDGET_H
#define PEAKFINDWIDGET_H

#include <QWidget>

#include <QSortFilterProxyModel>
#include <QPair>
#include <QVector>
#include <QPointF>

#include "experiment.h"
#include "peakfinder.h"
#include "peaklistmodel.h"

namespace Ui {
class PeakFindWidget;
}

class PeakFindWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PeakFindWidget(Ft ft, QWidget *parent = 0);
    ~PeakFindWidget();

signals:
    void peakList(const QList<QPointF>);

public slots:
    void newFt(const Ft ft);
    void newPeakList(const QList<QPointF> pl);
    void findPeaks();
    void removeSelected();
    void updateRemoveButton();
    void changeScaleFactor(double scf);
    void launchOptionsDialog();
    void launchExportDialog();

private:
    Ui::PeakFindWidget *ui;

    PeakFinder *p_pf;
    PeakListModel *p_listModel;
    QSortFilterProxyModel *p_proxy;

    double d_minFreq;
    double d_maxFreq;
    double d_snr;
    int d_winSize;
    int d_polyOrder;
    int d_number;
    bool d_busy;
    bool d_waiting;
    Ft d_currentFt;
    QThread *p_thread;
};

#endif // PEAKFINDWIDGET_H
