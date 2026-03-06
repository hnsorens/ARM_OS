#include "gic/gic_types.h"
#include "serial_debug/serial_debug_inc.h"
#include "gicv3.h"

#define __MODULE_NAME_STR__ "GIC"

// System register access macros
#define SYS_READ(reg, val)  __asm__ volatile("mrs %0, " #reg : "=r"(val))
#define SYS_WRITE(reg, val) __asm__ volatile("msr " #reg ", %0" : : "r"(val) : "memory")

#define WRITE_BIT(val, idx, bit_val) ((val) = ((val) & ~(1U << (idx))) | ((uint32_t)(bit_val) << (idx)))

// ARM Generic Timer registers
#define CNTFRQ_EL0      CNTPCT_FRQ_EL0   // Alias for frequency register
#define CNTV_TVAL_EL0   CNTV_TVAL_EL0
#define CNTV_CTL_EL0    CNTV_CTL_EL0

// GICv3 register offsets
#define GICD_CTLR_BASE           0x0000

typedef struct GICD
{
  // GICD_CTRL
  struct
  {
    unsigned int EnableGrp0 : 1;
    unsigned int EnableGrp1NS : 1;
    unsigned int EnableGrp1S : 1;
    unsigned int Res0 : 1;
    unsigned int ARE_S : 1;
    unsigned int ARE_NS : 1;
    unsigned int E1NWF : 1;
    unsigned int Res1 : 24;
    unsigned int RWP : 1;
  } CTRL;

  // GICD_TYPER
  struct
  {
    unsigned int ITLinesNumber : 5;
    unsigned int CPUNumber : 3;
    unsigned int ESPI : 1;
    unsigned int NMI : 1;
    unsigned int SecurityExtn : 1;
    unsigned int num_LPIs : 5;
    unsigned int MBIS : 1;
    unsigned int LPIS : 1;
    unsigned int DVIS : 1;
    unsigned int IDbits : 5;
    unsigned int A3V : 1;
    unsigned int No1N : 1;
    unsigned int RSS : 1;
    unsigned int ESPI_range : 5;
  } TYPER;

  // GICD_IIDR
  struct 
  {
    unsigned int Implementer : 12;
    unsigned int Revision : 4;
    unsigned int Variant : 4;
    unsigned int RES0 : 4;
    unsigned int ProductID : 8;
  } IIDR;

  // GICD_TYPER2
  struct
  {
    unsigned int VID : 5;
    unsigned int RES0 : 2;
    unsigned int VIL : 1;
    unsigned int nASSGIcap : 1;
    unsigned int RES1 : 23;
  } TYPER2;

  // GICD_STATUSR
  struct 
  {
    unsigned int RRD : 1;
    unsigned int WRD : 1;
    unsigned int RWOD : 1;
    unsigned int WROD : 1;
    unsigned int RES0 : 28;
  } STATUSR;

  char res0[44];

  // GICD_SETSPI_NSR
  struct 
  {
    unsigned int INDID : 13;
    unsigned int res0 : 19;
  } SETSPI_NSR;

  char res1[4];

  // GICD_CLRSPI_NSR
  struct 
  {
    unsigned int INDID : 13;
    unsigned int res0 : 19;
  } CLRSPI_NSR;

  char res2[4];

  // GICD_SETSPI_SR
  struct 
  {
    unsigned int INDID : 13;
    unsigned int res0 : 19;
  } SETSPI_SR;

  char res3[4];

  // GICD_CLRSPI_SR
  struct 
  {
    unsigned int INDID : 13;
    unsigned int res0 : 19;
  } CLRSPI_SR;

  char res4[36];

  // GICD_IGROUPR<n>
  struct 
  {
    unsigned int bits; // 1 bits size
  } IGROUPR[32];

  // GICD_ISENABLER<n>
  struct
  {
    unsigned int bits; // 1 bit size
  } ISENABLER[32];

  // GICD_ICENABLER<n>
  struct
  {
    unsigned int bits; // 1 bits size
  } ICENABLER[32];

  // GICD_ISPENDR<n>
  struct
  {
    unsigned int bits; // 1 bit size
  } ISPENDR[32];

  // GICD_ICPENDR<n>
  struct 
  {
    unsigned int bits; // 1 bit size
  } ICPENDR[32];

  // GICD_ISACTIVER<n>
  struct 
  {
    unsigned int bits; // 1 bit size
  } ISACTIVER[32];

  //GICD_ICACTIVER<n>
  struct
  {
    unsigned int bits; // 1 bit size
  } ICACTIVER[32];

  // GICD_IPRIORITYR<n>
  struct 
  {
    unsigned char Priority_offset[1024];
  } IPRIORITYR;

  // GICD_ITARGETSR<n>
  struct
  {
    unsigned char CPU_targets_offset[1024];
  } ITARGETSR;

  // GICD_ICFGR
  struct
  {
    unsigned int bits; // 2 bit size
  } ICFGR[64];

  // GICD_IGRPMODR<n>
  struct 
  {
    unsigned int Group_modifier_bits; // 1 bit size
  } IGRPMODR[32];

  unsigned char res7[128];

  // GICD_NSACR
  struct
  {
    unsigned int NS_access; // 2 bit size
  } NSACR[64];

  // GICD_SGIR
  struct 
  {
    unsigned int INTID : 4;
    unsigned int RES0 : 11;
    unsigned int NSATT : 1;
    unsigned int CPUTargetList : 8;
    unsigned int TargetListFilter : 2;
    unsigned int RES1 : 6;
  } SGIR;

  unsigned char res8[12];

  // GICD_CPENDSGIR
  struct 
  {
    unsigned char SGI_clear_pending[16];
  } CPENDSGIR;

  // GICD_SPENDSGIR
  struct 
  {
    unsigned char SGI_clear_pending[16];
  } SPENDSGIR;

  char res5[79];

  // GICD_INMIR<n>
  struct 
  {
    unsigned int NMI;
  } INMIR[32];

  char res9[20736];

  // GICD_IROUTER<n>
  struct
  {
    unsigned int Aff0 : 8;
    unsigned int Aff1 : 8;
    unsigned int Aff2 : 8;
    unsigned int RES0 : 7;
    unsigned int Interrupt_Routing_Mode : 1;
    unsigned int Aff3 : 8;
    unsigned int RES1 : 24;
  } IROUTER[992];

} GICD;

