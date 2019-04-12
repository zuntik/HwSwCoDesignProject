/*
 * fifo.h
 *
 *  Created on: 12 Apr 2019
 *      Author: thomas
 */

#ifndef SRC_FIFO_H_
#define SRC_FIFO_H_

/***************************** Include Files *********************************/
#include "xparameters.h"
#include "xil_exception.h"
#include "xstreamer.h"
#include "xil_cache.h"
#include "xllfifo.h"
#include "xstatus.h"
#include <stdio.h>

/************************** Function Prototypes ******************************/
int my_axis_fifo_init();
unsigned my_send_to_fifo(void *BufPtr, unsigned nWords);
unsigned my_receive_from_fifo(void *BufPtr, unsigned nWords);


#endif /* SRC_FIFO_H_ */
