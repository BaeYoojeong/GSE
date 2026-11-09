#pragma once
#include "GltfModel.h"
#include <algorithm>
#include <cmath>

namespace GltfModel
{
    inline Matrix Identity()
    {
        return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    }

    inline Matrix Multiply(const Matrix& a, const Matrix& b)
    {
        Matrix result = {};
        for (unsigned col = 0; col < 4; ++col)
        {
            for (unsigned row = 0; row < 4; ++row)
            {
                for (unsigned k = 0; k < 4; ++k)
                {
                    result[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
                }
            }
        }
        return result;
    }

    inline Vector Point(const Matrix& matrix, const Vector& point)
    {
        Vector result;
        for (unsigned row = 0; row < 3; ++row)
        {
            result[row] = matrix[row] * point[0] + matrix[4 + row] * point[1] + matrix[8 + row] * point[2] +
                          matrix[12 + row];
        }
        return result;
    }

    inline Rotation Normalize(Rotation value)
    {
        float length = 0;
        for (float component : value)
        {
            length += component * component;
        }
        if (length < .00000001f)
        {
            return {0, 0, 0, 1};
        }
        length = std::sqrt(length);
        for (float& component : value)
        {
            component /= length;
        }
        return value;
    }

    inline Rotation Slerp(Rotation a, Rotation b, float t)
    {
        a = Normalize(a);
        b = Normalize(b);
        float dot = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            dot += a[i] * b[i];
        }
        if (dot < 0)
        {
            for (float& value : b)
            {
                value = -value;
            }
            dot = -dot;
        }
        float wa = 1 - t, wb = t;
        if (dot < .9995f)
        {
            const float angle = std::acos((std::clamp)(dot, -1.0f, 1.0f));
            wa = std::sin((1 - t) * angle) / std::sin(angle);
            wb = std::sin(t * angle) / std::sin(angle);
        }
        Rotation result;
        for (unsigned i = 0; i < 4; ++i)
        {
            result[i] = a[i] * wa + b[i] * wb;
        }
        return Normalize(result);
    }

    inline Matrix Compose(const Transform& value)
    {
        const auto q = Normalize(value.rotation);
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        Matrix result = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),     0,
                         2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),     0,
                         2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y), 0,
                         value.translation[0],    value.translation[1],    value.translation[2],    1};
        for (unsigned col = 0; col < 3; ++col)
        {
            for (unsigned row = 0; row < 3; ++row)
            {
                result[col * 4 + row] *= value.scale[col];
            }
        }
        return result;
    }
} // namespace GltfModel
