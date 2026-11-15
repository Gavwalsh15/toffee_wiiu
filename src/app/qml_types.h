#pragma once

#include "dsu/dsu_server.h"
#include "inputs/input_controller.h"
#include "stream/stream_controller.h"
#include <QtQml/qqmlregistration.h>

struct StreamControllerForeign {
    Q_GADGET
    QML_FOREIGN(StreamController)
    QML_NAMED_ELEMENT(StreamController)
};

struct InputControllerForeign {
    Q_GADGET
    QML_FOREIGN(InputController)
    QML_NAMED_ELEMENT(InputController)
};

struct DsuServerForeign {
    Q_GADGET
    QML_FOREIGN(DsuServer)
    QML_NAMED_ELEMENT(DsuServer)
};
