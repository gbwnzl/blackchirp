 

HEADERS += \
    $$PWD/ioboard.h

SOURCES += \
    $$PWD/ioboard.cpp


equals(IOBOARD,0) {
    HEADERS += $$PWD/virtualioboard.h
	SOURCES += $$PWD/virtualioboard.cpp
}
equals(IOBOARD,1) {
    HEADERS += $$PWD/labjacku3.h \
	           $$PWD/u3.h
	SOURCES += $$PWD/labjacku3.cpp \
	           $$PWD/u3.cpp
}