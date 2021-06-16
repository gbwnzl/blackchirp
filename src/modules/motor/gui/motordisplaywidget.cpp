#include <src/modules/motor/gui/motordisplaywidget.h>

#include <algorithm>

#include <QSettings>

MotorDisplayWidget::MotorDisplayWidget(QWidget *parent) :
    QWidget(parent), SettingsStorage(BC::Key::motorDisplay),
    ui(new Ui::MotorDisplayWidget)
{
    ui->setupUi(this);

    ui->largeSlider1->setAxis(BlackChirp::MotorX);
    ui->largeSlider2->setAxis(BlackChirp::MotorT);
    ui->smallSlider1->setAxis(BlackChirp::MotorZ);
    ui->smallSlider2->setAxis(BlackChirp::MotorT);
    ui->timeXSlider->setAxis(BlackChirp::MotorX);
    ui->timeYSlider->setAxis(BlackChirp::MotorY);
    ui->timeZSlider->setAxis(BlackChirp::MotorZ);

    d_sliders << ui->largeSlider1 << ui->largeSlider2 << ui->smallSlider1 << ui->smallSlider2
              << ui->timeXSlider << ui->timeYSlider << ui->timeZSlider;

    for(int i=0; i<d_sliders.size();i++)
        connect(d_sliders.at(i),&MotorSliderWidget::valueChanged,this,&MotorDisplayWidget::updatePlots);

    QSettings s(QSettings::SystemScope,QApplication::organizationName(),QApplication::applicationName());
    ui->smoothBox->setChecked(s.value(QString("motorDisplay/smooth"),false).toBool());
    ui->winBox->setValue(s.value(QString("motorDisplay/winSize"),21).toInt());
    ui->polyBox->setValue(s.value(QString("motorDisplay/polyOrder"),3).toInt());
    s.setValue(QString("motorDisplay/smooth"),ui->smoothBox->isChecked());
    s.setValue(QString("motorDisplay/winSize"),ui->winBox->value());
    s.setValue(QString("motorDisplay/polyOrder"),ui->polyBox->value());
    s.sync();

    updateCoefs();

    connect(ui->smoothBox,&QCheckBox::toggled,this,&MotorDisplayWidget::smoothBoxChecked);
    auto vc = static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged);
    connect(ui->winBox,vc,this,&MotorDisplayWidget::winSizeChanged);
    connect(ui->polyBox,vc,this,&MotorDisplayWidget::polySizeChanged);
}

MotorDisplayWidget::~MotorDisplayWidget()
{
    delete ui;
}

void MotorDisplayWidget::prepareForScan(const MotorScan s)
{
    setEnabled(s.isEnabled());

    if(s.isEnabled())
    {
        d_currentScan = s;

        Q_FOREACH(MotorSliderWidget *w,d_sliders)
        {
            w->blockSignals(true);
            w->setRange(s);
            w->blockSignals(false);
        }

        ui->motorLargeSpectrogramPlot->prepareForScan(s);
        ui->motorTimePlot->prepareForScan(s);
        ui->motorSmallSpectrogramPlot->prepareForScan(s);

        ui->smoothBox->setEnabled(true);
        ui->polyBox->setEnabled(ui->smoothBox->isChecked());
        ui->winBox->setEnabled(ui->smoothBox->isChecked());

        ui->winBox->setRange(5,d_currentScan.tPoints() - d_currentScan.tPoints() % 2 - 1);

        updatePlots();
    }
    else
    {
        ui->smoothBox->setEnabled(false);
        ui->polyBox->setEnabled(false);
        ui->winBox->setEnabled(false);
    }
}

void MotorDisplayWidget::newMotorData(const MotorScan s)
{
    d_currentScan = s;
    updatePlots();
}

