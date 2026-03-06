import type { ModuleType, VTableFunction } from '../types'

const blk_dev: VTableFunction[] = [
	{ name: 'create', signature: 'blk_device_t (*blk_dev_create)( void* mmio_base )', description: 'something', required: true },
	{ name: 'read_sectors', signature: 'int (*blk_dev_read_sectors)( blk_device_t dev, uint64_t sector, void* buffer, uint64_t sector_count )', description: 'something', required: true },
	{ name: 'write_sector', signature: 'int (*blk_dev_write_sector)( blk_device_t dev, uint64_t sector, const void* buffer, uint64_t sector_count )', description: 'something', required: true },
	{ name: 'flush', signature: 'int (*blk_dev_flush)( void* dev )', description: 'something', required: true },
];

const gic: VTableFunction[] = [
];

const serial_debug: VTableFunction[] = [
	{ name: 'serial_printf', signature: 'int (*serial_debug_serial_printf)( char*, ... )', description: 'something', required: true },
];

const fs: VTableFunction[] = [
	{ name: 'create_fs', signature: 'void* (*fs_create_fs)( unsigned int, unsigned int )', description: 'something', required: true },
];

const pmm: VTableFunction[] = [
	{ name: 'alloc_virt_kernel', signature: 'void* (*pmm_alloc_virt_kernel)( void* vaddr, unsigned long size )', description: 'something', required: true },
	{ name: 'free_virt_kernel', signature: 'void (*pmm_free_virt_kernel)( void* vaddr, unsigned long size )', description: 'something', required: true },
	{ name: 'alloc_phys', signature: 'void* (*pmm_alloc_phys)( unsigned long order )', description: 'something', required: true },
	{ name: 'free_phys', signature: 'void (*pmm_free_phys)( void* paddr, unsigned long order )', description: 'something', required: true },
	{ name: 'memory_available', signature: 'unsigned long (*pmm_memory_available)( void )', description: 'something', required: true },
];

const str: VTableFunction[] = [
	{ name: 'memset', signature: 'void* (*str_memset)( void* s, int c, unsigned long n )', description: 'something', required: true },
	{ name: 'memcpy', signature: 'void* (*str_memcpy)( void* dest, const void* src, unsigned long n )', description: 'something', required: true },
	{ name: 'memmove', signature: 'void* (*str_memmove)( void* dest, const void* src, unsigned long n )', description: 'something', required: true },
	{ name: 'memcmp', signature: 'int (*str_memcmp)( const void* s1, const void* s2, unsigned long n )', description: 'something', required: true },
	{ name: 'memchr', signature: 'void* (*str_memchr)( const void* s, int c, unsigned long n )', description: 'something', required: true },
	{ name: 'strlen', signature: 'unsigned long (*str_strlen)( const char* s )', description: 'something', required: true },
	{ name: 'strcpy', signature: 'char* (*str_strcpy)( char* dest, const char* src )', description: 'something', required: true },
	{ name: 'strncpy', signature: 'char* (*str_strncpy)( char* dest, const char* src, unsigned long n )', description: 'something', required: true },
	{ name: 'strcat', signature: 'char* (*str_strcat)( char* dest, const char* src )', description: 'something', required: true },
	{ name: 'strncat', signature: 'char* (*str_strncat)( char* dest, const char* src, unsigned long n )', description: 'something', required: true },
	{ name: 'strcmp', signature: 'int (*str_strcmp)( const char* s1, const char* s2 )', description: 'something', required: true },
	{ name: 'strncmp', signature: 'int (*str_strncmp)( const char* s1, const char* s2, unsigned long n )', description: 'something', required: true },
	{ name: 'strchr', signature: 'char* (*str_strchr)( const char* s, int c )', description: 'something', required: true },
	{ name: 'strrchr', signature: 'char* (*str_strrchr)( const char* s, int c )', description: 'something', required: true },
	{ name: 'strstr', signature: 'char* (*str_strstr)( const char* haystack, const char* needle )', description: 'something', required: true },
	{ name: 'strdup', signature: 'char* (*str_strdup)( const char* s )', description: 'something', required: true },
];

const kmm: VTableFunction[] = [
	{ name: 'kmalloc', signature: 'void* (*kmm_kmalloc)( unsigned long size )', description: 'something', required: true },
	{ name: 'kmalloc_aligned', signature: 'void* (*kmm_kmalloc_aligned)( unsigned long size, unsigned long align )', description: 'something', required: true },
	{ name: 'kcalloc', signature: 'void* (*kmm_kcalloc)( unsigned long count, unsigned long size )', description: 'something', required: true },
	{ name: 'krealloc', signature: 'void* (*kmm_krealloc)( void* ptr, unsigned long size )', description: 'something', required: true },
	{ name: 'kfree', signature: 'void (*kmm_kfree)( void* ptr )', description: 'something', required: true },
	{ name: 'ksalloc', signature: 'void* (*kmm_ksalloc)( int order )', description: 'something', required: true },
	{ name: 'ksfree', signature: 'void (*kmm_ksfree)( int order, void* addr )', description: 'something', required: true },
];

const gpt: VTableFunction[] = [
	{ name: 'create', signature: 'gpt_partition_t* (*gpt_create)( blk_device_t dev )', description: 'something', required: true },
];

