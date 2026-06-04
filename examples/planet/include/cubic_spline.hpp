#pragma once
#include <vector>

template<typename FloatType>
void spline_weights(FloatType u, FloatType weights[4]) {
    using Ft = FloatType;
    auto T = u,
    S = Ft(1.0) - u;
    weights[0] = (S * S * S) / Ft(6.0);
    weights[1] = ((Ft(4.0) * S * S * S + T * T * T) + (Ft(12.0) * S * T * S + Ft(6.0) * T * S * T)) / Ft(6.0);
    weights[2] = ((Ft(4.0) * T * T * T + S * S * S) + (Ft(12.0) * T * S * T + Ft(6.0) * S * T * S)) / Ft(6.0);
    weights[3] = (T * T * T) / Ft(6.0);
}

template<typename T, typename P>
T evaluate_catmull_rom_spline(const std::vector<T>& splinePoints, P t, bool loop)
{
    // First we find which segment we'll be choosing
    P tP = t * (splinePoints.size());

    auto segmentID = static_cast<int32_t>(tP);
    P remappedT = tP - static_cast<P>(segmentID);

    // Grab the 4 points we need
    const T& p0 = splinePoints[loop ? (segmentID - 1 + splinePoints.size()) % splinePoints.size() : std::max(segmentID - 1, 0)];
    const T& p1 = splinePoints[std::max(segmentID, 0)];
    const T& p2 = splinePoints[loop ? (segmentID + 1) % splinePoints.size() : std::min(segmentID + 1, int32_t(splinePoints.size()) - 1)];
    const T& p3 = splinePoints[loop ? (segmentID + 2) % splinePoints.size() : std::min(segmentID + 2, int32_t(splinePoints.size()) - 1)];

    // Evaluate the weights
    P weights[4];
    spline_weights(remappedT, weights);

    // Combine
    return p0 * weights[0] + p1 * weights[1] + p2 * weights[2] + p3 * weights[3];
}