void MotorDisplayWidget::updatePlots()
{
    if(ui->smoothBox->isChecked())
    {
        //prepare smoothed slices; update plots
        QVector<double> sliceZPlot = d_currentScan.smoothSlice(ui->motorLargeSpectrogramPlot->leftAxis(),ui->motorLargeSpectrogramPlot->bottomAxis()
                                                         ,ui->largeSlider1->axis(),ui->largeSlider1->currentIndex(),
                                                         ui->largeSlider2->axis(),ui->largeSlider2->currentIndex(),d_coefs);

        ui->motorLargeSpectrogramPlot->updateData(sliceZPlot,d_currentScan.numPoints(ui->motorLargeSpectrogramPlot->bottomAxis()));

        QVector<double> sliceXYPlot = d_currentScan.smoothSlice(ui->motorSmallSpectrogramPlot->leftAxis(),ui->motorSmallSpectrogramPlot->bottomAxis()
                                                          ,ui->smallSlider1->axis(),ui->smallSlider1->currentIndex(),
                                                          ui->smallSlider2->axis(),ui->smallSlider2->currentIndex(),d_coefs);
        ui->motorSmallSpectrogramPlot->updateData(sliceXYPlot,d_currentScan.numPoints(ui->motorSmallSpectrogramPlot->bottomAxis()));

        QVector<QPointF> timeTrace = d_currentScan.smoothtTrace(ui->timeXSlider->currentIndex(),ui->timeYSlider->currentIndex(),ui->timeZSlider->currentIndex(),d_coefs);
        ui->motorTimePlot->updateData(timeTrace);
    }

    else
    {
        //show raw data; no smoothing
        QVector<double> sliceZPlot = d_currentScan.slice(ui->motorLargeSpectrogramPlot->leftAxis(),ui->motorLargeSpectrogramPlot->bottomAxis()
                                                         ,ui->largeSlider1->axis(),ui->largeSlider1->currentIndex(),
                                                         ui->largeSlider2->axis(),ui->largeSlider2->currentIndex());

        ui->motorLargeSpectrogramPlot->updateData(sliceZPlot,d_currentScan.numPoints(ui->motorLargeSpectrogramPlot->bottomAxis()));

        QVector<double> sliceXYPlot = d_currentScan.slice(ui->motorSmallSpectrogramPlot->leftAxis(),ui->motorSmallSpectrogramPlot->bottomAxis()
                                                          ,ui->smallSlider1->axis(),ui->smallSlider1->currentIndex(),
                                                          ui->smallSlider2->axis(),ui->smallSlider2->currentIndex());
        ui->motorSmallSpectrogramPlot->updateData(sliceXYPlot,d_currentScan.numPoints(ui->motorSmallSpectrogramPlot->bottomAxis()));

        QVector<QPointF> timeTrace = d_currentScan.tTrace(ui->timeXSlider->currentIndex(),ui->timeYSlider->currentIndex(),ui->timeZSlider->currentIndex());
        ui->motorTimePlot->updateData(timeTrace);
    }
}

void MotorDisplayWidget::smoothBoxChecked(bool checked)
{
    ui->winBox->setEnabled(checked);
    ui->polyBox->setEnabled(checked);

    QSettings s(QSettings::SystemScope,QApplication::organizationName(),QApplication::applicationName());
    s.setValue(QString("motorDisplay/smooth"),checked);
    s.sync();

    updatePlots();
}

void MotorDisplayWidget::updateCoefs()
{
    d_coefs = Analysis::calcSavGolCoefs(ui->winBox->value(),ui->polyBox->value());
}

void MotorDisplayWidget::winSizeChanged(int w)
{
    if(w <= 0 || w >= d_currentScan.tPoints())
        return;

    if(!(w%2))
    {
        ui->winBox->blockSignals(true);
        ui->winBox->setValue(w-1);
        ui->winBox->blockSignals(false);
        w -= 1;
    }

    ui->polyBox->blockSignals(true);
    ui->polyBox->setRange(2,w-1);
    ui->polyBox->blockSignals(false);

    if(ui->polyBox->value() >= w)
    {
        ui->polyBox->blockSignals(true);
        ui->polyBox->setValue(w-1);
        ui->polyBox->blockSignals(false);
    }

    updateCoefs();
    updatePlots();

}

void MotorDisplayWidget::polySizeChanged(int p)
{
    int newP = qBound(2,p,ui->winBox->value()-1);
    if(newP != p)
    {
        ui->polyBox->blockSignals(true);
        ui->polyBox->setValue(newP);
        ui->polyBox->blockSignals(false);
    }

    updateCoefs();
    updatePlots();
}
