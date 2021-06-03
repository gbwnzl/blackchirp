#ifndef LIFSLICEPLOT_H
#define LIFSLICEPLOT_H

#include "zoompanplot.h"

class QwtPlotCurve;
class QwtPlotTextLabel;

class LifSlicePlot : public ZoomPanPlot
{
    Q_OBJECT
public:
    LifSlicePlot(QWidget *parent = nullptr);
    ~LifSlicePlot();

    void setXAxisTitle(QString title);
    void setName(QString name);

    void prepareForExperiment(double xMin, double xMax);
    void setData(const QVector<QPointF> d);
    void setPlotTitle(QString text);

public slots:
    void exportXY();

    // ZoomPanPlot interface
protected:
    void filterData();

    QwtPlotCurve *p_curve;
    QVector<QPointF> d_currentData;

    // ZoomPanPlot interface
protected:
    QMenu *contextMenu() override;
};

#endif // LIFSLICEPLOT_H
