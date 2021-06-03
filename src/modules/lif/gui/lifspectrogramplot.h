#ifndef LIFSPECTROGRAMPLOT_H
#define LIFSPECTROGRAMPLOT_H

#include "zoompanplot.h"

#include "lifconfig.h"

class QwtPlotSpectrogram;
class QwtMatrixRasterData;
class QwtPlotMarker;
class QMouseEvent;

class LifSpectrogramPlot : public ZoomPanPlot
{
    Q_OBJECT
public:
    LifSpectrogramPlot(QWidget *parent = nullptr);
    ~LifSpectrogramPlot();

    void prepareForExperiment(const LifConfig c);
    void updateData(const QVector<double> d, int numCols, double zMin, double zMax);

    void setZMax(double d);
    void replot();

public slots:
    void moveFreqCursor(QPoint pos);
    void moveDelayCursor(QPoint pos);
    void buildContextMenu(QMouseEvent *me);

signals:
    void freqSlice(int delayIndex);
    void delaySlice(int freqIndex);

private:
    QwtMatrixRasterData *p_spectrogramData;
    QwtPlotSpectrogram *p_spectrogram;
    QwtPlotMarker *p_delayMarker, *p_freqMarker;
    bool d_enabled;
    bool d_delayDragging, d_freqDragging, d_grabDelay, d_grabFreq;


    // ZoomPanPlot interface
protected:
    void filterData();
    bool eventFilter(QObject *obj, QEvent *ev);
};

#endif // LIFSPECTROGRAMPLOT_H
