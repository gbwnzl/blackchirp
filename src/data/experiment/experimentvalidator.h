#ifndef EXPERIMENTVALIDATOR_H
#define EXPERIMENTVALIDATOR_H

#include <QString>
#include <QVariant>
#include <map>

class BlackchirpCSV;

class ExperimentValidator
{
public:
    using ValueRange = std::pair<double,double>;
    using ObjectMap = std::map<QString,ValueRange>;
    using ValidationMap = std::map<QString,ObjectMap>;

    ExperimentValidator();
    explicit ExperimentValidator(BlackchirpCSV* csv, int num, QString path);

    bool validate(const QString key, const QVariant val);
    QString errorString() const { return d_errorString; }
    void setValidationMap(const ValidationMap &m){ d_valMap = m; }

    bool saveValidation(int num);

private:
    ValidationMap d_valMap;
    QString d_errorString;
};

#endif // EXPERIMENTVALIDATOR_H