typedef struct GICR
{
  // GICR_CTRL
  struct 
  {
    unsigned int EnableLPIs : 1;
    unsigned int CES : 1;
    unsigned int IR : 1;
    unsigned int RWP : 1;
    unsigned int RES0 : 20;
    unsigned int DPG0 : 1;
    unsigned int DPG1NS : 1;
    unsigned int DPG1S : 1;
    unsigned int RES1 : 4;
    unsigned int UWP : 1;
  } CTRL;

  // GICR_IIDR
  struct 
  {
    unsigned int Implementer : 12;
    unsigned int Revision : 4;
    unsigned int Variant : 4;
    unsigned int RES0 : 4;
    unsigned int ProductID : 8;
  } IIDR;

  // GICR_TYPER
  struct
  {
    unsigned int PLPIS : 1;
    unsigned int VLPIS : 1;
    unsigned int Dirty : 1;
    unsigned int DirectLPI : 1;
    unsigned int Last : 1;
    unsigned int DPGS : 1;
    unsigned int MPAM : 1;
    unsigned int RVPEID : 1;
    unsigned int Processor_Number : 16;
    unsigned int CommonLPIAff : 2;
    unsigned int VSGI : 1;
    unsigned int PPInum : 5;
    unsigned int Affinity_Value : 32;
  } TYPER;

  // GICR_STATUSR
  struct 
  {
    unsigned int RRD : 1;
    unsigned int WRD : 1;
    unsigned int RWOD : 1;
    unsigned int WROD : 1;
    unsigned int res0 : 28;
  } STATUSR;

  // GICR_WAKER
  struct 
  {
    unsigned int IMPLEMENTATION_DEFINED1 : 1;
    unsigned int ProcessorSleep : 1;
    unsigned int ChildrenAsleep : 1;
    unsigned int RES0 : 28;
    unsigned int IMPLEMENTATION_DEFINED2 : 1;
  } WAKER;

  //GICR_MPAMIDR
  struct 
  {
    unsigned int PARTIDmax : 16;
    unsigned int PMGmax : 8;
    unsigned int RES0 : 8;
  } MPAMIDR;

  // GICR_PARTIDR
  struct 
  {
    unsigned int PARTIDmax : 16;
    unsigned int PMGmax : 8;
    unsigned int RES0 : 8;
  } PARTIDR;

  char res0[29];

  // GICR_SETLPIR
  struct 
  {
    unsigned int pINTID;
    unsigned int something;
  } SETLPIR;

  // GICR_CLRLPIR
  struct
  {
    unsigned int pINTID;
    unsigned int something;
  } CLRLPIR;

  char res1[29];

  // GICR_PROPBASER
  struct 
  {
    unsigned int IDbits : 5;
    unsigned int RES0 : 2;
    unsigned int InnerCache : 3;
    unsigned int Shareability : 2;
    unsigned int Physical_Address1 : 20;
    unsigned int Physical_Address2 : 20;
    unsigned int RES1 : 4;
    unsigned int OuterCache : 3;
    unsigned int RES2 : 5;
  } PROPBASER;

  // GICR_PENDBASER
  struct 
  {
    unsigned int RES0 : 7;
    unsigned int InnerCache : 3;
    unsigned int Shareability : 2;
    unsigned int RES1 : 4;
    unsigned int Physical_Address1 : 16;
    unsigned int Physical_Address2 : 20;
    unsigned int RES2 : 4;
    unsigned int OuterCache : 3;
    unsigned int RES3 : 3;
    unsigned int PTZ : 1;
    unsigned int RES4 : 1;
  } PENDBASER;

  char res7[32];

  // GICR_INVLPIR
  struct 
  {
    unsigned int INTID : 32;
    unsigned int vPEID : 16;
    unsigned int RES0 : 15;
    unsigned int V : 1;
  } INVLPIR;

  char res2[8];

  // GICR_INVALLR
  struct 
  {
    unsigned int RES0 : 32;
    unsigned int vPEID : 16;
    unsigned int RES1 : 15;
    unsigned int V : 1;
  } INVALLR;

  char res3[8];

  // GICR_SYNCR
  struct 
  {
    unsigned int Busy : 1;
    unsigned int RES0 : 31;
  } SYNCR;
} GICR;

