#ifndef TRACKINGVIEWWIDGET_H
#define TRACKINGVIEWWIDGET_H

#include <QWidget>
#include <QGridLayout>
#include <QList>
#include <QEvent>
#include <qwt6/qwt_plot.h>
#include <qwt6/qwt_plot_curve.h>
#include "trackingplot.h"

class TrackingViewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrackingViewWidget(QWidget *parent = 0);
    ~TrackingViewWidget();

    //! Associates curves with which plot and axis they're displayed on
    struct CurveMetaData {
        QwtPlotCurve *curve; /*!< The curve */
        QVector<QPointF> data; /*!< The curve data */
        QString name; /*!< The name of the curve */
        int plotIndex; /*!< The index of the plot (in allPlots) the curve is plotted on */
        int axis; /*!< The y-axis on which the curve is plotted */
        bool isVisible; /*!< Whether the curve is visible */
    };

public slots:
    void initializeForExperiment();
    void pointUpdated(const QList<QPair<QString,QVariant> > list);



    void changeNumPlots();


private:
    QGridLayout *d_gridLayout = nullptr;
    QList<CurveMetaData> d_plotCurves;
    QList<TrackingPlot*> d_allPlots;

    void addNewPlot();

    void configureGrid();


};

#endif // TRACKINGVIEWWIDGET_H