const ide: VTableFunction[] = [
	{ name: 'read_fn', signature: 'void* (*ide_read_fn)( unsigned int, unsigned int )', description: 'something', required: true },
	{ name: 'write_fn', signature: 'void (*ide_write_fn)( unsigned int, unsigned int, void* )', description: 'something', required: true },
];

const vmm: VTableFunction[] = [
	{ name: 'pages_map_kernel', signature: 'void (*mmu_pages_map_kernel)( void* vaddr, void* paddr, unsigned long page_order, unsigned long page_count )', description: 'something', required: true },
	{ name: 'virt_to_phys_kernel', signature: 'unsigned long (*mmu_virt_to_phys_kernel)( void* vaddr )', description: 'something', required: true },
];

const bus_controller: VTableFunction[] = [
	{ name: 'find_device', signature: 'uintptr_t (*bus_controller_find_device)( unsigned char id )', description: 'something', required: true },
	{ name: 'init_device', signature: 'void (*bus_controller_init_device)( void* device_base )', description: 'something', required: true },
	{ name: 'setup_queue', signature: 'int (*bus_controller_setup_queue)( void* base, uint32_t queue_idx, void* queue )', description: 'something', required: true },
	{ name: 'submit_request', signature: 'int (*bus_controller_submit_request)( void* base, uint32_t type, void* queue, void* req, unsigned long req_len, void* data, unsigned long data_len, uint8_t* status )', description: 'something', required: true },
];

export const moduleCategories: Record<string, ModuleType[]> = {
    'Hardware': [
        {
            type: "Hardware",
            name: "HPET Timer",
            description: "High Precision Event Timer",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "x86_64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [],
            incompatibleWith: [],
            hardwareRequirements: [
                "HPET"
            ],
            vtable: hardwareVTable
        },
        {
            type: "Hardware",
            name: "HPET Timer",
            description: "High Precision Event Timer",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "x86_64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [],
            incompatibleWith: [],
            hardwareRequirements: [
                "HPET"
            ],
            vtable: hardwareVTable
        },
        {
            type: "Hardware",
            name: "HPET Timer",
            description: "High Precision Event Timer",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "x86_64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [],
            incompatibleWith: [],
            hardwareRequirements: [
                "HPET"
            ],
            vtable: hardwareVTable
        },
        {
            type: "Hardware",
            name: "HPET Timer",
            description: "High Precision Event Timer",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "x86_64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [],
            incompatibleWith: [],
            hardwareRequirements: [
                "HPET"
            ],
            vtable: hardwareVTable
        },
        {
            type: "Hardware",
            name: "HPET Timer",
            description: "High Precision Event Timer",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "x86_64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [],
            incompatibleWith: [],
            hardwareRequirements: [
                "HPET"
            ],
            vtable: hardwareVTable
        },
        {
            type: "Hardware",
            name: "HPET Timer",
            description: "High Precision Event Timer",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "x86_64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [],
            incompatibleWith: [],
            hardwareRequirements: [
                "HPET"
            ],
            vtable: hardwareVTable
        },
        {
            type: "Hardware",
            name: "HPET Timer",
            description: "High Precision Event Timer",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "x86_64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [],
            incompatibleWith: [],
            hardwareRequirements: [
                "HPET"
            ],
            vtable: hardwareVTable
        },
        {
            type: "Hardware",
            name: "HPET Timer",
            description: "High Precision Event Timer",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "x86_64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [],
            incompatibleWith: [],
            hardwareRequirements: [
                "HPET"
            ],
            vtable: hardwareVTable
        },
        {
            type: "Hardware",
            name: "HPET Timer",
            description: "High Precision Event Timer",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "x86_64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [],
            incompatibleWith: [],
            hardwareRequirements: [
                "HPET"
            ],
            vtable: hardwareVTable
        },
        {
            type: "Hardware",
            name: "HPET Timer",
            description: "High Precision Event Timer",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "x86_64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [],
            incompatibleWith: [],
            hardwareRequirements: [
                "HPET"
            ],
            vtable: hardwareVTable
        },
        {
            type: "Hardware",
            name: "HPET Timer",
            description: "High Precision Event Timer",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "x86_64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [],
            incompatibleWith: [],
            hardwareRequirements: [
                "HPET"
            ],
            vtable: hardwareVTable
        }
    ],
    'Bus Controller': [
        {
            type: "Bus Controller",
            name: "Virtio Bus Controller",
            description: "Bus Controller for Virtio Devices",
            icon: "Timer",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "aarch64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [
                "str",
                "pmm",
                "serial_debug"
            ],
            incompatibleWith: [],
            hardwareRequirements: [],
            vtable: bus_controller
        }
    ],
    'VMM': [
        {
            type: "VMM",
            name: "Virtual Memory Allocator",
            description: "Maps Memory to Page Tables",
            icon: "",
            layer: "hardware",
            source: "builtin",
            targetArchitectures: [
                "aarch64"
            ],
            defaultConfig: {
                frequency: 1000,
                oneShot: false
            },
            configSchema: [
                {
                    key: "frequency",
                    label: "Frequency (Hz)",
                    type: "number",
                    default: 1000,
                    min: 1,
                    max: 10000,
                    description: "Timer interrupt frequency"
                }
            ],
            dependencies: [
                "serial_debug",
                "pmm"
            ],
            incompatibleWith: [],
            hardwareRequirements: [],
            vtable: vmm
        }
    ]
}