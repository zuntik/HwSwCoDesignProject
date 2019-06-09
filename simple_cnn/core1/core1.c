#include <stdlib.h>
#include <stdio.h>
#if 0
#include <math.h>
#endif
#include "core1.h"
#include "xil_mmu.h"
#include "xil_cache.h"


#define NIMAGES 100
#define IMAGE_HEIGTH 28
#define IMAGE_WIDTH 28

volatile int *sync_f = (int *)0xFFFFFC00;
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



#if 0
int forward_softmax_layer_2core()
{
	int i, n=10, best=-1;
	float e;
	float largest = -FLT_MAX;


    while(*sync_f != DO_FORTH_LAYER){}
    

    // place in core 1
	*sum2 = 0;
	for(i = 5; i < 10; ++i){
		e = exp(matConnB[i]);
		*sum2 += e;
		matSoftM[i] = e;
	}

	*sync_f = FORTH_LAYER_DONE;

	//print_fp((float *)matSoftM, 10, "Softmax");

	return best;
}
#endif


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
	
	// we wait do to stuff
	while(*sync_f != DO_SECOND_LAYER){}

    // second half
	for(k = 11; k < 22; ++k){
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

    Xil_DCacheFlushRange((INTPTR)(matB+25*11), (unsigned)(4*25*11));


	*sync_f = SECOND_LAYER_DONE;

	// print_fp((float *)matCpool, 120, "Pool");
	// Output matrix Cpool is 22*144, that is this layer outputs 22 12*12 images.
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


    //sync_f = (int*)paddress;

    //printf("%p\n",paddress);


	// printf("%p, %d\n", (void *)paddress+10, (paddress+10)-(float *)MEM_DATA_BASE_ADDRESS);
	// Total data region size is 71898 * 4 = 287,592 Bytes

}



int main(int argc, char **argv)
{
	define_memory_regions();

	Xil_SetTlbAttributes(0xFFFFFC00,0x14de2);
	
	while(*sync_f != ZERO_STARTED) {}
	*sync_f = ONE_STARTED;

    printf("Process on core one has started!\n");
	
    while(1) {
    	forward_maxpool_layer_2core();


	}

}

// vim:foldmethod=syntax
