#ifndef PEAKLISTEXPORTDIALOG_H
#define PEAKLISTEXPORTDIALOG_H

#include <QDialog>
#include <QAbstractTableModel>

#include <QList>
#include <QPair>
#include <QSortFilterProxyModel>

#include "peaklistmodel.h"

namespace Ui {
class PeakListExportDialog;
}

class ShotsModel;

class PeakListExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PeakListExportDialog(const QList<QPointF> peakList, int number, QWidget *parent = 0);
    ~PeakListExportDialog();

public slots:
    void toggleButtons();
    void insertShot();
    void removeShots();
    void removePeaks();

private:
    Ui::PeakListExportDialog *ui;

    int d_number;
    QList<QPointF> d_peakList;
    ShotsModel *p_sm;
    PeakListModel *p_pm;
    QSortFilterProxyModel *p_proxy;

    // QDialog interface
public slots:
    void accept();
};

class ShotsModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit ShotsModel(QObject *parent = nullptr);
    void setList(const QList<QPair<int,double>> l);
    QList<QPair<int,double>> shotsList() const;
    void addEntry();
    void insertEntry(int pos);
    void removeEntries(QList<int> rows);

private:
    QList<QPair<int,double>> d_shotsList;

    // QAbstractItemModel interface
public:
    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    bool setData(const QModelIndex &index, const QVariant &value, int role);
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    Qt::ItemFlags flags(const QModelIndex &index) const;
};



#endif // PEAKLISTEXPORTDIALOG_H
