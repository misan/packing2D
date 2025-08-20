#include "MVector.h"

MVector::MVector(double x, double y) : x(x), y(y) {}

double MVector::getX() const {
    return x;
}

double MVector::getY() const {
    return y;
}

MVector MVector::inverse() const {
    return MVector(-x, -y);
}