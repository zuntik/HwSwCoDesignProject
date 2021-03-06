/*
 * dma_int_matp.c
 *
 *  Created on: 12/04/2018
 *      Author: hcn
 */

/******************************************************************************
 * Matrix Multiplication DMA Example
 *
 * Based on Xilinx xaxidma_example_simple_poll.c example,
 * which demonstrates how to use the xaxidma driver on the Xilinx AXI
 * DMA core (AXIDMA) to transfer packets in polling mode when the AXI DMA core
 * is configured in simple mode.
 * ***************************************************************************
 */
/***************************** Include Files *********************************/
#include "xaxidma.h"
#include "xparameters.h"

#include <stdio.h>
#include "xtime_l.h"
//#include "xil_mmu.h"
#include "xil_cache.h"
//#include "xil_cache_l.h"

/******************** Constant Definitions **********************************/

/* Device hardware build related constants. */
#define DMA_DEV_ID		XPAR_AXIDMA_0_DEVICE_ID

/* Program constants */
// #define MAT_SIZE 100

#define N1 50
#define N2 50
#define N3 50

volatile int *memA;   // matA N1xN2
volatile int *memB, *memTB;   // matB N2xN3
volatile int *memC, *memTC;   // matC N1xN3

#define MEMA(I,J) (memA[(I)*N2+(J)])
#define MEMB(I,J) (memB[(I)*N3+(J)])
#define MEMC(I,J) (memC[(I)*N3+(J)])
#define MEMTB(I,J) (memTB[(I)*N2+(J)])
#define MEMTC(I,J) (memTC[(I)*N1+(J)])

#define MATA_START_ADD 0x10000000
#define MATB_START_ADD (MATA_START_ADD+4*N1*N2)
#define MATC_START_ADD (MATA_START_ADD+4*N1*N2+4*N2*N3)
#define MAT_TB_START_ADD (MATC_START_ADD+4*N1*N3)
#define MAT_TC_START_ADD (MAT_TB_START_ADD+4*N2*N3)

#define MATA_SIZE_IN_BYTES (N1*N2*4)
#define COLB_SIZE_IN_BYTES (N2*4)
#define COLC_SIZE_IN_BYTES (N1*4)

#define CHECK_RESULT 1
#define DO_SW 1

/************************** Function Prototypes ******************************/

int XAxiDma_Simple_MatProd(u16 DeviceId);
int init_XAxiDma_SimplePollMode(u16 DeviceId);


/************************** Variable Definitions *****************************/
/*
 * Device instance definitions
 */
XAxiDma AxiDma;


/*****************************************************************************/
/*
* The entry point for this example. It invokes the example function,
* and reports the execution status.
*
* @param	None.
*
* @return
*		- XST_SUCCESS if example finishes successfully
*		- XST_FAILURE if example fails.
*
* @note		None.
*
******************************************************************************/
int main()
{
  int Status;

  // Xil_DCacheDisable();

  /* Init DMA in poll mode for simple transfer */
  Status = init_XAxiDma_SimplePollMode(DMA_DEV_ID);
  if (Status != XST_SUCCESS) {
    printf("init_XAxiDma_SimplePollMode: Failed\r\n");
    return XST_FAILURE;
  }

  Status = XAxiDma_Simple_MatProd(DMA_DEV_ID);
  if (Status != XST_SUCCESS) {
    printf("XAxiDma_Simple_MatProd: Failed\r\n");
    return XST_FAILURE;
  }

  return XST_SUCCESS;
}

void print_mat(int *x, int colsize, int rowsize, int firstrow)
{
  int i, j;

  for (i=firstrow-1; i<colsize; i++) {
    for (j=0; j<rowsize; j++) {
      printf("%d ", x[i*rowsize+j]);
    }
    printf("\n");
  }
  printf("\n");
}

