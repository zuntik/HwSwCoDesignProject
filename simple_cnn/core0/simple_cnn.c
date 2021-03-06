#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "image.h"
#include "simple_cnn.h"

#if USEHARDWARE
#include "xaxidma.h"
#include "xparameters.h"
#include "xtime_l.h"
#include "xil_cache.h"
#define DMA_DEV_ID0		XPAR_AXIDMA_0_DEVICE_ID
#define DMA_DEV_ID1		XPAR_AXIDMA_1_DEVICE_ID
#define DMA_DEV_ID2		XPAR_AXIDMA_2_DEVICE_ID
#endif

#if USEDUALCORE
#include "xil_mmu.h"
#endif



#if USEHARDWARE
XAxiDma AxiDma[3];
#endif

#if USEDUALCORE
volatile int *sync_f = (int *)0xFFFFFC00;
#endif

volatile float *sum1, *sum2;

volatile unsigned char *ch_images;  // Images data region
volatile float *fp_weights; // Network weights data region

volatile float *fp_image; // Scaled floating-point image to be processed

volatile float *matA;  // Auxiliary matrix A
volatile float *matAT;  // Transpose of matA
volatile float *matB;   // Auxiliary matrix B of 22 feature maps with 5*5 weights each
volatile float *matBT;  // Transpose of matB
volatile float *matC;   // Auxiliary matrix C with intermediate output (before adding bias) in convolutional layer
volatile float *matCT;   // Transpose of matC
volatile float *matCbias; // Output of convolutional layer 22 images of size 24*24
volatile float *matCpool; // Output of pooling layer 22 images of size 12*12
volatile float *matConn;  // Intermediate output (before adding bias) of fully connected layer (10 elements)
volatile float *matConnB; // Output of fully connected layer (10 elements)
volatile float *matSoftM; // Output of softmax layer (10 elements)

volatile float *subMatsWeights; // Auxiliary matrix for 3rd layer
volatile float *aux; // Auxiliary array for 
volatile float *auxMatC;


