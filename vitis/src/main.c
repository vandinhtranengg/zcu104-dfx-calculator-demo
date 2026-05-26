/******************************************************************************
 * ZCU104 Bare-metal DFX Demo
 *
 * Static region:
 *   - Zynq UltraScale+ MPSoC
 *   - AXI Interconnect
 *   - DFX AXI Shutdown Manager
 *
 * Reconfigurable Partition:
 *   - RM1: rp_calculator_add
 *   - RM2: rp_calculator_sub
 *
 * HLS AXI-Lite register map:
 *   0x00 CTRL
 *   0x10 a
 *   0x18 b
 *   0x20 result
 *   0x24 result_ctrl / result_ap_vld
 *   0x30 module_id
 *   0x34 module_id_ctrl / module_id_ap_vld
 ******************************************************************************/

#include "xparameters.h"
#include "xil_printf.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xilfpga.h"
#include "sleep.h"

#include "ff.h"
#include "xstatus.h"

/* -------------------------------------------------------------------------- */
/* User configuration                                                         */
/* -------------------------------------------------------------------------- */

/*
 * Check these names in xparameters.h.
 * They may be slightly different depending on your Vivado block names.
 */
#ifndef XPAR_CALCULATOR_RP_BASEADDR
#define CALC_BASEADDR              0xA0000000U
#else
#define CALC_BASEADDR              XPAR_CALCULATOR_RP_BASEADDR
#endif

#ifndef XPAR_DFX_AXI_SHUTDOWN_MAN_0_BASEADDR
#define DFX_SHUTDOWN_BASEADDR      0xA0010000U
#else
#define DFX_SHUTDOWN_BASEADDR      XPAR_DFX_AXI_SHUTDOWN_MAN_0_BASEADDR
#endif

/*
 * DDR address used as temporary buffer for partial bitstream.
 * Make sure this address does not overlap with your program memory.
 */
#define BITSTREAM_DDR_ADDR         0x10000000U
#define MAX_BITSTREAM_SIZE         0x02000000U   /* 32 MB */

/*
 * Partial bitstream names on SD card.
 * Rename these to match your Vivado-generated partial bit files.
 */
#define ADD_BIT_FILE               "0:/ADD.BIT"
#define SUB_BIT_FILE               "0:/SUB.BIT"

/* Test values */
#define TEST_A                     20U
#define TEST_B                     7U

/* -------------------------------------------------------------------------- */
/* HLS calculator register offsets                                            */
/* -------------------------------------------------------------------------- */

#define CALC_CTRL_OFFSET           0x00U
#define CALC_A_OFFSET              0x10U
#define CALC_B_OFFSET              0x18U
#define CALC_RESULT_OFFSET         0x20U
#define CALC_RESULT_CTRL_OFFSET    0x24U
#define CALC_MODULE_ID_OFFSET      0x30U
#define CALC_MODULE_ID_CTRL_OFFSET 0x34U

/* HLS CTRL bits */
#define AP_START                   0x01U
#define AP_DONE                    0x02U
#define AP_IDLE                    0x04U
#define AP_READY                   0x08U

/* HLS ap_vld bit for output registers */
#define AP_VLD                     0x01U

/* DFX AXI Shutdown Manager control */
#define DFX_SHUTDOWN_CTRL_OFFSET   0x00U
#define DFX_SHUTDOWN_ENABLE        0x01U
#define DFX_SHUTDOWN_DISABLE       0x00U

/* -------------------------------------------------------------------------- */
/* Global objects                                                             */
/* -------------------------------------------------------------------------- */

static FATFS FatFs;
static XFpga XFpgaInstance;

/* -------------------------------------------------------------------------- */
/* Helper functions                                                           */
/* -------------------------------------------------------------------------- */

static void dfx_shutdown_enable(void)
{
    xil_printf("Enabling DFX AXI Shutdown Manager...\r\n");
    Xil_Out32(DFX_SHUTDOWN_BASEADDR + DFX_SHUTDOWN_CTRL_OFFSET, DFX_SHUTDOWN_ENABLE);
    /*
     * Small delay to allow AXI interface to become safely isolated.
     */
    usleep(1000);
}

static void dfx_shutdown_disable(void)
{
    xil_printf("Disabling DFX AXI Shutdown Manager...\r\n");
    Xil_Out32(DFX_SHUTDOWN_BASEADDR + DFX_SHUTDOWN_CTRL_OFFSET, DFX_SHUTDOWN_DISABLE);
    /*
     * Small delay to allow AXI interface to reconnect.
     */
    usleep(1000);
}