int init_XAxiDma_SimplePollMode(u16 DeviceId)
{
  XAxiDma_Config *CfgPtr;
  int Status;

  /* Initialize the XAxiDma device.	 */
  CfgPtr = XAxiDma_LookupConfig(DeviceId);
  if (!CfgPtr) {
    printf("No config found for %d\r\n", DeviceId);
    return XST_FAILURE;
  }

  Status = XAxiDma_CfgInitialize(&AxiDma, CfgPtr);
  if (Status != XST_SUCCESS) {
    printf("Initialization failed %d\r\n", Status);
    return XST_FAILURE;
  }

  if(XAxiDma_HasSg(&AxiDma)){
    printf("Device configured as SG mode \r\n");
    return XST_FAILURE;
  }

  /* Disable interrupts, we use polling mode	 */
  XAxiDma_IntrDisable(&AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
  XAxiDma_IntrDisable(&AxiDma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

  return XST_SUCCESS;
}

int XAxiDma_Simple_MatProd(u16 DeviceId)
{
  int Status;
  int i, j, col;
  int *TxBufferPtr, *RxBufferPtr;
  XTime tStart, tEnd;

  memA = (int *)(MATA_START_ADD);
  memB = (int *)(MATB_START_ADD);
  memC = (int *)(MATC_START_ADD);
  memTB = (int *)(MAT_TB_START_ADD);
  memTC = (int *)(MAT_TC_START_ADD);

  print_mat((int *)memA,N1,N2,N1);
  print_mat((int *)memB,N2,N3,N2);
  // matrix B is transposed to be accessed by columns
  // (B size is N2xN3)
  for (i=0; i<N2; i++) {
	for (j=0; j<N3; j++) {
		MEMTB(j,i) = MEMB(i,j);
	}
  }
  print_mat((int *)memTB,N3,N2,N3);
  // flush matrix B transposed to external memory
  Xil_DCacheFlushRange((INTPTR)(memTB), (unsigned)(4*N2*N3));

 XTime_GetTime(&tStart);
 for (col=0; col < N3; col++) {
    // send column of B (row of TB)
    TxBufferPtr = (int *)memTB + (col*N2);
    Status = XAxiDma_SimpleTransfer(&AxiDma,(UINTPTR) TxBufferPtr,
				    COLB_SIZE_IN_BYTES, XAXIDMA_DMA_TO_DEVICE);
    if (Status != XST_SUCCESS) { return XST_FAILURE; }
    while (XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE)) { /* Wait for Tx*/ }

    // receive column of C (row of TC)
    RxBufferPtr = (int *)memTC + (col*N1);
    Status = XAxiDma_SimpleTransfer(&AxiDma,(UINTPTR) (RxBufferPtr),
				    COLC_SIZE_IN_BYTES, XAXIDMA_DEVICE_TO_DMA);
    if (Status != XST_SUCCESS) { return XST_FAILURE; }

    // send full matrix A
    TxBufferPtr = (int *)memA;
    Status = XAxiDma_SimpleTransfer(&AxiDma,(UINTPTR) TxBufferPtr,
				      MATA_SIZE_IN_BYTES, XAXIDMA_DMA_TO_DEVICE);
    if (Status != XST_SUCCESS) { return XST_FAILURE; }
    while (XAxiDma_Busy(&AxiDma,XAXIDMA_DMA_TO_DEVICE)) { /* Wait Tx */ }

    while (XAxiDma_Busy(&AxiDma,XAXIDMA_DEVICE_TO_DMA)) { /* Wait Rx*/ }
  }
  XTime_GetTime(&tEnd);

  // Invalidate Cache Range to force reading matrix C from external memory
  Xil_DCacheInvalidateRange((INTPTR)(memTC), (unsigned)(4*N1*N3));
  // transpose matrix TC (C size is N1xN3)
  for (i=0; i<N1; i++) {
	for (j=0; j<N3; j++) {
		MEMC(i,j) = MEMTC(j,i);
	}
  }
  print_mat((int *)memC,N1,N3,N1);

#if CHECK_RESULT
{
  int nerrors=0;
  for (i=0; i<N1; i++) {
	if (nerrors > 50) break;
	for (j=0; j<N3; j++) {
      int sum = 0;
      for (int k=0; k<N2; k++) {
    	sum += MEMA(i,k)*MEMB(k,j);
      }
  	  if (sum != MEMC(i,j)) {
  	    printf("Verification error in C(%d,%d): %d %d\n", i, j, sum, MEMC(i,j));
  	    nerrors++;
  	  }
	}
  }
  printf("Number of result errors = %d\n", nerrors);
}
#endif

  printf("Output took %llu clock cycles.\n", 2*(tEnd - tStart));
  printf("Output took %.2f us.\n",
		  1.0 * (tEnd - tStart) / (COUNTS_PER_SECOND/1000000));

#if DO_SW
  XTime_GetTime(&tStart);
  for (i=0; i<N1; i++) {
	  for (j=0; j<N3; j++) {
		  MEMC(i,j) = 0;
		  for (int k=0; k<N2; k++) {
			  MEMC(i,j) += MEMA(i,k)*MEMB(k,j);
		  }
	  }
  }
  XTime_GetTime(&tEnd);
  printf("SW Output took %llu clock cycles.\n", 2*(tEnd - tStart));
  printf("SW Output took %.2f us.\n",
		  1.0 * (tEnd - tStart) / (COUNTS_PER_SECOND/1000000));
#endif

  return XST_SUCCESS;
}