#if USEHARDWARE
int init_XAxiDma_SimplePollMode(u16 DeviceId, int i)
{
  XAxiDma_Config *CfgPtr;
  int Status;

  /* Initialize the XAxiDma device.	 */
  CfgPtr = XAxiDma_LookupConfig(DeviceId);
  if (!CfgPtr) {
    printf("No config found for %d\r\n", DeviceId);
    return XST_FAILURE;
  }

  Status = XAxiDma_CfgInitialize(&AxiDma[i], CfgPtr);
  if (Status != XST_SUCCESS) {
    printf("Initialization failed %d\r\n", Status);
    return XST_FAILURE;
  }

  if(XAxiDma_HasSg(&AxiDma[i])){
    printf("Device configured as SG mode \r\n");
    return XST_FAILURE;
  }

  /* Disable interrupts, we use polling mode	 */
  XAxiDma_IntrDisable(&AxiDma[i], XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
  XAxiDma_IntrDisable(&AxiDma[i], XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

  return XST_SUCCESS;
}
int init_dma() {
    int a,b,c;
    a = init_XAxiDma_SimplePollMode(DMA_DEV_ID0,0);
    b = init_XAxiDma_SimplePollMode(DMA_DEV_ID1,1);
    c = init_XAxiDma_SimplePollMode(DMA_DEV_ID2,2);
    return a && b && c;
}
#endif


// Matrix multiplication: C = A * B
void gemm(float *A, float *B, float *C, int rowsA, int colsA, int colsB)
{
	int i, j, k;

	for (i=0; i<rowsA; i++) {
		for (j=0; j<colsB; j++) {
			C[i*colsB+j] = 0.0;
			for (k=0; k<colsA; k++) {
	C[i*colsB+j] += A[i*colsA+k] * B[k*colsB+j];
			}
		}
	}
}

// Matrix multiplication: C = A * transposed(B)
void gemmBT(float *A, float *B, float *C, int rowsA, int colsA, int rowsB)
{
	int i, j, k;
	int colsBT, colsB;

	colsBT = rowsB;
	colsB = colsA;
	for (i=0; i<rowsA; i++) {
		for (j=0; j<colsBT; j++) {
			C[i*colsBT+j] = 0.0;
			for (k=0; k<colsA; k++) {
	C[i*colsBT+j] += A[i*colsA+k] * B[j*colsB+k];
			}
		}
	}
}


// Transposes matrix
void transpose(float *C, int rows, int cols, float *CT)
{
	int i, j;

	for (i=0; i<rows; i++) {
		for (j=0; j<cols; j++) {
			CT[j*rows+i] = C[i*cols+j] ;
		}
	}
	
}

// Adds bias to matrix C
// if (transpose_flag == 1) also transposes input matrix
void add_bias(float *C, int rows, int cols, float *bias, float *Cbias, int transpose_flag)
{
	int i, j;

	if (transpose_flag) {
		for (i=0; i<rows; i++) {
			for (j=0; j<cols; j++) {
				Cbias[j*rows+i] = C[i*cols+j] + bias[j] ;
			}
		}
	}
	else {
		for (i=0; i<rows; i++) {
			for (j=0; j<cols; j++) {
			Cbias[i*cols+j] = C[i*cols+j] + bias[i] ;
			}
		}
	}
}

// Prepares matrix A to calculate convolutions as a matrix multiplication
void prepare_matrixA()
{
	int i, j, k, row, col;

	for (i=0; i<24; i++) {
		for (j=0; j<24; j++) {
			for (k=0; k<25; k++) {
				row = i + k/5;
				col = j + (k%5);
				// Matrix A has (24*24) rows and 25 columns
				matA[(i*24+j)*25+k] = fp_image[row*IMAGE_WIDTH+col];
			}
		}
	}
}


void prepare_sub_matrices()
{
    float *matW = (float *)fp_weights + 22 + 550 + 10;
    int i, j, k;

    for(k=0; k < 6; k++) {
        for(i=0; i<10; i++) {
            for(j=0; j<528; j++) {
                subMatsWeights[ k*5280 + (i*528 + j) ] = matW[ i*3168 +  k*528+j ];
            }
        }
    }
}


// Prints first <size> elements of matrix f
void print_fp(float *f, int size, char *c)
{
	int i;
	printf("%s\n", c);
	for (i = 0; i < size; i++) {
		if ((i % 12) == 0) printf("%02d: ", i/12);
		printf("%f ", f[i]);
		if ((i % 12) == 11) printf("\n");
	}
	printf("\n");
}

// Prints matrix elements
void print_fp_mat(float *mat, int rows, int cols)
{
	int i, j;

	for (i=0; i<rows; i++) {
		for (j=0; j<cols; j++) {
			printf("%f ", mat[i*cols+j]);
		}
		printf("\n");
	}

}

// Returns the position of the largest value in the 10-element input vector
// The softmax function normalizes the input-vector to a probability distribution
int forward_softmax_layer()
{
	int i, n=10, best=-1;
	float sum = 0.0, e;
	float largest = -FLT_MAX;

	for(i = 0; i < n; ++i){
		if(matConnB[i] > largest) {
			largest = matConnB[i];
			best = i;
		}
	}

	for(i = 0; i < n; ++i){
		e = exp(matConnB[i] - largest);
		sum += e;
		matSoftM[i] = e;
	}
	for(i = 0; i < n; ++i){
		matSoftM[i] /= sum;
	}
	//print_fp((float *)matSoftM, 10, "Softmax");

	return best;
}
#if USEDUALCORE
int forward_softmax_layer_2core()
{
	int i, best=-1;
	float sum, e;
	float largest = -FLT_MAX;

    // Finding the biggest element should be done by core 1
	for(i = 0; i < 10; ++i){
		if(matConnB[i] > largest) {
			largest = matConnB[i];
			best = i;
		}
	}
	
    // only the exponentiation should be distributed
    
    // place in core 1
	for(i = 0; i < 5; ++i){
		e = exp(matConnB[i]);
		*sum1 += e;
		matSoftM[i] = e;
	}

    // done by core 1
    sum = *sum1 + *sum2;
	for(i = 0; i < 10; ++i){
		matSoftM[i] /= sum;
	}

	//print_fp((float *)matSoftM, 10, "Softmax");

	return best;
}
#endif

// The max-pool layer condenses the 24*24 images to 12*12,
// computing the maximum value for each 2*2 input region.
void forward_maxpool_layer()
{
	int i, j, k, n, m, row, col, index;
	int size=2, stride=2;
	int oh=12, ow=12;
	int ih=24, iw=24, chan=22;
	float max = -FLT_MAX, val;
	float *pout, *pin;

	pin = (float *)matCbias;
	pout = (float *)matCpool;

	for(k = 0; k < chan; ++k){
		for(i = 0; i < oh; ++i) {
			for(j = 0; j < ow; ++j) {
				max = -FLT_MAX;
				for(n = 0; n < size; ++n){
					for(m = 0; m < size; ++m){
						row = i*stride + n;
						col = j*stride + m;
						index = col + iw * (row + ih * k);
						val = pin[index] ;
						max = (val > max) ? val : max;
					}
				}
				pout[j + ow * (i + oh * k)] = max;
			}
		}
	}
	// print_fp((float *)matCpool, 120, "Pool");
	// Output matrix Cpool is 22*144, that is this layer outputs 22 12*12 images.
}
#if USEDUALCORE 
void forward_maxpool_layer_2core()
{
	int i, j, k, n, m, row, col, index;
	int size=2, stride=2;
	int oh=12, ow=12;
	int ih=24, iw=24;
	float max = -FLT_MAX, val;
	float *pout, *pin;

	pin = (float *)matCbias;
	pout = (float *)matCpool;
	
    Xil_DCacheInvalidateRange((INTPTR)(auxMatC), (unsigned)(24*24*22));
    *sync_f = DO_SECOND_LAYER;

    // first half
	for(k = 0; k < 11; ++k){
		for(i = 0; i < oh; ++i) {
			for(j = 0; j < ow; ++j) {
	            max = -FLT_MAX;
	            for(n = 0; n < size; ++n){
		            for(m = 0; m < size; ++m){
                        row = i*stride + n;
                        col = j*stride + m;
                        index = col + iw * (row + ih * k);
                        val = pin[index] ;
                        max = (val > max) ? val : max;
		            }
	            }
	            pout[j + ow * (i + oh * k)] = max;
			}
		}
	}

    Xil_DCacheFlushRange((INTPTR)(matB), (unsigned)(4*25*11));

	while(*sync_f != SECOND_LAYER_DONE){}



	// print_fp((float *)matCpool, 120, "Pool");
	// Output matrix Cpool is 22*144, that is this layer outputs 22 12*12 images.
}
#endif


// The convolution layer consists of 22 feature maps.
// Each feature map has a set of 5*5 weights and a single bias.
void forward_convolutional_layer()
{
    // Matrix A is prepared (with 24*24=576 rows and 5*5=25 columns)
    // in order to do the convolutions as a matrix multiplication
    // such that, A(576*25) * BT(25*22) -> C(576*22)
    prepare_matrixA();

    // The 22 maps weights are stored as a 22*5*5 matrix (after the initial 22 bias values)
    matB = fp_weights + 22;
    
    // Matrix B is transposed to 25*22 for multiplication
    // You can do (1) transpose + gemm, or (2) gemmBT
    // transpose((float *)matB, 22, 25, (float *)matBT);
    // gemm((float *)matA, (float *)matBT, (float *)matC, 24*24, 25, 22);
    gemmBT((float *)matA, (float *)matB, (float *)matC, 24*24, 25, 22);
    
    // Add bias and transpose. 
    add_bias((float *)matC, 24*24, 22, (float *)fp_weights, (float *)matCbias, 1);
    // print_fp((float *)matCbias, 300, "Convolutional+Bias");
    // There is no activation function
    // Output matrix Cbias is 22*576, that is this layer outputs 22 24*24 images.

}
#if USEHARDWARE 
void forward_convolutional_layer_HARDWARE()
{
    //float *aIn, *auxOut;
    float auxMatC[24*24*22]={0};
    float *TxBufferPtr, *RxBufferPtr;

	// The 22 maps weights are stored as a 22*5*5 matrix (after the initial 22 bias values)
	matB = fp_weights + 22;

	prepare_matrixA();

    Xil_DCacheFlushRange((INTPTR)(matA), (unsigned)(4*24*24*25));
    Xil_DCacheFlushRange((INTPTR)(matB), (unsigned)(4*25*22));

    for( int i=0; i<7; i++) {
        TxBufferPtr = (float *) matB + (i*3)*25;
        if(XAxiDma_SimpleTransfer(&AxiDma[0],(UINTPTR) TxBufferPtr,
				    25*4, XAXIDMA_DMA_TO_DEVICE)!=XST_SUCCESS)printf("fail\n");
        TxBufferPtr = (float *) matB + (i*3+1)*25;
        XAxiDma_SimpleTransfer(&AxiDma[1],(UINTPTR) TxBufferPtr,
				    25*4, XAXIDMA_DMA_TO_DEVICE);
        TxBufferPtr = (float *) matB + (i*3+2)*25;
        XAxiDma_SimpleTransfer(&AxiDma[2],(UINTPTR) TxBufferPtr,
				    25*4, XAXIDMA_DMA_TO_DEVICE);


        /* Wait for Tx*/ 
        while ( XAxiDma_Busy(&AxiDma[0], XAXIDMA_DMA_TO_DEVICE) ||
                XAxiDma_Busy(&AxiDma[1], XAXIDMA_DMA_TO_DEVICE) ||
                XAxiDma_Busy(&AxiDma[2], XAXIDMA_DMA_TO_DEVICE) ) {}


        RxBufferPtr = (float *) auxMatC + (i*3)*576;
        if(XAxiDma_SimpleTransfer(&AxiDma[0],(UINTPTR) (RxBufferPtr),
				    24*24*4, XAXIDMA_DEVICE_TO_DMA)!=XST_SUCCESS)printf("fail\n");
        RxBufferPtr = (float *) auxMatC + (i*3+1)*576;
        XAxiDma_SimpleTransfer(&AxiDma[1],(UINTPTR) (RxBufferPtr),
				    24*24*4, XAXIDMA_DEVICE_TO_DMA);
        RxBufferPtr = (float *) auxMatC + (i*3+2)*576;
        XAxiDma_SimpleTransfer(&AxiDma[2],(UINTPTR) (RxBufferPtr),
				    24*24*4, XAXIDMA_DEVICE_TO_DMA);


        TxBufferPtr = (float *) matA;
        if(XAxiDma_SimpleTransfer(&AxiDma[0],(UINTPTR) TxBufferPtr,
				    24*24*25*4, XAXIDMA_DMA_TO_DEVICE)!=XST_SUCCESS)printf("fail\n");
        XAxiDma_SimpleTransfer(&AxiDma[1],(UINTPTR) TxBufferPtr,
				    24*24*25*4, XAXIDMA_DMA_TO_DEVICE);
        XAxiDma_SimpleTransfer(&AxiDma[2],(UINTPTR) TxBufferPtr,
				    24*24*25*4, XAXIDMA_DMA_TO_DEVICE);

        /* Wait for Tx*/ 
        while ( XAxiDma_Busy(&AxiDma[0], XAXIDMA_DMA_TO_DEVICE) ||
                XAxiDma_Busy(&AxiDma[1], XAXIDMA_DMA_TO_DEVICE) ||
                XAxiDma_Busy(&AxiDma[2], XAXIDMA_DMA_TO_DEVICE) ) {}

        /* Wait for Rx*/ 
        while ( XAxiDma_Busy(&AxiDma[0], XAXIDMA_DEVICE_TO_DMA) ||
                XAxiDma_Busy(&AxiDma[1], XAXIDMA_DEVICE_TO_DMA) ||
                XAxiDma_Busy(&AxiDma[2], XAXIDMA_DEVICE_TO_DMA) ) {}
 
    }

    TxBufferPtr = (float *) matB + 21*25;
    XAxiDma_SimpleTransfer(&AxiDma[0],(UINTPTR) TxBufferPtr,
                100, XAXIDMA_DMA_TO_DEVICE);


    /* Wait for Tx*/ 
    while ( XAxiDma_Busy(&AxiDma[0], XAXIDMA_DMA_TO_DEVICE)) {}

    RxBufferPtr = (float *) auxMatC + (21)*576;
    XAxiDma_SimpleTransfer(&AxiDma[0],(UINTPTR) (RxBufferPtr),
                24*24*25*4, XAXIDMA_DEVICE_TO_DMA);

    TxBufferPtr = (float *) matA;
    XAxiDma_SimpleTransfer(&AxiDma[0],(UINTPTR) TxBufferPtr,
                24*24*25, XAXIDMA_DMA_TO_DEVICE);

    /* Wait for Tx*/ 
    while ( XAxiDma_Busy(&AxiDma[0], XAXIDMA_DMA_TO_DEVICE)) {}
    /* Wait for Rx*/ 
    while ( XAxiDma_Busy(&AxiDma[0], XAXIDMA_DEVICE_TO_DMA)) {}

    Xil_DCacheInvalidateRange((INTPTR)(auxMatC), (unsigned)(4*24*24*22));
    

    transpose(auxMatC,  22, 576 , (float *)matC);

	// Add bias and transpose. 
	add_bias((float *)matC, 24*24, 22, (float *)fp_weights, (float *)matCbias, 1);
	// print_fp((float *)matCbias, 300, "Convolutional+Bias");
	// There is no activation function
	// Output matrix Cbias is 22*576, that is this layer outputs 22 24*24 images.
    

}
#endif


// This layer fully connects the 3168 inputs (22 12*12images),
// to 10 output neurons (one for each digit)
void forward_connected_layer()
{
		float *matW, *matIN, *mbias, *matOUT, *matOutB;

		// The 10 bias values of this layer are stored after the 22+550 convolutional bias+weigths
		mbias = (float *)fp_weights + 22 + 550;
		// The 10*2880 weights are stored after the 10 bias values
		matW = (float *)fp_weights + 22 + 550 + 10;
		
		matIN = (float *)matCpool;
		matOUT = (float *)matConn;
		matOutB = (float *)matConnB;
		
		// A(10*3168) * B(3168*1) -> C(10*1)
		gemm(matW, matIN, matOUT, 10, 3168, 1);
		// print_fp((float *)matConn, 10, "Connected");
		// print_fp(mbias, 10, "Bias");
			
		add_bias(matOUT, 10, 1, mbias, (float *)matOutB, 0);
		// print_fp((float *)matConnB, 10, "Connected+Bias");
		// Output vector ConnB has 10 values, one for each digit
}
#if USEHARDWARE 
void forward_connected_layer_HARDWARE()
{
    float *matIN, *mbias, *matOUT, *matOutB;
    float *TxBufferPtr, *RxBufferPtr;

    mbias = (float *)fp_weights + 22 + 550;
    
    matIN = (float *)matCpool;
    matOUT = (float *)matConn;
    matOutB = (float *)matConnB;

    //float *aux2,*aux3;
    prepare_sub_matrices();

    for(int it = 0; it < 60; it++) aux[it] = 0;
    for(int it = 0; it < 10; it++) matOUT[it] = 0;

    Xil_DCacheFlushRange((INTPTR)(aux), (unsigned)(60*4));
    Xil_DCacheFlushRange((INTPTR)(matIN), (unsigned)(12*12*22*4));
    Xil_DCacheFlushRange((INTPTR)(subMatsWeights), (unsigned)(12*12*22*10*4));

    for(int i=0; i<2; i++){
		TxBufferPtr = (float *) matIN+i*3*528;
		if(XAxiDma_SimpleTransfer(&AxiDma[0],(UINTPTR) TxBufferPtr,
					528*4, XAXIDMA_DMA_TO_DEVICE)!=XST_SUCCESS) printf("fail\n");
		TxBufferPtr = (float *) matIN+(i*3+1)*528;
		XAxiDma_SimpleTransfer(&AxiDma[1],(UINTPTR) TxBufferPtr,
					528*4, XAXIDMA_DMA_TO_DEVICE);
		TxBufferPtr = (float *) matIN+(i*3+2)*528;
		XAxiDma_SimpleTransfer(&AxiDma[2],(UINTPTR) TxBufferPtr,
					528*4, XAXIDMA_DMA_TO_DEVICE);

		/* Wait for Tx*/
		while ( XAxiDma_Busy(&AxiDma[0], XAXIDMA_DMA_TO_DEVICE) ||
				XAxiDma_Busy(&AxiDma[1], XAXIDMA_DMA_TO_DEVICE) ||
				XAxiDma_Busy(&AxiDma[2], XAXIDMA_DMA_TO_DEVICE) ) {}

		RxBufferPtr = (float *) aux+i*3*10;
		if(XAxiDma_SimpleTransfer(&AxiDma[0],(UINTPTR) (RxBufferPtr),
					10*4, XAXIDMA_DEVICE_TO_DMA)!=XST_SUCCESS) printf("fail\n");
		RxBufferPtr = (float *) aux+(i*3+1)*10;
		XAxiDma_SimpleTransfer(&AxiDma[1],(UINTPTR) (RxBufferPtr),
					10*4, XAXIDMA_DEVICE_TO_DMA);
		RxBufferPtr = (float *) aux+(i*3+2)*10;
		XAxiDma_SimpleTransfer(&AxiDma[2],(UINTPTR) (RxBufferPtr),
					10*4, XAXIDMA_DEVICE_TO_DMA);

		TxBufferPtr = (float *) subMatsWeights+i*3*5280;
		if(XAxiDma_SimpleTransfer(&AxiDma[0],(UINTPTR) TxBufferPtr,
					5280*4, XAXIDMA_DMA_TO_DEVICE)!=XST_SUCCESS) printf("fail\n");
		TxBufferPtr = (float *) subMatsWeights+(i*3+1)*5280;
		XAxiDma_SimpleTransfer(&AxiDma[1],(UINTPTR) TxBufferPtr,
					5280*4, XAXIDMA_DMA_TO_DEVICE);
		TxBufferPtr = (float *) subMatsWeights+(i*3+2)*5280;
		XAxiDma_SimpleTransfer(&AxiDma[2],(UINTPTR) TxBufferPtr,
					5280*4, XAXIDMA_DMA_TO_DEVICE);

		/* Wait for Tx*/
		while ( XAxiDma_Busy(&AxiDma[0], XAXIDMA_DMA_TO_DEVICE) ||
				XAxiDma_Busy(&AxiDma[1], XAXIDMA_DMA_TO_DEVICE) ||
				XAxiDma_Busy(&AxiDma[2], XAXIDMA_DMA_TO_DEVICE) ) {}

		/* Wait for Rx*/
		while ( XAxiDma_Busy(&AxiDma[0], XAXIDMA_DEVICE_TO_DMA) ||
				XAxiDma_Busy(&AxiDma[1], XAXIDMA_DEVICE_TO_DMA) ||
				XAxiDma_Busy(&AxiDma[2], XAXIDMA_DEVICE_TO_DMA) ) {}

    }

    Xil_DCacheInvalidateRange((INTPTR)(aux), (unsigned)(60*4));

    for(int it = 0; it < 10; it++)
    	matOUT[it] += aux[it] + aux[it+10] + aux[it+20] + aux[it+30] + aux[it+40] + aux[it+50];


    add_bias(matOUT, 10, 1, mbias, (float *)matOutB, 0);

}
#endif

// Digit classification is performed using 4 layers:
// 1. Convolutional layer
// 2. Pooling layer
// 3. Fully-connected layer
// 4. Softmax Layer
int predict_mnist()
{
	int best;
	double *ptime, *measure_time();
	
	measure_time(0);
#if USEHARDWARE
	forward_convolutional_layer_HARDWARE();
#else
	forward_convolutional_layer();
#endif

	measure_time(1);
#if USEDUALCORE
	forward_maxpool_layer_2core();
#else
	forward_maxpool_layer();
#endif

	measure_time(2);
#if USEHARDWARE
	forward_connected_layer_HARDWARE();
#else
	forward_connected_layer();
#endif

	measure_time(3);
#if 0
	forward_softmax_layer_2core();
#else
	best = forward_softmax_layer();
#endif


	ptime = measure_time(4);
#if PRINT_TIME_PER_LAYER
	printf("Layer 1 (Convolutional) took %.0f us.\n", ptime[0]);
	printf("Layer 2 (Pooling) took %.0f us.\n", ptime[1]);
	printf("Layer 3 (Fully-Connected) took %.0f us.\n", ptime[2]);
	printf("Layer 4 (Soft-max) took %.0f us.\n", 1.0 * ptime[3]);
#endif
	return best;
}


void define_memory_regions()
{
static float *paddress = (float *)MEM_DATA_BASE_ADDRESS;

	// Region Size NIMAGES*IMAGE_HEIGTH*IMAGE_WIDTH+16 = 78416 Bytes (100 images)
	ch_images = (unsigned char *)MEM_IMAGES_BASE_ADDRESS;
	// Region Size TOTAL_WEIGTHS*sizeof(float) = 29330*4 = 117320 Bytes
	fp_weights = (volatile float *)MEM_WEIGTHS_BASE_ADDRESS; 
	 
	// Region Size IMAGE_HEIGTH*IMAGE_WIDTH*sizeof(float) = 28*28*4 = 3136 Bytes
	fp_image = paddress;
	paddress += 28*28;
 
	// Aux matrix of (24*24)*(25) elements. Region Size = 14400 * 4 = 57600 Bytes
	matA = paddress;
	paddress += (24*24)*(25);
	// Transpose of matA. Region Size = 14400 * 4
	matAT = paddress;
	paddress += (24*24)*(25);
	// Aux matrix of (22)*(25) elements. Region Size = 550 * 4 Bytes;
	matB = paddress;
	paddress += (22)*(25);
	// Transpose of matB. Region Size = 550 * 4 Bytes
	matBT = paddress;
	paddress += (22)*(25);
	// Aux matrix of (24*24)*(22) elements. Region Size = 12672 * 4 Bytes;
	matC = paddress;
	paddress += (24*24)*(22);
	// Transpose of matC. Region Size = 11520 * 4 Bytes
	matCT = paddress;
	paddress += (24*24)*(22);
	// Aux matrix of (22)*(24*24) elements. Region Size = 11520 * 4 Bytes
	matCbias = paddress;
	paddress += (22)*(24*24);
	// Aux matrix of (22)*(12*12) elements. Region Size = 3168 * 4 Bytes
	matCpool = paddress;
	paddress += (22)*(12*12);
	// Aux matrix of 10 elements. Region Size = 10 * 4 Bytes;
	matConn = paddress;
	paddress += 10;
	// Aux matrix of 10 elements. Region Size = 10 * 4 Bytes
	matConnB = paddress;
	paddress += 10;
	// Aux matrix of 10 elements. Region Size = 10 * 4 Bytes
	matSoftM = paddress;
    paddress += 10;
    // Aux mat of (10)*(12*12*22) elements. Region size = 3168 * 4 Bytes
    subMatsWeights = paddress;
    paddress += 3168;
    // Aux array of 40 elements. Region size = 40 * 4 Bytes
    aux = paddress;
    paddress += 60;
    // Aux floats for sums
    sum1 = paddress;
    paddress += 1;
    sum2 = paddress;
    paddress += 1;
    auxMatC = paddress;
    paddress += 24*24*22;

#if USEDUALCORE
    //sync_f = (int*)paddress;
#endif

    //printf("%p\n",paddress);

	// printf("%p, %d\n", (void *)paddress+10, (paddress+10)-(float *)MEM_DATA_BASE_ADDRESS);
	// Total data region size is 71898 * 4 = 287,592 Bytes

}

// Measures and stores time taken between calls
// argument count must be >= 0 and < 5
double *measure_time(int count)
{
	static double timetab[5];
	
#if EMBEDDED == 1
	static XTime t[5];
	XTime_GetTime(&(t[count]));

#else
	static clock_t t[5];
	t[count] = clock();
#endif

	if (count > 0) {
		timetab[count-1] = ((double) (t[count] - t[count-1])) / (COUNTS_PER_SECOND/1000000);
	}
	return timetab;
}

// Only used for PC execution
#if EMBEDDED == 0
void upload_images_and_weights(void *pim, void *pwe, int size_im, int size_we)
{
	char header[16];
	FILE *fimages, *fweights;
	
	// Open images file
	if ((fimages = fopen("t100-images-idx3-ubyte", "r+")) == NULL) {
		fprintf(stderr, "unable to open file <t100-images-idx3-ubyte>\n");
		exit(1);
	}
	// Open weigths file
	if ((fweights = fopen("wb.bin", "r+")) == NULL) {
		fprintf(stderr, "unable to open file wb.bin\n");
		exit(1);
	}

	// read images
	fread(pim, size_im, 1, fimages);
	// read weights
	fread(pwe, size_we, 4, fweights);

	fclose(fimages);
	fclose(fweights);
}
#endif

int main(int argc, char **argv)
{

	unsigned int image_to_classify = 1; //default
	int prediction;
	
	define_memory_regions();

#if USEHARDWARE
    init_dma();
#endif

#if USEDUALCORE
	Xil_SetTlbAttributes(0xFFFFFC00,0x14de2);
    *sync_f = ZERO_STARTED;
    printf("Process on core zero has started!\n");
    while(*sync_f != ONE_STARTED){}
#endif

	
#if EMBEDDED == 0
	upload_images_and_weights((void *)ch_images, (void *)fp_weights, 
					16+NIMAGES*IMAGE_HEIGTH*IMAGE_WIDTH, TOTAL_WEIGTHS); // Only used for PC execution
#endif

	for (image_to_classify = IMAGE_TO_CLASSIFY;
			image_to_classify < (IMAGE_TO_CLASSIFY+NUMBER_OF_IMAGES_TO_CLASSIFY);
			image_to_classify++) {

		// The pixels of the input image are scaled to the [0,1[ interval
		image_scale2float((unsigned char *)ch_images, image_to_classify, (float *)fp_image);

#if PRINT_IMAGE
		print_pgm((unsigned char *)ch_images, image_to_classify);
#endif

        prediction = predict_mnist();

		printf("Image %d -> Digit %d %f\n", image_to_classify, prediction, matSoftM[prediction]*100);
	}

}
