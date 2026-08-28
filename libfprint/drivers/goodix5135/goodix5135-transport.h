/*
 * Goodix 27c6:5135 libfprint USB transport preparation.
 *
 * This layer may construct and fill FpiUsbTransfer objects, but does not
 * submit them. No Goodix protocol command is sent by this module.
 */

#pragma once

#include <glib.h>

#include "fpi-usb-transfer.h"

#include "goodix5135-request.h"

/*
 * Construct one unsubmitted bulk transfer from an already validated,
 * in-flight Goodix5135Request.
 *
 * BULK_IN:
 *   out_data must be NULL and out_data_length must be zero.
 *
 * BULK_OUT:
 *   out_data must contain exactly request->length bytes. The payload is
 *   copied so the returned transfer owns its buffer independently of
 *   the caller.
 *
 * Returns NULL if the request/payload contract is invalid.
 */
FpiUsbTransfer *goodix5135_transport_prepare_transfer (
                  FpDevice                *device,
                  const Goodix5135Request *request,
                  const guint8            *out_data,
                  gsize                    out_data_length);
