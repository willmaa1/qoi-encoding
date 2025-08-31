# QOI Encoding

Small project to experiment with encoding and decoding QOI image files. Encoding and decoding based on the [specification document](https://qoiformat.org/qoi-specification.pdf) with [reference implementation](https://github.com/phoboslab/qoi) looked at mainly for an example on reading and writing png files.

## Dependencies

[stb](https://github.com/nothings/stb) for reading and writing png files (see files in `libs`).

## Compile and run

Modify the `main` function in `src/main.c` to perform the wanted decoding/encoding operations.

```
gcc src/main.c -Wall -lm -o main && ./main
```

## More on QOI
Official QOI website: https://qoiformat.org/  
QOI specification: https://qoiformat.org/qoi-specification.pdf  
QOI github: https://github.com/phoboslab/qoi  