static int load_file_to_ddr(const char *FileName, UINTPTR DdrAddr, UINT *FileSize)
{
    FIL File;
    FRESULT Res;
    UINT BytesRead;
    FSIZE_t Size;

    xil_printf("Opening file: %s\r\n", FileName);
    Res = f_open(&File, FileName, FA_READ);
    if (Res != FR_OK) {
        xil_printf("ERROR: f_open failed. Code = %d\r\n", Res);
        return XST_FAILURE;
    }

    Size = f_size(&File);

    if (Size == 0 || Size > MAX_BITSTREAM_SIZE) {
        xil_printf("ERROR: Invalid bitstream size = %lu bytes\r\n",
                   (unsigned long)Size);
        f_close(&File);
        return XST_FAILURE;
    }

    xil_printf("File size = %lu bytes\r\n", (unsigned long)Size);

    Res = f_read(&File, (void *)DdrAddr, (UINT)Size, &BytesRead); //Direct DDR write
    if (Res != FR_OK) {
        xil_printf("ERROR: f_read failed. Code = %d\r\n", Res);
        f_close(&File);
        return XST_FAILURE;
    }

    f_close(&File);

    if (BytesRead != (UINT)Size) {
        xil_printf("ERROR: Bytes read mismatch. Expected %lu, got %u\r\n",
                   (unsigned long)Size, BytesRead);
        return XST_FAILURE;
    }

    Xil_DCacheFlushRange(DdrAddr, BytesRead);

    xil_printf("Loaded %u bytes to DDR address 0x%08lx\r\n", BytesRead, (unsigned long)DdrAddr);

    *FileSize = BytesRead;

    return XST_SUCCESS;
}

static int load_partial_bitstream(const char *FileName)
{
    int Status;
    UINT BitstreamSize = 0;
    UINTPTR KeyAddr = (UINTPTR)NULL;

    Status = load_file_to_ddr(FileName, BITSTREAM_DDR_ADDR, &BitstreamSize);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Failed to load bitstream file to DDR.\r\n");
        return XST_FAILURE;
    }

    xil_printf("Loading partial bitstream through PCAP...\r\n");
    //xil_printf("DDR address = 0x%08lx\r\n", (unsigned long)BITSTREAM_DDR_ADDR);
    //xil_printf("Size        = %u bytes\r\n", BitstreamSize);

    Xil_DCacheFlushRange(BITSTREAM_DDR_ADDR, BitstreamSize);

    Status = XFpga_BitStream_Load(&XFpgaInstance,
                                  BITSTREAM_DDR_ADDR,
                                  KeyAddr,
                                  BitstreamSize,
                                  XFPGA_PARTIAL_EN);

    if (Status != XFPGA_SUCCESS) {
        xil_printf("ERROR: Partial bitstream load failed. Status = %d\r\n",Status);
        return XST_FAILURE;
    }

    xil_printf("Partial bitstream loaded successfully.\r\n");

    return XST_SUCCESS;
}