#define SGI_BASE_OFFSET           0x10000

#define GICR_IGROUPR0_OFFSET      0x0080;
#define GICR_IGROUPR1E_OFFSET     0x0084;
#define GICR_IGROUPR2E_OFFSET     0x0088;

#define GICR_ISENABLER0_OFFSET    0x0100;
#define GICR_ISENABLER1E_OFFSET   0x0104;
#define GICR_ISENABLER2E_OFFSET   0x0108;

#define WRITE_GICR_IGROUPR0(base, id, value)     ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_IGROUPR0_OFFSET)) = (*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_IGROUPR0_OFFSET)) | ((value) << (id)))
#define READ_GICR_IGROUPR0(base, id)             ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_IGROUPR0_OFFSET) >> (id)) & 0x01)
#define WRITE_GICR_IGROUPR1E(base, id, value)    ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_IGROUPR1E_OFFSET)) = (*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_IGROUPR1E_OFFSET)) | ((value) << (id)))
#define READ_GICR_IGROUPR1E(base, id)            ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_IGROUPR1E_OFFSET) >> (id)) & 0x01)
#define WRITE_GICR_IGROUPR2E(base, id, value)    ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_IGROUPR2E_OFFSET)) = (*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_IGROUPR2E_OFFSET)) | ((value) << (id)))
#define READ_GICR_IGROUPR2E(base, id)            ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_IGROUPR2E_OFFSET) >> (id)) & 0x01)

