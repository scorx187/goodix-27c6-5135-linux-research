#pragma once

#include <glib.h>

#include "goodix5135-tls.h"

G_BEGIN_DECLS

#define GOODIX5135_TLS_PACK_FLAGS_CONTROL 0xb0U
#define GOODIX5135_TLS_PACK_FLAGS_DATA    0xb2U
#define GOODIX5135_TLS_PACK_HEADER_SIZE   4U
#define GOODIX5135_TLS_PACK_MAX_PAYLOAD   0xffffU

typedef struct _Goodix5135TlsTransport Goodix5135TlsTransport;

Goodix5135TlsTransport *
goodix5135_tls_transport_new (const guint8 *psk,
                              gsize         psk_len,
                              GError      **error);

void
goodix5135_tls_transport_free (Goodix5135TlsTransport *self);

gboolean
goodix5135_tls_transport_build_frame (guint8         flags,
                                      const guint8  *payload,
                                      gsize          payload_len,
                                      GByteArray    *frame,
                                      GError       **error);

gboolean
goodix5135_tls_transport_parse_frame (const guint8   *frame,
                                      gsize           frame_len,
                                      guint8         *flags,
                                      const guint8  **payload,
                                      gsize          *payload_len,
                                      GError        **error);

gboolean
goodix5135_tls_transport_feed_frame (Goodix5135TlsTransport *self,
                                     const guint8            *sensor_frame,
                                     gsize                    sensor_frame_len,
                                     GByteArray              *sensor_output_frame,
                                     GByteArray              *app_output,
                                     GError                 **error);

gboolean
goodix5135_tls_transport_is_established (Goodix5135TlsTransport *self);

G_END_DECLS
