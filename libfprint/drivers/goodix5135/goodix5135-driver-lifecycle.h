/*
 * Goodix 27c6:5135 bridge from async callback drain to FpImageDevice.
 */

#pragma once

#include "goodix5135-io.h"

typedef struct _FpDevice FpDevice;

/*
 * Notify the driver that one asynchronous callback has finished all
 * higher-level completion processing and lifecycle pending accounting
 * may now be inspected for deactivation completion.
 */
void goodix5135_driver_async_drained (
       FpDevice                 *device,
       Goodix5135IoLifecycle    *io);
