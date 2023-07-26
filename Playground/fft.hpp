#pragma once

#include <cmath>
#include <vector>
#include <complex>
#include <tuple>
#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <fmt/format.h>
#include "dft.hpp"


#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif // PI

void ComputeWeight(int N, int k, float &Wr, float &Wi)
{
    Wr =  cosl(2.0*PI*k/(float)N);
    Wi = -sinl(2.0*PI*k/(float)N);

}


void CreateButterflyLookups(int *butterflylookupI,
                                 float *butterflylookupWR,
                                 float *butterflylookupWI,
                                 int NButterflies, int N)
{
    int *ptr0 = butterflylookupI;
    float *ptr1 = butterflylookupWR;
    float *ptr2 = butterflylookupWI;

    int i, j, k, i1, i2, j1, j2;
    int nBlocks, nHInputs;
    float wr, wi;
    int *qtr0;
    float *qtr1, *qtr2;

    for (i = 0; i < NButterflies; i++) {
        nBlocks  = powf(2.0, (float)(NButterflies - 1 - i));
        nHInputs = powf(2.0, (float)(i));
        qtr0 = ptr0;
        qtr1 = ptr1;
        qtr2 = ptr2;
        for (j = 0; j < nBlocks; j++) {

            for (k = 0; k < nHInputs; k++) {

                if (i == 0) {
                    i1 = j*nHInputs*2 + k;
                    i2 = j*nHInputs*2 + nHInputs + k;
                    j1 = bitReverse(i1, N);
                    j2 = bitReverse(i2, N);
                }
                else {
                    i1 = j*nHInputs*2 + k;
                    i2 = j*nHInputs*2 + nHInputs + k;
                    j1 = i1;
                    j2 = i2;
                }

                ComputeWeight(N, k*nBlocks, wr, wi);

                *(qtr0 + 2*i1)   = j1;
                *(qtr0 + 2*i1+1) = j2;
                *(qtr1 + i1) = wr;
                *(qtr2 + i1) = wi;

                *(qtr0 + 2*i2)   = j1;
                *(qtr0 + 2*i2+1) = j2;
                *(qtr1 + i2) = -wr;
                *(qtr2 + i2) = -wi;
//                fmt::print("{}/{}, {:.3f} + {:.3f}i\n", k*nBlocks, N, *(qtr1 + i2), *(qtr2 + i2));
            }
        }
        ptr0 += 2*N;
        ptr1 += N;
        ptr2 += N;
    }
}

template<class Iter_T>
void fft(Iter_T a, Iter_T b, int log2n)
{
    const std::complex<double> J(0, 1);
    int n = 1 << log2n;
    for (unsigned int i=0; i < n; ++i) {
        b[bitReverse(i, n)] = a[i];
    }
    for (int s = 1; s <= log2n; ++s) {
        int m = 1 << s;
        int m2 = m >> 1;
        std::complex<double> w(1, 0);
        std::complex<double> wm = exp(-J * (PI / m2));
        for (int j=0; j < m2; ++j) {
            for (int k=j; k < n; k += m) {
                std::complex<double> t = w * b[k + m2];
                std::complex<double> u = b[k];
                b[k] = u + t;
                b[k + m2] = u - t;
            }
            w *= wm;
        }
        fmt::print("pass: {}\n", s);
        for(int i = 0; i < n; i++){
            fmt::print("{} => {}\n", i, b[i]);
        }
        fmt::print("\n");
    }
}

template<class Iter_T>
void fft_butterfly(Iter_T dataIn, Iter_T dataOut, int log2n){
    int N = 1 << log2n;
    int nButterflies = log2n;

    std::vector<std::complex<double>> butterflyLookup(N * nButterflies);
    std::vector<int> lookupIndex(N * nButterflies * 2);

    createButterflyLookups(lookupIndex, butterflyLookup, nButterflies);

    auto in = dataIn;
    auto out = dataOut;
    for(int pass = 0; pass < nButterflies; pass++){
        fmt::print("pass {}:\n", pass);
        for(int i = 0; i < N; i++){
            int i0 = static_cast<int>(lookupIndex[pass * (N * 2) + i * 2]);
            int i1 = static_cast<int>(lookupIndex[pass * (N * 2) + i * 2 + 1]);
            auto w = butterflyLookup[pass * N + i];
            auto a = in[i0];
            auto b = in[i1];

            auto res = a + b * w;
            out[i] = res;

            if(glm::epsilonEqual(w.real(), 1.0, 0.0001) && glm::epsilonEqual(w.imag(), 0.0, 0.0001)){
                fmt::print("\t{} {}, {},                   => {}\n",i ,  a, b, res);
            }else {
                fmt::print("\t{} {}, {}, {} => {}\n", i, a, b, w, res);
            }
        }
        fmt::print("\n");
        if(pass < nButterflies - 1) {
            std::swap(in, out);
        }
    }

}