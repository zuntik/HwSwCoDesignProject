#include "fifo.h"

#define IWIDTH  28
#define IHEIGHT 28
#define K_SIZE 5
#define OWIDTH  (IWIDTH-K_SIZE+1)
#define OHEIGHT (IHEIGHT-K_SIZE+1)
volatile char *image_in;
volatile int *image_out;
#define IMAGEIN(I,J) (image_in[(I)*IWIDTH+(J)])
#define IMAGEOUT(I,J) (image_out[(I)*OWIDTH+(J)])
#define WEIGHTS(I,J) (kernel[(I)*K_SIZE+(J)])

#define CONVHEIGHT (OWIDTH * OHEIGHT) 
#define PADDING_SIZE (4-(K_SIZE*K_SIZE)%4)%4
#define CONVWIDTH (K_SIZE * K_SIZE) + PADDING_SIZE
volatile char *image_conv;
#define IMAGECONV(I,J) (image_conv[(I)*CONVWIDTH+(J)])

// the image to be used should go from 1 to 100
#define IMAGE_TO_USE 1

#define FILE_START_ADDRESS 0x100000
#define IMAGE_IN_START_ADDRESS (FILE_START_ADDRESS+16+(IMAGE_TO_USE-1)*IWIDTH*IHEIGHT)

#define IMAGE_OUT_START_ADDRESS (FILE_START_ADDRESS+16+100*IWIDTH*IHEIGHT)
#define IMAGE_CONV_START_ADDRESS (IMAGE_OUT_START_ADDRESS+OWIDTH*OHEIGHT)

#define KERNEL 0

#if KERNEL == 0
static int kernel[K_SIZE*K_SIZE] = {
     0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,
     0,  0,  1,  0,  0,
     0,  0,  0,  0,  0,
     0,  0,  0,  0,  0 };
static int bias=0;
#elif KERNEL == 1
static int kernel[K_SIZE*K_SIZE] = {
    -1, -1, -1, -1,  0,
    -1, -1, -1,  0,  1,
    -1, -1,  0,  1,  1,
    -1,  0,  1,  1,  1,
     0,  1,  1,  1,  1 };
static int bias=128;
#endif

void create_matrix(){
    int i, j, k1, k2;

    for(i = 0; i < OHEIGHT; i++){
        for(j = 0; j < OWIDTH; j++){
            for(k1 = i; k1 < i + K_SIZE; k1++){
                for(k2 = j; k2 < j + K_SIZE; k2++){
                    IMAGECONV(i*OWIDTH+j,(k1-i)*K_SIZE+(k2-j)) = IMAGEIN(k1,k2);
                }
            }
        }
    }

    for(i=0;i<CONVHEIGHT;i++){
		for(j=K_SIZE*K_SIZE;j<CONVWIDTH;j++){
			IMAGECONV(i,j) = 0;
		}
	}

}

void mat_vec() {
    int  acc = 0;
    for ( int i = 0; i < CONVHEIGHT ; i ++ ) {
        acc = 0;
        for ( int j = 0 ; j < CONVWIDTH ; j++ ) {
            acc += kernel[j] * IMAGECONV(i,j) ;
        }
        acc += bias;
        image_out[i] = acc;
    }
}



void print_pgm_in(int invert)
{
    int i, j;

    printf("P2\n%d %d 255\n", IHEIGHT, IWIDTH);
    for (i=0; i < IHEIGHT; i++) {
        for (j=0; j < IWIDTH; j++) {
            if (invert == 1) {
                printf("%3d ", 255-IMAGEIN(i,j));
            }
            else {
                printf("%3d ", IMAGEIN(i,j));
            }
        }
        printf("\n");
    }
}


void print_pgm_out(int invert, int saturate)
{
    int i, j, pixel;

    printf("P2\n%d %d 255\n", OHEIGHT, OWIDTH);
    for (i=0; i < OHEIGHT; i++) {
        for (j=0; j < OWIDTH; j++) {
            pixel = IMAGEOUT(i,j);
            if (saturate == 1) {
                pixel = (pixel < 0) ? 0 : pixel;
                pixel = (pixel > 255) ? 255 : pixel;
            }
            if (invert == 1) {
                pixel = 255-pixel;
            }
            printf("%3d ", pixel);
        }
        printf("\n");
    }
}


int main()
{

	int nwords;

    image_in = (char*)(IMAGE_IN_START_ADDRESS);
    image_out = (int *)(IMAGE_OUT_START_ADDRESS);
    image_conv = (char *)(IMAGE_CONV_START_ADDRESS);


    //print_pgm_in(0);
    //convolution_2D();
    //print_pgm_out(0, 0);

    create_matrix();
    //print_pgm_out(0, 0);




    my_axis_fifo_init();


    printf("hi\n");
	nwords = my_send_to_fifo((void *) kernel, K_SIZE*K_SIZE);
	printf("%d\n",nwords);
	nwords = my_send_to_fifo((void *) image_conv, CONVHEIGHT*CONVWIDTH );
	printf("%d\n",nwords);
	nwords = my_receive_from_fifo((void *) image_out, OWIDTH*OHEIGHT);
	printf("%d\n",nwords);


    //mat_vec();

    print_pgm_out(0, 0);

    printf("adeus\n");
    return 0;
}
