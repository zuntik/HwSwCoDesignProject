CNN_architecture.png 
  Block diagram of CNN architecture (adapted from
  http://neuralnetworksanddeeplearning.com/chap6.html)

wb.bin
  Binary file with 22+(22*5*5)+10+(10*22*12*12) floating-point neural net weights

SDK Application Source and Header files:
  simple_cnn.h
  simple_cnn.c
  image.h
  image.c
  
Note: to use the math function exp() you must add the linker library "m" 
(in SDK Application/C Build Settings)