static int run_calculator(u32 A, u32 B, u32 *Result, u32 *ModuleId)
{
    u32 Ctrl;
    u32 Timeout;

    xil_printf("Writing operands: A = %lu, B = %lu\r\n",
               (unsigned long)A, (unsigned long)B);

    Xil_Out32(CALC_BASEADDR + CALC_A_OFFSET, A);
    Xil_Out32(CALC_BASEADDR + CALC_B_OFFSET, B);

    /*
     * Start HLS IP.
     */
    Xil_Out32(CALC_BASEADDR + CALC_CTRL_OFFSET, AP_START);

    /*
     * Wait for AP_DONE.
     */
    Timeout = 10000000U;

    while (Timeout > 0U) {
        Ctrl = Xil_In32(CALC_BASEADDR + CALC_CTRL_OFFSET);

        if ((Ctrl & AP_DONE) != 0U) {
            break;
        }

        Timeout--;
    }

    if (Timeout == 0U) {
        xil_printf("ERROR: Calculator timeout. CTRL = 0x%08lx\r\n",
                   (unsigned long)Ctrl);
        return XST_FAILURE;
    }

    /*
     * Optional: check output valid bits.
     * HLS ap_vld usually appears at result_ctrl[0] and module_id_ctrl[0].
     */
    Timeout = 10000000U;

    while (Timeout > 0U) {
        u32 ResultValid;
        u32 ModuleIdValid;

        ResultValid   = Xil_In32(CALC_BASEADDR + CALC_RESULT_CTRL_OFFSET);
        ModuleIdValid = Xil_In32(CALC_BASEADDR + CALC_MODULE_ID_CTRL_OFFSET);

        if (((ResultValid & AP_VLD) != 0U) &&
            ((ModuleIdValid & AP_VLD) != 0U)) {
            break;
        }

        Timeout--;
    }

    if (Timeout == 0U) {
        xil_printf("WARNING: Output valid timeout. Reading data anyway.\r\n");
    }

    *Result   = Xil_In32(CALC_BASEADDR + CALC_RESULT_OFFSET);
    *ModuleId = Xil_In32(CALC_BASEADDR + CALC_MODULE_ID_OFFSET);

    xil_printf("RESULT    = %lu\r\n", (unsigned long)(*Result));
    xil_printf("MODULE ID = 0x%08lx\r\n", (unsigned long)(*ModuleId));

    return XST_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void)
{
    int Status;
    u32 Result;
    u32 ModuleId;

    xil_printf("\r\n");
    xil_printf("========================================\r\n");
    xil_printf("ZCU104 Bare-metal DFX Calculator Demo\r\n");
    xil_printf("ADD -> wait 10 seconds -> SUB\r\n");
    xil_printf("========================================\r\n");

    /*
     * Initialize XilFPGA.
     */
    xil_printf("Initializing XilFPGA...\r\n");
    Status = XFpga_Initialize(&XFpgaInstance);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: XFpga_Initialize failed. Status = %d\r\n", Status);
        return XST_FAILURE;
    }
    xil_printf("XilFPGA initialized.\r\n");

    /*
     * Mount SD card.
     */
    xil_printf("Mounting SD card...\r\n");
    Status = f_mount(&FatFs, "0:/", 1);
    if (Status != FR_OK) {
        xil_printf("ERROR: SD card mount failed. Code = %d\r\n", Status);
        return XST_FAILURE;
    }
    xil_printf("SD card mounted successfully.\r\n");

    /*
     * ----------------------------------------------------------------------
     * Step 1: Load ADD partial bitstream
     * ----------------------------------------------------------------------
     */
    xil_printf("\r\n");
    xil_printf("Step 1: Reconfigure RP with ADD module.\r\n");
    dfx_shutdown_enable();
    Status = load_partial_bitstream(ADD_BIT_FILE);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: ADD partial reconfiguration failed.\r\n");
        return XST_FAILURE;
    }
    dfx_shutdown_disable();

    /*
     * Run ADD module.
     */
    xil_printf("\r\n");
    xil_printf("Running ADD module...\r\n");
    Status = run_calculator(TEST_A, TEST_B, &Result, &ModuleId);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: ADD module failed.\r\n");
        return XST_FAILURE;
    }

    if (Result == (TEST_A + TEST_B)) {
        xil_printf("ADD result PASS.\r\n");
    } else {
        xil_printf("ADD result FAIL.\r\n");
    }

    if (ModuleId == 0x00000001U) {
        xil_printf("ADD module ID PASS.\r\n");
    } else {
        xil_printf("ADD module ID FAIL. Expected 0x00000001.\r\n");
    }

    /*
     * Wait before switching module.
     */
    xil_printf("\r\n");
    xil_printf("Waiting 10 seconds before loading SUB module...\r\n");
    sleep(10);

    /*
     * ----------------------------------------------------------------------
     * Step 2: Load SUB partial bitstream
     * ----------------------------------------------------------------------
     */
    xil_printf("\r\n");
    xil_printf("Step 2: Reconfigure RP with SUB module.\r\n");

    dfx_shutdown_enable();

    Status = load_partial_bitstream(SUB_BIT_FILE);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: SUB partial reconfiguration failed.\r\n");
        return XST_FAILURE;
    }

    dfx_shutdown_disable();

    /*
     * Run SUB module.
     */
    xil_printf("\r\n");
    xil_printf("Running SUB module...\r\n");

    Status = run_calculator(TEST_A, TEST_B, &Result, &ModuleId);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: SUB module failed.\r\n");
        return XST_FAILURE;
    }

    if (Result == (TEST_A - TEST_B)) {
        xil_printf("SUB result PASS.\r\n");
    } else {
        xil_printf("SUB result FAIL.\r\n");
    }

    if (ModuleId == 0x00000002U) {
        xil_printf("SUB module ID PASS.\r\n");
    } else {
        xil_printf("SUB module ID FAIL. Expected 0x00000002.\r\n");
    }

    xil_printf("\r\n");
    xil_printf("========================================\r\n");
    xil_printf("DFX Calculator Demo Finished\r\n");
    xil_printf("========================================\r\n");

    return XST_SUCCESS;
}
