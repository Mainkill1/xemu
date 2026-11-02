/*
 * Optional Xecuter 3 boot identification interface.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This models only the documented read-only identification port. X3 flash
 * banking, programming and display hardware are not implemented here.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/isa/isa.h"
#include "qom/object.h"

#define TYPE_X3_BOOT_INTERFACE "x3-boot-interface"
OBJECT_DECLARE_SIMPLE_TYPE(X3BootInterface, X3_BOOT_INTERFACE)

/*
 * Published hardware contract; no firmware implementation is reproduced.
 * https://github.com/Team-Resurgent/PrometheOS-Firmware/blob/
 * fff36b28dddf188974bba49409c66799ebe022d0/
 * PrometheOSXbe/PrometheOSXbe/globalDefines.h
 */
#define X3_IDENTIFICATION_PORT 0xf500
#define X3_IDENTIFICATION_VALUE 0xe1

struct X3BootInterface {
    ISADevice parent_obj;
    MemoryRegion identification;
};

static uint64_t x3_identification_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    return X3_IDENTIFICATION_VALUE;
}

static void x3_identification_write(void *opaque, hwaddr offset, uint64_t value,
                                    unsigned size)
{
    /* The hardware identification value is read-only. */
}

static const MemoryRegionOps x3_identification_ops = {
    .read = x3_identification_read,
    .write = x3_identification_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void x3_boot_interface_realize(DeviceState *dev, Error **errp)
{
    X3BootInterface *s = X3_BOOT_INTERFACE(dev);

    memory_region_init_io(&s->identification, OBJECT(dev),
                          &x3_identification_ops, s, TYPE_X3_BOOT_INTERFACE, 1);
    memory_region_add_subregion(isa_address_space_io(ISA_DEVICE(dev)),
                                X3_IDENTIFICATION_PORT, &s->identification);
}

static void x3_boot_interface_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = x3_boot_interface_realize;
    dc->desc = "Xecuter 3 boot identification port (no flash banking)";
}

static const TypeInfo x3_boot_interface_type = {
    .name = TYPE_X3_BOOT_INTERFACE,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(X3BootInterface),
    .class_init = x3_boot_interface_class_init,
};

static void x3_boot_interface_register_types(void)
{
    type_register_static(&x3_boot_interface_type);
}

type_init(x3_boot_interface_register_types)
