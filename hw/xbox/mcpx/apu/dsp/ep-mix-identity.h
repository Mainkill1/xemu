/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_EP_MIX_IDENTITY_H
#define XEMU_EP_MIX_IDENTITY_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* These exact digests are recognized for shadow/replay comparison only. */
static inline bool ep_mix_digest_known(const char *digest)
{
    return digest && (!strcmp(digest, "5471f7867c55afea5765c5c3e21065a6f456dc5a"
                                      "ab2103a146f06b1d07b1a118") ||
                      !strcmp(digest, "b4e7bcc65856da0dcbffe792ed3965c74f50ebb5"
                                      "49e359622bdf911428f40c36"));
}

/* These P-memory words changed between the captured pre/post states. This
 * masked identity is valid only for non-serving comparisons; it does not
 * establish that arbitrary values of the mutable words are safe to bypass. */
static inline uint32_t ep_mix_identity_word(unsigned index, uint32_t word)
{
    switch (index) {
    case 1:
    case 7:
    case 278:
    case 422:
    case 427:
    case 428:
    case 429:
    case 430:
        return 0;
    default:
        return word & 0xffffff;
    }
}
#endif
