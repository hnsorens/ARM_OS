# GICv3 Multi‑Core Usage

## Initialization

1. **Global initialisation (once)**  
   ```c
   int ret = init_global(gicd_base, gicr_base);
   ```  
   – sets Distributor and Redistributor base addresses, enables Group 1 Non‑Secure distribution.

2. **Per‑core initialisation (on every core that will handle interrupts)**  
   ```c
   int ret = init_core();
   ```  
   – registers the current core in the internal topology table (stores its MPIDR),  
   – wakes up the local Redistributor,  
   – programs the CPU interface system registers (ICC_PMR_EL1, ICC_BPR1_EL1, ICC_IGRPEN1_EL1),  
   – sets the exception vector table (vbar_el1).

## Starting Secondary Cores

The GICv3 module does **not** provide a core wake‑up mechanism; that is platform‑specific.  
On AArch64 systems, secondary cores are usually brought online via:

- **PSCI (Power State Coordination Interface)** – the boot core issues a `CPU_ON` SMC call to wake a target core, specifying its entry point.
- **Spin‑table** – the kernel writes the secondary entry point address in a platform‑specific table (e.g., `RELEASE[]` array in the boot loader) and then sends an inter‑processor interrupt (IPI) using a GIC SGI (Software Generated Interrupt).

Either method gives the secondary core its first instruction pointer.  

Once the secondary core starts executing its own entry code, it **must**:
1. Call `init_core()` (after `init_global()` has already been called by the boot core).
2. Configure its own per‑CPU interrupts (PPIs, including the timer).
3. (Optionally) call `route_to_core()` for any SPIs that should be delivered to this core.

### Using SGIs to send an IPI

The GIC provides a direct way to generate an SGI via the `ICC_SGI1R_EL1` system register.  
The GICv3 module does **not** currently wrap this into a separate function; you can use the raw register access shown in the existing test (`GIC_SGISelf`):

```c
uint64_t sgi_reg = ...;  // build target core list, SGI ID, etc.
__asm__ volatile("msr ICC_SGI1R_EL1, %0" : : "r"(sgi_reg));
__asm__ volatile("dsb sy");
```

For convenience, the module could be extended with an `sgi_register()` function in the future.

## Routing an SPI to a Core

Only SPI interrupts (vector ≥ 32) can be routed. Use:

```c
uint64_t mpidr = get_current_mpidr();   // obtain the target core’s MPIDR
int ret = route_to_core(vector, mpidr);  // vector must be ≥32
```

The function validates that the target core has previously been initialised via `init_core()`.  
The hardware IROUTER register is written with the unicast‑cleared MPIDR (IRM bit cleared).

## Enabling a Timer Interrupt (PPI 27)

PPI 27 is a per‑CPU interrupt; it is local to the core that enables it. It cannot be routed to a different core.

1. **Enable the interrupt**  
   ```c
   enable(27);
   ```
2. **(Optional) Configure priority and trigger type**  
   ```c
   configure(27, IRQ_TRIGGER_LEVEL, 0x80);  // or IRQ_TRIGGER_EDGE for edge‑sensitive timer
   ```
3. **Register an ISR**  
   ```c
   void my_timer_isr(void *ctx) { /* … */ }
   register_handler(27, my_timer_isr, some_context);
   ```
4. **Start the hardware timer**  
   The core timer must be programmed separately, e.g.:
   ```c
   arm64_core_timer_start(ticks);
   arm64_unmask_cpu_exceptions();
   ```

## Important Order on Secondary Cores

- **Before** any interrupt handling on a secondary core, call `init_core()` on that core.  
- `init_global()` must be called first (typically on the boot core) before any `init_core()` call.

## Example Flow (Textual)

1. **Boot core**  
   ```c
   init_global(0x8000000, 0x80A0000);
   init_core();
   ```
   Routes SPI 50 to itself:
   ```c
   route_to_core(50, get_current_mpidr());
   configure(50, IRQ_TRIGGER_EDGE, 0xA0);
   enable(50);
   register_handler(50, my_isr, my_ctx);
   ```

2. **Boot core wakes secondary core**  
   Uses PSCI or spin‑table to start the secondary core’s entry point (e.g., `secondary_entry()`).

3. **Secondary core** (first time it runs any code)  
   ```c
   init_core();          // registers itself, wakes redistributor, sets up CPU interface
   configure(27, IRQ_TRIGGER_LEVEL, 0x80);
   enable(27);
   register_handler(27, timer_isr, NULL);
   arm64_core_timer_start(100000);
   arm64_unmask_cpu_exceptions();
   ```

4. **Scheduling** – after both cores are ready, the boot core can route additional SPIs to the secondary core by calling `route_to_core(irq, secondary_mpidr)`.

All the individual API calls are demonstrated in the existing test suite (`GIC_RouteSPIToCore`, `GIC_FullSPILifecycle`, `GIC_SGISelf`).
