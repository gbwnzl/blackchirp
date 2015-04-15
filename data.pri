SOURCES += loghandler.cpp \
    $$PWD/fid.cpp \
    $$PWD/ftworker.cpp \
    $$PWD/experiment.cpp \
    $$PWD/ftmwconfig.cpp \
    $$PWD/chirpconfig.cpp \
    $$PWD/pulsegenconfig.cpp

HEADERS += loghandler.h \
    $$PWD/fid.h \
    $$PWD/ftworker.h \
    $$PWD/experiment.h \
    $$PWD/ftmwconfig.h \
    $$PWD/gpuaverager.h \
    $$PWD/chirpconfig.h \
    $$PWD/pulsegenconfig.h

gpu-cuda {
    OTHER_FILES += $$PWD/gpuaverager.cu
}
