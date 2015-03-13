#include "trackingviewwidget.h"
#include <QSettings>
#include <QInputDialog>
#include <qwt6/qwt_date.h>

TrackingViewWidget::TrackingViewWidget(QWidget *parent) :
    QWidget(parent)
{    
    QSettings s;
    int numPlots = qBound(1,s.value(QString("trackingWidget/numPlots"),4).toInt(),9);
    for(int i=0; i<numPlots;i++)
        addNewPlot();

    configureGrid();
}

TrackingViewWidget::~TrackingViewWidget()
{

}

void TrackingViewWidget::initializeForExperiment()
{
    d_plotCurves.clear();

    for(int i=0; i<d_allPlots.size(); i++)
    {
        d_allPlots[i]->detachItems();
        d_allPlots[i]->replot();
    }
}

void TrackingViewWidget::pointUpdated(const QList<QPair<QString, QVariant> > list)
{
    for(int i=0; i<list.size(); i++)
    {
        //first, determine if the QVariant contains a number
        //no need to plot the data if it's not a number
        bool ok = false;
        double value = list.at(i).second.toDouble(&ok);
        if(!ok)
            continue;

        //create point to be plotted
        QPointF newPoint(QwtDate::toDouble(QDateTime::currentDateTime()),value);

        //locate curve by name and append point
        bool foundCurve = false;
        for(int j=0; j<d_plotCurves.size(); j++)
        {
            if(list.at(i).first == d_plotCurves.at(j).name)
            {
                d_plotCurves[j].data.append(newPoint);
                d_plotCurves[j].curve->setSamples(d_plotCurves.at(j).data);
                if(d_plotCurves.at(j).isVisible)
                    d_allPlots.at(d_plotCurves.at(j).plotIndex)->replot();

                foundCurve = true;
                break;
            }
        }

        if(foundCurve)
            continue;

        //if we reach this point, a new curve and metadata struct need to be created
        QSettings s;
        CurveMetaData md;
        md.data.reserve(100);
        md.data.append(newPoint);

        md.name = list.at(i).first;

        //Create curve
        QwtPlotCurve *c = new QwtPlotCurve(md.name);
        c->setRenderHint(QwtPlotItem::RenderAntialiased);
        md.curve = c;
        c->setSamples(md.data);

        s.beginGroup(QString("trackingWidget/curves/%1").arg(md.name));

        md.axis = s.value(QString("axis"),QwtPlot::yLeft).toInt();
        md.plotIndex = s.value(QString("plotIndex"),d_plotCurves.size()).toInt() % d_allPlots.size();
        md.isVisible = s.value(QString("isVisible"),true).toBool();

        c->setAxes(QwtPlot::xBottom,md.axis);
        c->setVisible(md.isVisible);

        QColor color = s.value(QString("color"),palette().color(QPalette::Text)).value<QColor>();
        c->setPen(color);
        c->attach(d_allPlots.at(md.plotIndex));
        d_allPlots.at(md.plotIndex)->initializeLabel(md.curve,md.isVisible);

        s.endGroup();

        d_plotCurves.append(md);
        d_allPlots.at(md.plotIndex)->replot();

    }
}

void TrackingViewWidget::changeNumPlots()
{
    bool ok = true;
    int newNum = QInputDialog::getInt(this,QString("BC: Change Number of Tracking Plots"),QString("Number of plots:"),d_allPlots.size(),1,9,1,&ok);

    if(!ok || newNum == d_allPlots.size())
        return;

    QSettings s;
    s.setValue(QString("trackingWidget/numPlots"),newNum);

    if(newNum > d_allPlots.size())
    {
        while(d_allPlots.size() < newNum)
            addNewPlot();
    }
    else
    {
        for(int i=0; i < d_plotCurves.size(); i++)
        {
            //reassign any curves that are on graphs about to be removed
            if(d_plotCurves.at(i).plotIndex >= newNum)
            {
                d_plotCurves.at(i).curve->detach();
                int newPlotIndex = d_plotCurves.at(i).plotIndex % newNum;
                d_plotCurves[i].plotIndex = newPlotIndex;
                d_plotCurves.at(i).curve->attach(d_allPlots.at(newPlotIndex));
            }
        }

        while(newNum < d_allPlots.size())
            delete d_allPlots.takeLast();
    }

    configureGrid();

    for(int i=0; i<d_allPlots.size(); i++)
        d_allPlots.at(i)->replot();

}

void TrackingViewWidget::addNewPlot()
{
    TrackingPlot *tp = new TrackingPlot(this);
    tp->setMinimumHeight(200);
    tp->setMinimumWidth(375);
    tp->installEventFilter(this);

    //signal-slot connections go here

    d_allPlots.append(tp);

}