#define WRITE_GICR_ISENABLER0(base, id, value)     ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_ISENABLER0_OFFSET)) = (*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_ISENABLER0_OFFSET)) | ((value) << (id)))
#define READ_GICR_ISENABLER0(base, id)             ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_ISENABLER0_OFFSET) >> (id)) & 0x01)
#define WRITE_GICR_ISENABLER1E(base, id, value)    ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_ISENABLER1E_OFFSET)) = (*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_ISENABLER1E_OFFSET)) | ((value) << (id)))
#define READ_GICR_ISENABLER1E(base, id)            ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_ISENABLER1E_OFFSET) >> (id)) & 0x01)
#define WRITE_GICR_ISENABLER2E(base, id, value)    ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_ISENABLER2E_OFFSET)) = (*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_ISENABLER2E_OFFSET)) | ((value) << (id)))
#define READ_GICR_ISENABLER2E(base, id)            ((*(unsigned int)((base) + SGI_BASE_OFFSET + GICR_ISENABLER2E_OFFSET) >> (id)) & 0x01)


#define GICD_ISENABLERn     0x0100
#define GICD_ICENABLERn     0x0180
#define GICD_IPRIORITYRn    0x0400
#define GICD_ICFGRn         0x0C00
#define GICD_IROUTERn       0x6100  // + (irq-32)*8 for SPIs

#define GICR_WAKER          0x0014
#define GICR_SGI_BASE       0x10000 
#define GICR_IGROUPR0       (GICR_SGI_BASE + 0x0080)
#define GICR_ISENABLER0     (GICR_SGI_BASE + 0x0100)
#define GICR_ICENABLER0     (GICR_SGI_BASE + 0x0180)
#define GICR_IPRIORITYR0    (GICR_SGI_BASE + 0x0400)
#define GICR_ICFGR0         (GICR_SGI_BASE + 0x0C00)
#define GICR_ICFGR1         (GICR_SGI_BASE + 0x0C04)

static irq_handler_t g_handlers[1024] = {0};
static void *g_handler_data[1024] = {0};

int gicv3_init(base_t dist_base, base_t redist_base)
{
  volatile GICD *gicd = (volatile GICD*)dist_base;
  volatile GICR *gicr = (volatile GICR*)redist_base;

  // Disable interrupts
  gicd->CTRL.EnableGrp0 = 0;
  gicd->CTRL.EnableGrp1NS = 0;
  gicd->CTRL.EnableGrp1S = 0;

  // Enable affinity routing
  DEBUG("Setting Affinity Routing");
  gicd->CTRL.ARE_S = 1;
  gicd->CTRL.ARE_NS = 1;
  DEBUG("Successfully Set Affinity Routing");

  // Wakes up PE
  DEBUG("Waking up PE");
  gicr->WAKER.ProcessorSleep = 0;
  while (gicr->WAKER.ChildrenAsleep != 0) {}
  DEBUG("Successfully Woke up PE");

  // CPU Interface Configuration
  DEBUG("Configuring CPU Interface");

  // (Use mrs/msr registers instead of GICC mapped registers)
  unsigned long SRE_EL1;
  SYS_READ(ICC_SRE_EL1, SRE_EL1);
  SRE_EL1 |= 1;
  SYS_WRITE(ICC_SRE_EL1, SRE_EL1);

  // (allow all priorities)
  SYS_WRITE(ICC_PMR_EL1, 0xFF);

  // (All priorities in one group)
  SYS_WRITE(ICC_BPR1_EL1, 0x00);

  // ()
  SYS_WRITE(ICC_CTLR_EL1, 0x00);

  // (Enable group 1 interrupts)
  SYS_WRITE(ICC_IGRPEN1_EL1, 0x1);


  int enabledInterrupt = 30;
  gicd->IPRIORITYR.Priority_offset[enabledInterrupt] = 0x00;
  gicr->I

  __asm__ volatile("isb");

  while (1);
}

void gicv3_handle_irq(void) {

    unsigned long iar;
    SYS_READ(ICC_IAR1_EL1, iar);
    unsigned int irq = iar & 0xFFFFFF;

    if (irq == 1023) return;  // Spurious

    if (g_handlers[irq]) {
        g_handlers[irq]((int)irq, g_handler_data[irq]);
    }

    SYS_WRITE(ICC_EOIR1_EL1, iar);
    if (irq >= 32) {
        SYS_WRITE(ICC_DIR_EL1, iar);   // Deactivate SPI
    }
}