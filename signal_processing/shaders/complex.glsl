#ifndef COMPLEX_GLSL
#define COMPLEX_GLSL

struct Complex{
    float real;
    float imaginary;
};

Complex add(Complex a, Complex b){
    return Complex(a.real + b.real, a.imaginary + b.imaginary);
}

Complex mul(Complex c, float scalar){
    return Complex(c.real * scalar, c.imaginary * scalar);
}

Complex mul(Complex a, Complex b){
    float real = a.real * b.real - a.imaginary * b.imaginary;
    float imaginary = a.real * b.imaginary + a.imaginary * b.real;
    return Complex(real, imaginary);
}

Complex exp(Complex i){
    return Complex(cos(i.imaginary), sin(i.imaginary));
}

Complex norm(Complex c){
    vec2 v = normalize(vec2(c.real, c.imaginary));
    return Complex(v.x, v.y);
}

Complex conjugate(Complex c){
    return Complex(c.real, -c.imaginary);
}

float magnitude(Complex c){
    return length(vec2(c.real, c.imaginary));
}

Complex complexFrom(vec2 v){
    return Complex(v.x, v.y);
}

#endif // COMPLEX_GLSL