#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _Goodix5135Tls Goodix5135Tls;

Goodix5135Tls *
goodix5135_tls_new (const guint8 *psk,
                    gsize         psk_len,
                    GError      **error);

void
goodix5135_tls_free (Goodix5135Tls *self);

gboolean
goodix5135_tls_feed (Goodix5135Tls *self,
                     const guint8  *input,
                     gsize          input_len,
                     GByteArray    *wire_output,
                     GByteArray    *app_output,
                     GError       **error);

gboolean
goodix5135_tls_is_established (Goodix5135Tls *self);

G_END_DECLS
