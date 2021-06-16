#ifndef ZOOMPANPLOT_H
#define ZOOMPANPLOT_H

#include <qwt6/qwt_plot.h>
#include <qwt6/qwt_symbol.h>

#include <src/data/storage/settingsstorage.h>

class QMenu;
class CustomTracker;
class BlackchirpPlotCurve;


namespace BC::Key {
static const QString axes("axes");
static const QString bottom("Bottom");
static const QString top("Top");
static const QString left("Left");
static const QString right("right");
static const QString zoomFactor("zoomFactor");
static const QString trackerDecimals("trackerDecimals");
static const QString trackerScientific("trackerScientific");
static const QString trackerEn("trackerEnabled");
}

/// \todo Handle plot grid in this class

class ZoomPanPlot : public QwtPlot, public SettingsStorage
{
    Q_OBJECT

public:
    explicit ZoomPanPlot(const QString name, QWidget *parent = nullptr);
    virtual ~ZoomPanPlot();

    bool isAutoScale();
    void resetPlot();
    void setAxisAutoScaleRange(QwtPlot::Axis axis, double min, double max);
    void setAxisAutoScaleMin(QwtPlot::Axis axis, double min);
    void setAxisAutoScaleMax(QwtPlot::Axis axis, double max);
    void expandAutoScaleRange(QwtPlot::Axis axis, double newValueMin, double newValueMax);
    void setXRanges(const QwtScaleDiv &bottom, const QwtScaleDiv &top);
    void setMaxIndex(int i){ d_maxIndex = i; }

    const QString d_name;

public slots:
    void autoScale();
    virtual void replot();
    void setZoomFactor(QwtPlot::Axis a, double v);
    void setTrackerEnabled(bool en);
    void setTrackerDecimals(QwtPlot::Axis a, int dec);
    void setTrackerScientific(QwtPlot::Axis a, bool sci);

    void setCurveColor(BlackchirpPlotCurve* curve);
    void setCurveLineThickness(BlackchirpPlotCurve* curve, double t);
    void setCurveLineStyle(BlackchirpPlotCurve* curve, Qt::PenStyle s);
    void setCurveMarker(BlackchirpPlotCurve* curve, QwtSymbol::Style s);
    void setCurveMarkerSize(BlackchirpPlotCurve* curve, int s);
    void setCurveVisible(BlackchirpPlotCurve* curve, bool v);
    void setCurveAxisY(BlackchirpPlotCurve* curve, QwtPlot::Axis a);

signals:
    void panningStarted();
    void panningFinished();
    void plotRightClicked(QMouseEvent *ev);
    void curveMoveRequested(BlackchirpPlotCurve*, int);

protected:
    int d_maxIndex;
    CustomTracker *p_tracker;
    struct AxisConfig {
        QwtPlot::Axis type;
        bool autoScale;
        bool override;
        double min;
        double max;
        double zoomFactor;
        QString name;

        explicit AxisConfig(QwtPlot::Axis t, const QString n) : type(t), autoScale(true), override(false),
            min(0.0), max(1.0), zoomFactor(0.1), name(n) {}
    };

    struct PlotConfig {
        QList<AxisConfig> axisList;

        bool xDirty;
        bool panning;
        QPoint panClickPos;

        PlotConfig() : xDirty(false), panning(false) {}
    };

    void setAxisOverride(QwtPlot::Axis axis, bool override = true);

    virtual void filterData() =0;
    virtual void resizeEvent(QResizeEvent *ev);
    virtual bool eventFilter(QObject *obj, QEvent *ev);
    virtual void pan(QMouseEvent *me);
    virtual void zoom(QWheelEvent *we);

    virtual void buildContextMenu(QMouseEvent *me);
    virtual QMenu *contextMenu();

private:
    PlotConfig d_config;
    int getAxisIndex(QwtPlot::Axis a);

    // QWidget interface
public:
    virtual QSize sizeHint() const;
    virtual QSize minimumSizeHint() const;

    // QWidget interface
protected:
    virtual void showEvent(QShowEvent *event);
};

#endif // ZOOMPANPLOT_H
