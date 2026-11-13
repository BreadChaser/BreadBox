#pragma once
#include "ui_destination.h"

/* Optional scene is borrowed until the Share controller returns. A NULL scene
 * retains the standalone menu; share_about_run(true) never offers local service. */
void share_destination_run(const ui_destination *scene);
