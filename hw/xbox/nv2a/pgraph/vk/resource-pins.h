/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_RESOURCE_PINS_H
#define HW_XBOX_NV2A_PGRAPH_VK_RESOURCE_PINS_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Pin callbacks are infallible retain/release operations. Resource acquisition
 * that can fail must complete before try_pin() is called.
 */
typedef void (*PGRAPHVkResourcePinCallback)(void *opaque, void *resource);

typedef struct PGRAPHVkResourcePin {
    const void *identity;
    void *resource;
    PGRAPHVkResourcePinCallback retain;
    PGRAPHVkResourcePinCallback release;
    void *callback_opaque;
} PGRAPHVkResourcePin;

typedef struct PGRAPHVkResourcePinRegistry {
    PGRAPHVkResourcePin *pins;
    size_t capacity;
    size_t count;
    size_t max_pins;
    bool submission_serial_valid;
    uint32_t submission_serial;
} PGRAPHVkResourcePinRegistry;

typedef enum PGRAPHVkResourcePinResult {
    PGRAPH_VK_RESOURCE_PIN_ADDED,
    PGRAPH_VK_RESOURCE_PIN_DUPLICATE,
    PGRAPH_VK_RESOURCE_PIN_FULL,
    PGRAPH_VK_RESOURCE_PIN_ALLOCATION_FAILED,
    PGRAPH_VK_RESOURCE_PIN_CONFLICT,
    PGRAPH_VK_RESOURCE_PIN_IN_FLIGHT,
    PGRAPH_VK_RESOURCE_PIN_INVALID,
} PGRAPHVkResourcePinResult;

static inline size_t pgraph_vk_resource_pin_hash(const void *identity)
{
    uintptr_t value = (uintptr_t)identity;

    /* Stable pointer mixer; capacity is always a power of two. */
    value ^= value >> 17;
    value *= (uintptr_t)0xed5ad4bbU;
    value ^= value >> 11;
    return (size_t)value;
}

static inline size_t pgraph_vk_resource_pin_find_index(
    const PGRAPHVkResourcePin *pins, size_t capacity, const void *identity)
{
    size_t index = pgraph_vk_resource_pin_hash(identity) & (capacity - 1);

    while (pins[index].identity && pins[index].identity != identity) {
        index = (index + 1) & (capacity - 1);
    }
    return index;
}

static inline bool pgraph_vk_resource_pin_registry_init(
    PGRAPHVkResourcePinRegistry *registry, size_t max_pins)
{
    if (!registry || max_pins > SIZE_MAX / 2) {
        return false;
    }

    *registry = (PGRAPHVkResourcePinRegistry){
        .max_pins = max_pins,
    };
    return true;
}

static inline bool pgraph_vk_resource_pin_registry_grow(
    PGRAPHVkResourcePinRegistry *registry)
{
    size_t new_capacity = registry->capacity ? registry->capacity * 2 : 8;

    if (new_capacity < registry->capacity ||
        new_capacity > SIZE_MAX / sizeof(*registry->pins)) {
        return false;
    }

    PGRAPHVkResourcePin *new_pins =
        g_try_malloc0_n(new_capacity, sizeof(*new_pins));
    if (!new_pins) {
        return false;
    }

    for (size_t i = 0; i < registry->capacity; i++) {
        PGRAPHVkResourcePin pin = registry->pins[i];
        if (!pin.identity) {
            continue;
        }
        size_t index = pgraph_vk_resource_pin_find_index(
            new_pins, new_capacity, pin.identity);
        new_pins[index] = pin;
    }

    g_free(registry->pins);
    registry->pins = new_pins;
    registry->capacity = new_capacity;
    return true;
}

static inline PGRAPHVkResourcePinResult
pgraph_vk_resource_pin_registry_try_pin(
    PGRAPHVkResourcePinRegistry *registry, const void *identity,
    void *resource, PGRAPHVkResourcePinCallback retain,
    PGRAPHVkResourcePinCallback release, void *callback_opaque)
{
    /*
     * identity is the logical ownership key. Reusing it with different pin
     * parameters is a conflict; distinct identities may intentionally retain
     * the same underlying resource independently.
     */
    if (!registry || !identity || !resource || !retain || !release) {
        return PGRAPH_VK_RESOURCE_PIN_INVALID;
    }
    if (registry->submission_serial_valid) {
        return PGRAPH_VK_RESOURCE_PIN_IN_FLIGHT;
    }

    if (registry->capacity) {
        size_t index = pgraph_vk_resource_pin_find_index(
            registry->pins, registry->capacity, identity);
        PGRAPHVkResourcePin *pin = &registry->pins[index];
        if (pin->identity) {
            return pin->resource == resource && pin->retain == retain &&
                           pin->release == release &&
                           pin->callback_opaque == callback_opaque
                       ? PGRAPH_VK_RESOURCE_PIN_DUPLICATE
                       : PGRAPH_VK_RESOURCE_PIN_CONFLICT;
        }
    }

    if (registry->count >= registry->max_pins) {
        return PGRAPH_VK_RESOURCE_PIN_FULL;
    }

    if (!registry->capacity ||
        registry->count + 1 > registry->capacity / 2) {
        if (!pgraph_vk_resource_pin_registry_grow(registry)) {
            return PGRAPH_VK_RESOURCE_PIN_ALLOCATION_FAILED;
        }
    }

    size_t index = pgraph_vk_resource_pin_find_index(
        registry->pins, registry->capacity, identity);
    retain(callback_opaque, resource);
    registry->pins[index] = (PGRAPHVkResourcePin){
        .identity = identity,
        .resource = resource,
        .retain = retain,
        .release = release,
        .callback_opaque = callback_opaque,
    };
    registry->count++;
    return PGRAPH_VK_RESOURCE_PIN_ADDED;
}

static inline bool pgraph_vk_resource_pin_registry_mark_submitted(
    PGRAPHVkResourcePinRegistry *registry, uint32_t submission_serial)
{
    if (!registry || registry->submission_serial_valid) {
        return false;
    }
    registry->submission_serial = submission_serial;
    registry->submission_serial_valid = true;
    return true;
}

static inline void pgraph_vk_resource_pin_registry_release_all(
    PGRAPHVkResourcePinRegistry *registry)
{
    for (size_t i = 0; i < registry->capacity; i++) {
        PGRAPHVkResourcePin *pin = &registry->pins[i];
        if (pin->identity) {
            pin->release(pin->callback_opaque, pin->resource);
        }
    }
    if (registry->pins) {
        memset(registry->pins, 0,
               registry->capacity * sizeof(*registry->pins));
    }
    registry->count = 0;
}

static inline bool pgraph_vk_resource_pin_registry_retire(
    PGRAPHVkResourcePinRegistry *registry, uint32_t submission_serial)
{
    if (!registry || !registry->submission_serial_valid ||
        registry->submission_serial != submission_serial) {
        return false;
    }

    pgraph_vk_resource_pin_registry_release_all(registry);
    registry->submission_serial_valid = false;
    registry->submission_serial = 0;
    return true;
}

/*
 * Finalization is safe for an empty or unsubmitted registry. An in-flight
 * registry must first be retired after its completion primitive has signaled.
 */
static inline bool pgraph_vk_resource_pin_registry_finalize(
    PGRAPHVkResourcePinRegistry *registry)
{
    if (!registry || registry->submission_serial_valid) {
        return false;
    }

    pgraph_vk_resource_pin_registry_release_all(registry);
    g_clear_pointer(&registry->pins, g_free);
    registry->capacity = 0;
    registry->max_pins = 0;
    return true;
}

#endif