void TrackingViewWidget::configureGrid()
{
    if(d_allPlots.size() < 1)
        return;

    if(d_gridLayout != nullptr)
        delete d_gridLayout;

    d_gridLayout = new QGridLayout();
    setLayout(d_gridLayout);

    switch(d_allPlots.size())
    {
    case 1:
        d_gridLayout->addWidget(d_allPlots[0],0,0,1,1);
        break;
    case 2:
        d_gridLayout->addWidget(d_allPlots[0],0,0,1,1);
        d_gridLayout->addWidget(d_allPlots[1],1,0,1,1);
        d_gridLayout->setRowStretch(0,1);
        d_gridLayout->setRowStretch(1,1);
        break;
    case 3:
        d_gridLayout->addWidget(d_allPlots[0],0,0,1,1);
        d_gridLayout->addWidget(d_allPlots[1],0,1,1,1);
        d_gridLayout->addWidget(d_allPlots[2],1,0,1,2);
        d_gridLayout->setRowStretch(0,1);
        d_gridLayout->setRowStretch(1,1);
        d_gridLayout->setColumnStretch(0,1);
        d_gridLayout->setColumnStretch(1,1);
        break;
    case 4:
        d_gridLayout->addWidget(d_allPlots[0],0,0,1,1);
        d_gridLayout->addWidget(d_allPlots[1],0,1,1,1);
        d_gridLayout->addWidget(d_allPlots[2],1,0,1,1);
        d_gridLayout->addWidget(d_allPlots[3],1,1,1,1);
        d_gridLayout->setRowStretch(0,1);
        d_gridLayout->setRowStretch(1,1);
        d_gridLayout->setColumnStretch(0,1);
        d_gridLayout->setColumnStretch(1,1);
        break;
    case 5:
        d_gridLayout->addWidget(d_allPlots[0],0,0,1,1);
        d_gridLayout->addWidget(d_allPlots[1],0,1,1,1);
        d_gridLayout->addWidget(d_allPlots[2],0,2,1,1);
        d_gridLayout->addWidget(d_allPlots[3],1,0,1,1);
        d_gridLayout->addWidget(d_allPlots[4],1,1,1,2);
        d_gridLayout->setRowStretch(0,1);
        d_gridLayout->setRowStretch(1,1);
        d_gridLayout->setColumnStretch(0,1);
        d_gridLayout->setColumnStretch(1,1);
        d_gridLayout->setColumnStretch(2,1);
        break;
    case 6:
        d_gridLayout->addWidget(d_allPlots[0],0,0,1,1);
        d_gridLayout->addWidget(d_allPlots[1],0,1,1,1);
        d_gridLayout->addWidget(d_allPlots[2],0,2,1,1);
        d_gridLayout->addWidget(d_allPlots[3],1,0,1,1);
        d_gridLayout->addWidget(d_allPlots[4],1,1,1,1);
        d_gridLayout->addWidget(d_allPlots[5],1,2,1,1);
        d_gridLayout->setRowStretch(0,1);
        d_gridLayout->setRowStretch(1,1);
        d_gridLayout->setColumnStretch(0,1);
        d_gridLayout->setColumnStretch(1,1);
        d_gridLayout->setColumnStretch(2,1);
        break;
    case 7:
        d_gridLayout->addWidget(d_allPlots[0],0,0,1,1);
        d_gridLayout->addWidget(d_allPlots[1],0,1,1,1);
        d_gridLayout->addWidget(d_allPlots[2],0,2,1,1);
        d_gridLayout->addWidget(d_allPlots[3],0,3,1,1);
        d_gridLayout->addWidget(d_allPlots[4],1,0,1,1);
        d_gridLayout->addWidget(d_allPlots[5],1,1,1,1);
        d_gridLayout->addWidget(d_allPlots[6],1,2,1,2);
        d_gridLayout->setRowStretch(0,1);
        d_gridLayout->setRowStretch(1,1);
        d_gridLayout->setColumnStretch(0,1);
        d_gridLayout->setColumnStretch(1,1);
        d_gridLayout->setColumnStretch(2,1);
        d_gridLayout->setColumnStretch(3,1);
        break;
    case 8:
        d_gridLayout->addWidget(d_allPlots[0],0,0,1,1);
        d_gridLayout->addWidget(d_allPlots[1],0,1,1,1);
        d_gridLayout->addWidget(d_allPlots[2],0,2,1,1);
        d_gridLayout->addWidget(d_allPlots[3],0,3,1,1);
        d_gridLayout->addWidget(d_allPlots[4],1,0,1,1);
        d_gridLayout->addWidget(d_allPlots[5],1,1,1,1);
        d_gridLayout->addWidget(d_allPlots[6],1,2,1,1);
        d_gridLayout->addWidget(d_allPlots[7],1,3,1,1);
        d_gridLayout->setRowStretch(0,1);
        d_gridLayout->setRowStretch(1,1);
        d_gridLayout->setColumnStretch(0,1);
        d_gridLayout->setColumnStretch(1,1);
        d_gridLayout->setColumnStretch(2,1);
        d_gridLayout->setColumnStretch(3,1);
        break;
    case 9:
    default:
        d_gridLayout->addWidget(d_allPlots[0],0,0,1,1);
        d_gridLayout->addWidget(d_allPlots[1],0,1,1,1);
        d_gridLayout->addWidget(d_allPlots[2],0,2,1,1);
        d_gridLayout->addWidget(d_allPlots[3],1,0,1,1);
        d_gridLayout->addWidget(d_allPlots[4],1,1,1,1);
        d_gridLayout->addWidget(d_allPlots[5],1,2,1,1);
        d_gridLayout->addWidget(d_allPlots[6],2,0,1,1);
        d_gridLayout->addWidget(d_allPlots[7],2,1,1,1);
        d_gridLayout->addWidget(d_allPlots[8],2,2,1,1);
        d_gridLayout->setRowStretch(0,1);
        d_gridLayout->setRowStretch(1,1);
        d_gridLayout->setRowStretch(2,1);
        d_gridLayout->setColumnStretch(0,1);
        d_gridLayout->setColumnStretch(1,1);
        d_gridLayout->setColumnStretch(2,1);
        break;
    }

}
